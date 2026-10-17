// SPDX-License-Identifier: LicenseRef-OUT-Source-Review-1.0
// Copyright (c) 2026 Алексей Савельев (Alex Merqury). See ../../../LICENSE.
#include "out_motor.hpp"
#include <algorithm>
#include <limits>
namespace out::motor {
namespace {
constexpr float Pi = 3.14159265358979323846f;
float clamp(float v, float a, float b) {
    return std::max(a, std::min(b, v));
}
float repeat(float v) {
    return v - std::floor(v);
}
} // namespace
FootPlanner::FootPlanner(size_t capacity, std::shared_ptr<const DataBank> data)
    : feet_(capacity), initialized_(capacity), data_(std::move(data)) {
    if (!capacity || capacity > 65536 || !data_)
        throw std::invalid_argument("Invalid foot planner");
    upper_ = Vec::distance(data_->pose(0, 10), data_->pose(0, 11));
    lower_ = Vec::distance(data_->pose(0, 11), data_->pose(0, 12));
    toe_ = Vec::distance(data_->pose(0, 12), data_->pose(0, 13));
}
void FootPlanner::reset(int actor, const Vec* p, Quaternion heading) {
    if (actor < 0 || size_t(actor) >= feet_.size() || !p)
        throw std::out_of_range("Foot actor");
    for (int f = 0; f < 2; f++) {
        auto& foot = feet_[actor][f];
        foot = {};
        foot.planted = p[f ? 16 : 12];
        foot.forward = heading.rotate({0, 0, 1});
        foot.liftOff = foot.landing = foot.planted;
    }
    initialized_[actor] = 1;
}
bool FootPlanner::unfinished(int a) const {
    if (a < 0 || size_t(a) >= feet_.size())
        throw std::out_of_range("Foot actor");
    return feet_[a][0].swinging || feet_[a][1].swinging;
}
void FootPlanner::apply(int actor, float dt, float normalized, uint8_t contact, float clipDuration,
                        Vec worldVelocity, Quaternion heading, Vec anchor, Vec* goals,
                        Vec* velocities, const uint8_t* detached, bool running, bool authored) {
    if (actor < 0 || size_t(actor) >= feet_.size() || !goals || !velocities || !detached ||
        !std::isfinite(dt) || !std::isfinite(normalized) || !std::isfinite(clipDuration) ||
        clipDuration <= 0 || !worldVelocity.finite() || !anchor.finite())
        throw std::invalid_argument("Invalid foot input");
    if (!initialized_[actor])
        reset(actor, goals, heading);
    dt = clamp(dt, .001f, .1f);
    bool moving = worldVelocity.squared() > .0001f;
    Vec forward = heading.rotate({0, 0, 1}), right = heading.rotate({1, 0, 0});
    auto set = [&](int joint, Vec target) {
        Vec previous = goals[joint] - velocities[joint] * dt;
        goals[joint] = target;
        velocities[joint] = (target - previous) / dt;
    };
    for (int f = 0; f < 2; f++) {
        auto& foot = feet_[actor][f];
        int ankle = f ? 16 : 12, toe = ankle + 1, knee = ankle - 1, hip = ankle - 2;
        if (detached[ankle]) {
            foot.swinging = 0;
            continue;
        }
        bool swing = moving && (contact & (1 << f)) == 0;
        if (authored && swing) {
            foot.swinging = 2;
            foot.landing = goals[ankle];
            foot.landingForward = forward;
            continue;
        }
        if (foot.swinging == 2) {
            if (moving) {
                foot.planted = goals[ankle];
                foot.planted.y = DataBank::radius(ankle);
                foot.forward = forward;
                foot.swinging = 0;
            } else {
                foot.liftOff = foot.landing;
                foot.landing.y = DataBank::radius(ankle);
                foot.progress = 0;
                foot.duration = .2f;
                foot.swinging = 3;
            }
        }
        float phase = repeat(normalized + f * .5f);
        if (swing && phase < .6f)
            phase = repeat(phase + .5f);
        float authoredProgress = clamp((phase - .6f) / .4f, 0, 1);
        if (swing && !foot.swinging) {
            foot.swinging = 1;
            foot.liftOff = foot.planted;
            foot.duration = std::max(.12f, clipDuration * .4f);
            float reach =
                std::min(running ? .5f : .35f, worldVelocity.length() * foot.duration * .5f);
            foot.landing = anchor + right * (f ? .15f : -.15f) + forward * reach;
            foot.landing.y = DataBank::radius(ankle);
            foot.landingForward = forward;
            foot.progress = authoredProgress;
        }
        if (foot.swinging) {
            if (swing)
                foot.progress = std::max(foot.progress, authoredProgress);
            else if (moving)
                foot.progress = 1;
            else
                foot.progress = std::min(1.f, foot.progress + dt / foot.duration);
            if (foot.progress >= .9999f) {
                foot.planted = foot.landing;
                foot.forward = foot.landingForward;
                foot.swinging = 0;
            }
        }
        float u = foot.progress,
              lift = foot.swinging && foot.swinging != 3 ? std::sin(u * Pi) * (running ? .18f : .1f)
                                                         : 0,
              blend = u * u * (3 - 2 * u);
        Vec target = foot.swinging ? Vec::lerp(foot.liftOff, foot.landing, blend) + Vec{0, lift, 0}
                                   : foot.planted;
        Vec footForward = foot.swinging ? foot.landingForward : foot.forward;
        set(ankle, target);
        float toeY = DataBank::radius(toe) - DataBank::radius(ankle),
              horizontal = std::sqrt(std::max(0.f, toe_ * toe_ - toeY * toeY));
        set(toe, target + footForward * horizontal + Vec{0, toeY, 0});
        Vec hipGoal = goals[hip], chord = target - hipGoal;
        float distance = chord.length();
        if (distance > .0001f) {
            Vec axis = chord / distance;
            float reachable = clamp(distance, .05f, upper_ + lower_ - .002f),
                  along =
                      (upper_ * upper_ - lower_ * lower_ + reachable * reachable) / (2 * reachable),
                  bend = std::sqrt(std::max(.0016f, upper_ * upper_ - along * along));
            Vec bendForward = Vec::plane(forward, axis).normalized();
            if (bendForward.squared() < .01f)
                bendForward = Vec::plane({0, 1, 0}, axis).normalized();
            set(knee, Vec::lerp(goals[knee], hipGoal + axis * along + bendForward * bend, .65f));
        }
    }
}
FlowField::FlowField(int width, float size, Vec centre)
    : width_(std::max(16, std::min(128, width))), cell_(std::max(.5f, size)),
      origin_(centre - Vec{width_ * cell_ * .5f, 0, width_ * cell_ * .5f}),
      blocked_(size_t(width_) * width_), queue_(size_t(width_) * width_) {
    if (!std::isfinite(size) || !centre.finite())
        throw std::invalid_argument("Invalid navigation grid");
    for (auto& field : distance_)
        field.assign(blocked_.size(), 65535);
}
std::array<int, 2> FlowField::cell(Vec p) const {
    if (!p.finite())
        throw std::invalid_argument("Invalid grid position");
    return {int(clamp(std::floor((p.x - origin_.x) / cell_), 0, float(width_ - 1))),
            int(clamp(std::floor((p.z - origin_.z) / cell_), 0, float(width_ - 1)))};
}
void FlowField::bake(const std::function<bool(Vec, Vec)>& query) {
    for (int y = 0; y < width_; y++)
        for (int x = 0; x < width_; x++)
            blocked_[size_t(y) * width_ + x] = uint8_t(
                query && query({origin_.x + (x + .5f) * cell_, 1, origin_.z + (y + .5f) * cell_},
                               {cell_ * .38f, 1, cell_ * .38f}));
}
void FlowField::rebuild(Vec a, Vec b) {
    flood(a, distance_[0]);
    flood(b, distance_[1]);
}
void FlowField::flood(Vec goal, std::vector<uint16_t>& field) {
    std::fill(field.begin(), field.end(), uint16_t(65535));
    auto p = cell(goal);
    int x = p[0], y = p[1];
    if (blocked_[size_t(y) * width_ + x]) {
        bool found = false;
        for (int radius = 1; radius < width_ / 2 && !found; radius++)
            for (int dy = -radius; dy <= radius && !found; dy++)
                for (int dx = -radius; dx <= radius && !found; dx++) {
                    int xx = x + dx, yy = y + dy;
                    if (xx >= 0 && yy >= 0 && xx < width_ && yy < width_ &&
                        !blocked_[size_t(yy) * width_ + xx]) {
                        x = xx;
                        y = yy;
                        found = true;
                    }
                }
        if (!found)
            return;
    }
    size_t head = 0, tail = 0;
    int start = y * width_ + x;
    queue_[tail++] = start;
    field[start] = 0;
    constexpr int dx[] = {1, 0, -1, 0}, dy[] = {0, 1, 0, -1};
    while (head < tail) {
        int at = queue_[head++];
        for (int k = 0; k < 4; k++) {
            int xx = at % width_ + dx[k], yy = at / width_ + dy[k];
            if (xx < 0 || yy < 0 || xx >= width_ || yy >= width_)
                continue;
            int next = yy * width_ + xx;
            if (!blocked_[next] && field[next] == 65535) {
                field[next] = uint16_t(field[at] + 1);
                queue_[tail++] = next;
            }
        }
    }
}
Vec FlowField::direction(Vec position, int team) const {
    if (team < 0 || team > 1)
        throw std::invalid_argument("Navigation team");
    auto p = cell(position);
    auto& field = distance_[team];
    uint16_t best = field[size_t(p[1]) * width_ + p[0]];
    Vec result;
    constexpr int dx[] = {1, 0, -1, 0, 1, -1, 1, -1}, dy[] = {0, 1, 0, -1, 1, 1, -1, -1};
    for (int k = 0; k < 8; k++) {
        int x = p[0] + dx[k], y = p[1] + dy[k];
        if (x < 0 || y < 0 || x >= width_ || y >= width_)
            continue;
        int at = y * width_ + x;
        if (blocked_[at] || field[at] >= best)
            continue;
        if (k >= 4 && (blocked_[size_t(p[1]) * width_ + x] || blocked_[size_t(y) * width_ + p[0]]))
            continue;
        Vec d = {origin_.x + (x + .5f) * cell_ - position.x, 0,
                 origin_.z + (y + .5f) * cell_ - position.z};
        if (d.squared() > .0001f) {
            result = d.normalized();
            best = field[at];
        }
    }
    return result;
}
bool FlowField::blocked(Vec p) const {
    auto xy = cell(p);
    return blocked_[size_t(xy[1]) * width_ + xy[0]] != 0;
}
int FlowField::blockedCount() const {
    return int(std::count(blocked_.begin(), blocked_.end(), uint8_t(1)));
}
} // namespace out::motor
