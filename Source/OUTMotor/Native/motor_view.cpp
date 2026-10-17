// SPDX-License-Identifier: LicenseRef-OUT-Source-Review-1.0
// Copyright (c) 2026 Алексей Савельев (Alex Merqury). See ../../../LICENSE.
#include "out_motor.hpp"
#include <algorithm>
#include <chrono>
namespace out::motor {
namespace {
float unit(float v) {
    return std::max(0.f, std::min(1.f, v));
}
Quaternion normalize(Quaternion q) {
    float n = std::sqrt(q.x * q.x + q.y * q.y + q.z * q.z + q.w * q.w);
    if (n < 1e-8f)
        return {};
    return {q.x / n, q.y / n, q.z / n, q.w / n};
}
} // namespace
Quaternion Quaternion::operator*(Quaternion b) const {
    return {w * b.x + x * b.w + y * b.z - z * b.y, w * b.y - x * b.z + y * b.w + z * b.x,
            w * b.z + x * b.y - y * b.x + z * b.w, w * b.w - x * b.x - y * b.y - z * b.z};
}
Quaternion Quaternion::inverse() const {
    float n = x * x + y * y + z * z + w * w;
    if (!std::isfinite(n) || n < 1e-8f)
        throw std::invalid_argument("Invalid quaternion");
    return {-x / n, -y / n, -z / n, w / n};
}
Quaternion Quaternion::fromTo(Vec a, Vec b) {
    a = a.normalized();
    b = b.normalized();
    if (a.squared() < .1f || b.squared() < .1f)
        return {};
    float dot = Vec::dot(a, b);
    if (dot < -.999999f) {
        Vec axis = Vec::cross(a, {1, 0, 0});
        if (axis.squared() < 1e-6f)
            axis = Vec::cross(a, {0, 1, 0});
        axis = axis.normalized();
        return {axis.x, axis.y, axis.z, 0};
    }
    Vec cross = Vec::cross(a, b);
    return normalize({cross.x, cross.y, cross.z, 1 + dot});
}
Quaternion Quaternion::slerp(Quaternion a, Quaternion b, float t) {
    t = unit(t);
    float dot = a.x * b.x + a.y * b.y + a.z * b.z + a.w * b.w;
    if (dot < 0) {
        dot = -dot;
        b = {-b.x, -b.y, -b.z, -b.w};
    }
    if (dot > .9995f)
        return normalize({a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t, a.z + (b.z - a.z) * t,
                          a.w + (b.w - a.w) * t});
    float theta = std::acos(std::max(-1.f, std::min(1.f, dot))), denom = std::sin(theta),
          first = std::sin((1 - t) * theta) / denom, second = std::sin(t * theta) / denom;
    return {a.x * first + b.x * second, a.y * first + b.y * second, a.z * first + b.z * second,
            a.w * first + b.w * second};
}
void Archetype::apply(World& w, int a, int variantIndex) const {
    w.joint(a, 0);
    if (uint8_t(anatomy) > 1 || uint8_t(motion) > 15 || uint8_t(equipment) > 5 ||
        uint8_t(variant) > 5 || variantEvery < 0 || variantEvery > 32)
        throw std::invalid_argument("Invalid archetype vocabulary");
    for (float value : {mass, strength, health, speed, attackInterval, damage, impulse})
        if (!std::isfinite(value) || value < 0 || value > 1e6f)
            throw std::invalid_argument("Invalid archetype tuning");
    w.anatomy[a] = uint8_t(anatomy);
    w.setFamily(a, motion);
    w.strength[a] = std::max(.2f, std::min(2.f, strength));
    w.health[a] = std::max(1.f, health);
    Equipment item = variantEvery > 0 && variantIndex % variantEvery == 0 ? variant : equipment;
    w.visualArchetype[a] = item == Equipment::Baton   ? 5
                           : item == Equipment::Rifle ? 6
                           : item == Equipment::Flag  ? 8
                           : item == Equipment::Stone ? 9
                           : item == Equipment::Sword ? 10
                                                      : 0;
    w.role[a] =
        uint8_t(item == Equipment::Stone || item == Equipment::Flag || item == Equipment::Rifle);
    if (anatomy == Anatomy::Human && w.supportLegLoss(a))
        w.crawl(a);
}
Crowd::Crowd(World& w)
    : world_(w), cooldown_(size_t(w.count())), blood(2048), injuries(size_t(w.count())),
      navigation(64, 1.1f) {
    if (w.melee->onHit)
        throw std::invalid_argument(
            "Crowd requires its own combat notification slot; do not combine with RTS controller");
    w.melee->onHit = [this](int, int victim) {
        if (enableBlood)
            blood.emit(world_.melee->lastHitPoint, {0, 1, 0}, world_.melee->lastHitDamage,
                       uint32_t(world_.frame + victim));
    };
    projectiles.onImpact = [this](int actor, int, Vec at, float damage) {
        if (enableBlood)
            blood.emit(at, {0, 1, 0}, damage, uint32_t(world_.frame + actor));
    };
    refreshNavigation();
}
Crowd::~Crowd() {
    world_.melee->onHit = {};
}
void Crowd::refreshNavigation(const std::function<bool(Vec, Vec)>& overlap) {
    navigation.bake(overlap);
    navigation.rebuild(teamGoalA, teamGoalB);
}
void Crowd::updateBrains() {
    float interval = 1 / std::max(1.f, brainHz);
    for (int a = 0; a < world_.count(); a++) {
        auto& w = world_;
        if (w.state[a] != uint8_t(State::Active))
            continue;
        cooldown_[a] -= interval;
        int b = a ^ 1;
        if (b >= w.count())
            b = a > 0 ? a - 1 : 0;
        if (b == a || w.faction[a] == w.faction[b] || w.health[b] <= 0)
            continue;
        Vec me = w.joint(a, 2), target = w.joint(b, 2), delta = target - me;
        float distance = delta.length();
        bool rifle = w.visualArchetype[a] == 6,
             throwing = !rifle && w.role[a] == 1 && distance < 15 && distance > 2;
        float meleeReach = w.visualArchetype[a] == 10  ? 1.8f
                           : w.visualArchetype[a] == 5 ? 1.5f
                                                       : 1.15f,
              stop = rifle      ? 9.f
                     : throwing ? 9.f
                                : meleeReach * .8f;
        if (distance > stop) {
            Vec direction = useNavigation
                                ? navigation.direction(w.joint(a, 0), w.faction[a] == 0 ? 1 : 0)
                                : Vec{delta.x, 0, delta.z}.normalized();
            if (direction.squared() < .001f)
                direction = Vec{delta.x, 0, delta.z}.normalized();
            w.locomotion(a, direction, archetype ? archetype->speed : throwing ? .42f : .7f);
        } else
            w.locomotion(a, {}, 0);
        if (cooldown_[a] > 0)
            continue;
        if (rifle && distance < 20) {
            w.setAction(a, Action::Guard);
            projectiles.fire(w, w.joint(a, 9), target - w.joint(a, 9), 350,
                             archetype ? archetype->impulse : 12.f, .015f, a,
                             ProjectileKind::Bullet, 28);
            cooldown_[a] = std::max(.18f, archetype ? archetype->attackInterval * .35f : .35f);
        } else if (throwing && distance < 15) {
            Vec origin = w.joint(a, 9), aim = target - origin + Vec{0, distance * .13f, 0};
            w.beginAttack(a, Action::Throw);
            projectiles.fire(w, origin, aim, 26, archetype ? archetype->impulse : 65.f, .16f, a);
            cooldown_[a] = (archetype ? archetype->attackInterval : 2) + (a % 7) * .18f;
        } else if (!throwing && distance < meleeReach &&
                   w.melee->begin(a, b, archetype ? archetype->damage : 5.f + (a % 7),
                                  archetype ? archetype->impulse : 32.f + (a % 9) * 4.f))
            cooldown_[a] =
                std::max(.85f, archetype ? archetype->attackInterval : .75f) + (a % 11) * .037f;
    }
}
void Crowd::advance(float dt) {
    if (!simulate)
        return;
    if (!std::isfinite(dt) || dt < 0 || !std::isfinite(brainHz) || brainHz <= 0)
        throw std::invalid_argument("Invalid crowd time");
    auto start = std::chrono::steady_clock::now();
    brainClock_ += dt;
    float interval = 1 / std::max(1.f, brainHz);
    if (ai && brainClock_ >= interval) {
        brainClock_ = std::fmod(brainClock_, interval);
        updateBrains();
    }
    accumulator_ += dt;
    int steps = 0;
    while (accumulator_ >= Step && steps < 6) {
        world_.tick();
        injuries.tick(world_, tearLimbs, tearRatio);
        projectiles.tick(world_, Step);
        if (enableBlood)
            blood.tick(Step);
        accumulator_ -= Step;
        steps++;
    }
    if (accumulator_ >= Step) {
        droppedSeconds += accumulator_ - Step;
        accumulator_ = Step;
    }
    lastSimulationMs =
        std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
}
void Crowd::volley(Vec origin) {
    if (!origin.finite())
        throw std::invalid_argument("Invalid volley origin");
    for (int n = 0; n < 64; n++) {
        int a = (n * 79) % world_.count();
        Vec target = world_.joint(a, n % Joints),
            jitter{((n * 13) % 9 - 4) * .04f, ((n * 7) % 9 - 4) * .03f, 0},
            delta = target - origin + jitter;
        float travel = delta.length() / 26;
        delta.y += .5f * 9.81f * travel * travel;
        projectiles.fire(world_, origin, delta, 26, 65, .16f);
    }
}
namespace {
constexpr int driverChild[] = {1, 2, 3, 2, 5, 6, 5, 8, 9, 8, 11, 12, 13, 12, 15, 16, 17, 16};
}
void RigBinding::capture(const std::array<Vec, Joints>& positions,
                         const std::array<Quaternion, Joints>& rotations, Vec rootPosition,
                         Quaternion rootRotation, const DataBank& bank) {
    auto inverse = rootRotation.inverse();
    if (!rootPosition.finite())
        throw std::invalid_argument("Invalid rig root");
    hipOffset_ = inverse.rotate(positions[0] - rootPosition);
    parent_ = bank.parents;
    for (int i = 0; i < Joints; i++) {
        if (!positions[i].finite())
            throw std::invalid_argument("Invalid rig joint");
        restRotation_[i] = inverse * rotations[i];
        current_[i] = rotations[i];
        int child = driverChild[i];
        Vec direction = parent_[child] == i ? positions[child] - positions[i]
                                            : positions[i] - positions[std::max(0, parent_[i])];
        restDirection_[i] =
            inverse.rotate(direction.squared() > .00001f ? direction.normalized() : Vec{0, 0, 1});
    }
    ready_ = true;
}
RigPose RigBinding::apply(const World& w, int a, float dt, float smoothing) {
    if (!ready_ || !std::isfinite(dt) || dt < 0 || !std::isfinite(smoothing) || smoothing < 0)
        throw std::invalid_argument("Rig bind pose/time required");
    RigPose pose;
    auto heading = w.heading(a);
    pose.rootRotation = heading;
    pose.rootPosition = w.joint(a, 0) - heading.rotate(hipOffset_);
    float blend = smoothing <= 0 ? 1.f : 1 - std::exp(-smoothing * dt);
    for (int i = 0; i < Joints; i++) {
        int child = driverChild[i];
        Vec desired = parent_[child] == i ? w.joint(a, child) - w.joint(a, i)
                                          : w.joint(a, i) - w.joint(a, std::max(0, parent_[i]));
        if (desired.squared() >= .00001f) {
            Quaternion base = heading * restRotation_[i],
                       delta = Quaternion::fromTo(heading.rotate(restDirection_[i]),
                                                  desired.normalized());
            current_[i] = Quaternion::slerp(current_[i], delta * base, blend);
        }
        pose.bones[i] = current_[i];
    }
    return pose;
}
size_t writeEquipment(const World& w, EquipmentInstance* out, size_t capacity) {
    if (!out && capacity)
        throw std::invalid_argument("Null equipment output");
    size_t used = 0;
    for (int a = 0; a < w.count(); a++) {
        uint8_t tag = w.visualArchetype[a];
        Equipment item = tag == 5    ? Equipment::Baton
                         : tag == 6  ? Equipment::Rifle
                         : tag == 8  ? Equipment::Flag
                         : tag == 9  ? Equipment::Stone
                         : tag == 10 ? Equipment::Sword
                                     : Equipment::None;
        if (item == Equipment::None || w.detached[size_t(a) * Joints + 9])
            continue;
        if (used == capacity)
            return used;
        Vec hand = w.joint(a, 9), arm = (hand - w.joint(a, 8)).normalized();
        out[used++] = {item, hand, Quaternion::fromTo({0, 1, 0}, arm), a};
    }
    return used;
}
} // namespace out::motor
