// SPDX-License-Identifier: LicenseRef-OUT-Source-Review-1.0
// Copyright (c) 2026 Алексей Савельев (Alex Merqury). See ../../../LICENSE.
#include "out_motor.hpp"
#include <algorithm>
#include <limits>

namespace out::motor {
namespace {
float clamp(float v, float a, float b) {
    return std::max(a, std::min(b, v));
}
float smooth(float x) {
    x = clamp(x, 0, 1);
    return x * x * (3 - 2 * x);
}
int countCheck(int n) {
    if (n < 1 || n > 65536)
        throw std::invalid_argument("Motor capacity must be 1..65536");
    return n;
}
std::shared_ptr<const DataBank> copyBank(const std::shared_ptr<const DataBank>& b) {
    if (!b)
        throw std::invalid_argument("Licensed bank required");
    auto copy = std::make_shared<DataBank>(*b);
    copy->validate();
    return copy;
}
Quaternion turn(Quaternion from, Vec direction, float degrees) {
    if (direction.squared() < .0001f)
        return from;
    Vec f = from.rotate({0, 0, 1});
    float a = std::atan2(f.x, f.z) * 57.295779513f,
          b = std::atan2(direction.x, direction.z) * 57.295779513f,
          delta = std::remainder(b - a, 360.f);
    return Quaternion::yaw(a + clamp(delta, -degrees, degrees));
}
constexpr int recoveryPose[] = {10, 8, 12, 13, 14, 15, 16, 0};
constexpr float recoveryTime[] = {.8f, .55f, .7f, .65f, .7f, .62f, .88f, .7f};
uint8_t bin(float x, float a, float b, float c, uint8_t previous) {
    uint8_t target = uint8_t(x > c ? 3 : x > b ? 2 : x > a ? 1 : 0);
    if (target == previous)
        return target;
    float threshold = target > previous ? (previous == 0   ? a
                                           : previous == 1 ? b
                                                           : c)
                                        : (target == 0   ? a
                                           : target == 1 ? b
                                                         : c),
          margin = std::abs(threshold) * .11f + .013f;
    if (target > previous && x < threshold + margin)
        return previous;
    if (target < previous && x > threshold - margin)
        return previous;
    return target;
}
} // namespace
void Settings::validate() const {
    if (iterations < 1 || iterations > 16 || motorHz < 1 || motorHz > 120 || workers < 1 ||
        workers > 64 || maximumEvents < 1 || maximumEvents > 1048576)
        throw std::invalid_argument("Invalid Motor scheduler settings");
    for (float v : {muscle, damping, support, air, arenaHalfSize})
        if (!std::isfinite(v) || v < 0)
            throw std::invalid_argument("Invalid Motor tuning");
    if (muscle > 3 || damping > 5 || support > 1 || air > 2 || arenaHalfSize < 1 ||
        arenaHalfSize > 1024)
        throw std::invalid_argument("Motor tuning outside bounds");
}
World::World(std::shared_ptr<const DataBank> data, int count, Settings config, size_t eventCapacity)
    : data_(copyBank(data)), cache_(std::make_shared<MotionCache>(data_)), events_(eventCapacity),
      feet_(size_t(countCheck(count)), data_), capacity(count), settings(config) {
    settings.validate();
    size_t n = size_t(capacity), points = n * Joints;
    generation_.resize(n);
    anchor_.resize(n);
    intent_.resize(n);
    start_.resize(points);
    rotation_.resize(n);
    elapsed_.resize(n);
    phase_.resize(n);
    gaitPhase_.resize(n);
    gaitRate_.resize(n, 1);
    attack_.resize(n);
    motorGain_.resize(n);
    motorDamp_.resize(n);
    inverseMass_.resize(points);
    contactCount_.resize(n);
    cell_.resize(n);
    pending_.resize(n);
    dwell_.resize(n);
    errorBin_.resize(n);
    speedBin_.resize(n);
    family_.resize(n, 255);
    recoveryStage_.resize(n);
    lastAction_.resize(n, uint8_t(Action::Walk));
    locomotionBlend_.resize(n);
    hurt_.resize(n);
    stumble_.resize(n);
    footActive_.resize(n, 1);
    position.resize(points);
    velocity.resize(points);
    goal.resize(points);
    goalVelocity.resize(points);
    detached.resize(points);
    faction.resize(n);
    state.resize(n);
    anatomy.resize(n);
    action.resize(n);
    visualArchetype.resize(n);
    role.resize(n);
    health.resize(n);
    mass.resize(n);
    strength.resize(n);
    float total = 0;
    for (float m : data_->relativeMass)
        total += m;
    for (int j = 0; j < Joints; j++)
        baseMass_[j] = data_->relativeMass[j] * 70 / total;
    for (int k = 0; k < Links; k++)
        length_[k] =
            Vec::distance(data_->pose(0, DataBank::edgeA[k]), data_->pose(0, DataBank::edgeB[k]));
    bodyWidth_ = int(std::ceil(settings.arenaHalfSize));
    bodyOffsets_.resize(size_t(bodyWidth_) * bodyWidth_ + 1);
    bodyCursor_.resize(size_t(bodyWidth_) * bodyWidth_);
    bodyOrder_.resize(n);
    int columns = std::max(1, int(std::ceil(std::sqrt((capacity + 1) / 2.f)))),
        rows = std::max(1, int(std::ceil((capacity + 1) / 2.f / columns)));
    for (int a = 0; a < capacity; a++) {
        float side = (a & 1) ? 1.f : -1.f;
        int k = a >> 1;
        spawn(a,
              {side * (1.5f + (k % columns) * 1.03f), 0, ((k / columns) - (rows - 1) * .5f) * 1.1f},
              side < 0 ? 90.f : -90.f, 52.f + (a * 29 % 55), a % 2);
    }
    melee = std::make_unique<Melee>(*this);
    solver_ = out_motor_create(capacity, baseMass_.data(), length_.data(),
                               reinterpret_cast<const float*>(data_->field.data()));
    if (!solver_)
        throw std::runtime_error("Cannot allocate native Motor context");
}
World::~World() {
    if (solver_)
        out_motor_destroy(solver_);
}
void World::checkActor(int a) const {
    if (a < 0 || a >= capacity)
        throw std::out_of_range("Motor actor index");
}
Vec World::joint(int a, int j) const {
    checkActor(a);
    if (j < 0 || j >= Joints)
        throw std::out_of_range("Motor joint");
    return position[size_t(a) * Joints + j];
}
Quaternion World::heading(int a) const {
    checkActor(a);
    return rotation_[a];
}
Vec World::anchor(int a) const {
    checkActor(a);
    return anchor_[a];
}
uint32_t World::generation(int a) const {
    checkActor(a);
    return generation_[a];
}
void World::spawn(int a, Vec origin, float yaw, float kg, int team) {
    checkActor(a);
    if (!origin.finite() || !std::isfinite(yaw) || !std::isfinite(kg))
        throw std::invalid_argument("Nonfinite spawn");
    if (generation_[a] == UINT32_MAX)
        throw std::overflow_error("Actor generation exhausted");
    generation_[a]++;
    family_[a] = 255;
    visualArchetype[a] = 0;
    role[a] = uint8_t(a % 5 == 0);
    anatomy[a] = uint8_t(Anatomy::Human);
    anchor_[a] = origin;
    rotation_[a] = Quaternion::yaw(yaw);
    faction[a] = uint8_t(team & 1);
    action[a] = uint8_t(Action::Idle);
    state[a] = uint8_t(State::Active);
    elapsed_[a] = 0;
    phase_[a] = std::fmod(a * .618034f, 1.f);
    motorGain_[a] = .6f;
    motorDamp_[a] = 1;
    health[a] = 100;
    strength[a] = 1;
    mass[a] = clamp(kg, 25, 180);
    gaitPhase_[a] = 0;
    gaitRate_[a] = 1;
    locomotionBlend_[a] = 0;
    attack_[a] = hurt_[a] = stumble_[a] = 0;
    footActive_[a] = 1;
    intent_[a] = {};
    contactCount_[a] = cell_[a] = pending_[a] = dwell_[a] = errorBin_[a] = speedBin_[a] =
        recoveryStage_[a] = 0;
    for (int j = 0; j < Joints; j++) {
        int i = a * Joints + j;
        inverseMass_[i] = 70 / (mass[a] * baseMass_[j]);
        position[i] = origin + rotation_[a].rotate(data_->pose(0, j));
        start_[i] = goal[i] = position[i];
        velocity[i] = goalVelocity[i] = {};
        detached[i] = 0;
    }
    feet_.reset(a, position.data() + a * Joints, rotation_[a]);
    if (melee) {
        melee->cancel(a);
        melee->cancelTarget(a);
    }
}
bool World::queue(EventKind kind, int a, int j, Vec impulse, float damage) {
    if (a < 0 || a >= capacity || j < 0 || j >= Joints || uint8_t(kind) > 2 || !impulse.finite() ||
        !std::isfinite(damage) || damage < 0 || impulse.length() > 1e6f || damage > 1e6f ||
        !events_.push({kind, a, j, impulse, damage, a >= 0 && a < capacity ? generation_[a] : 0})) {
        rejectedEvents++;
        return false;
    }
    return true;
}
void World::impulse(int a, int j, Vec force) {
    int i = a * Joints + j;
    joint(a, j);
    if (!force.finite() || force.length() > 1e6f)
        throw std::invalid_argument("Invalid impulse");
    velocity[i] += force * inverseMass_[i];
    hurt_[a] = .9f;
    if (force.squared() > 1800 && state[a] == uint8_t(State::Active))
        fall(a);
    else if (force.squared() > 400 && state[a] == uint8_t(State::Active) && stumbleClip_) {
        stumble_[a] = .6f;
        attack_[a] = 0;
        intent_[a] = {};
        action[a] = uint8_t(Action::Idle);
        if (melee)
            melee->cancel(a);
    }
}
void World::damage(int a, int j, Vec force, float amount) {
    checkActor(a);
    if (!std::isfinite(amount) || amount < 0 || amount > 1e6f)
        throw std::invalid_argument("Invalid damage");
    if (state[a] == uint8_t(State::Dead))
        return;
    impulse(a, j, force);
    health[a] = std::max(0.f, health[a] - amount);
    if (health[a] <= 0) {
        state[a] = uint8_t(State::Dead);
        strength[a] = 0;
        melee->cancel(a);
    } else if (amount > 32 && (j == 5 || j == 8 || j == 11 || j == 15))
        sever(a, j);
}
bool World::supportLegLoss(int a) const {
    checkActor(a);
    return detached[size_t(a) * Joints + 11] || detached[size_t(a) * Joints + 15];
}
void World::sever(int a, int j) {
    joint(a, j);
    int group = j == 5 || j == 6                ? 1
                : j == 8 || j == 9              ? 2
                : j == 11 || j == 12 || j == 13 ? 3
                : j == 15 || j == 16 || j == 17 ? 4
                                                : 0;
    if (!group)
        return;
    int first = group == 1   ? 5
                : group == 2 ? 8
                : group == 3 ? 11
                             : 15,
        last = group == 1   ? 6
               : group == 2 ? 9
               : group == 3 ? 13
                            : 17;
    if (detached[size_t(a) * Joints + first])
        return;
    for (int k = first; k <= last; k++)
        detached[size_t(a) * Joints + k] = uint8_t(group);
    severed++;
    if (group >= 3 && anatomy[a] == uint8_t(Anatomy::Human))
        crawl(a);
}
void World::crawl(int a) {
    checkActor(a);
    if (state[a] == uint8_t(State::Dead) || state[a] == uint8_t(State::Crawling))
        return;
    state[a] = uint8_t(State::Crawling);
    strength[a] = .6f;
    anchor_[a] = position[size_t(a) * Joints];
    anchor_[a].y = 0;
    elapsed_[a] = 0;
    intent_[a] = rotation_[a].rotate({0, 0, .12f});
    action[a] = uint8_t(Action::Idle);
    for (int j = 0; j < Joints; j++)
        start_[size_t(a) * Joints + j] = goal[size_t(a) * Joints + j] =
            position[size_t(a) * Joints + j];
    if (melee)
        melee->cancel(a);
}
void World::fall(int a) {
    checkActor(a);
    if (state[a] == uint8_t(State::Dead))
        return;
    state[a] = uint8_t(State::Falling);
    elapsed_[a] = attack_[a] = hurt_[a] = stumble_[a] = 0;
    footActive_[a] = 1;
    intent_[a] = {};
    action[a] = uint8_t(Action::Idle);
    if (melee)
        melee->cancel(a);
}
void World::recover(int a) {
    checkActor(a);
    if (anatomy[a] == uint8_t(Anatomy::Human) && supportLegLoss(a)) {
        crawl(a);
        return;
    }
    if (state[a] == uint8_t(State::Dead))
        return;
    state[a] = uint8_t(State::Recovering);
    elapsed_[a] = 0;
    recoveryStage_[a] =
        Vec::dot(rotation_[a].rotate({0, 0, 1}), (joint(a, 2) - joint(a, 0)).normalized()) > .15f
            ? 0
            : 1;
    for (int j = 0; j < Joints; j++)
        start_[size_t(a) * Joints + j] = goal[size_t(a) * Joints + j] =
            position[size_t(a) * Joints + j];
    anchor_[a] = joint(a, 0);
    anchor_[a].y = 0;
}
void World::locomotion(int a, Vec direction, float speed) {
    checkActor(a);
    if (!direction.finite() || !std::isfinite(speed) || speed < 0 || speed > 10)
        throw std::invalid_argument("Invalid locomotion");
    if (anatomy[a] == uint8_t(Anatomy::Human) && supportLegLoss(a)) {
        crawl(a);
        speed = std::min(speed, .2f);
    }
    if (attacking(a) || stumbling(a)) {
        intent_[a] = {};
        return;
    }
    direction.y = 0;
    intent_[a] = direction.normalized() * speed;
    if (state[a] == uint8_t(State::Active)) {
        action[a] = uint8_t(intent_[a].squared() > .0001f ? (speed > 1 ? Action::Run : Action::Walk)
                                                          : Action::Idle);
        if (action[a] != uint8_t(Action::Idle))
            lastAction_[a] = action[a];
    }
}
void World::setAction(int a, Action value) {
    checkActor(a);
    if (uint8_t(value) > 7)
        throw std::invalid_argument("Motor action");
    action[a] = uint8_t(value);
}
void World::setFamily(int a, Family f) {
    checkActor(a);
    if (uint8_t(f) > 15)
        throw std::invalid_argument("Motion family");
    family_[a] = uint8_t(f);
}
void World::clearFamily(int a) {
    checkActor(a);
    family_[a] = 255;
}
void World::setClip(AuthoredClip clip) {
    clip.validate();
    auto shared = std::make_shared<const AuthoredClip>(std::move(clip));
    if (shared->name == "Stumble")
        stumbleClip_ = shared;
    else if (shared->name == "Sword_Attack")
        swordAttack_ = shared;
    else if (shared->name == "Sword_Idle")
        swordIdle_ = shared;
    else
        authored_[uint8_t(shared->family)] = shared;
}
bool World::attacking(int a) const {
    checkActor(a);
    return attack_[a] > 0 &&
           (action[a] == uint8_t(Action::Strike) || action[a] == uint8_t(Action::Throw));
}
bool World::stumbling(int a) const {
    checkActor(a);
    return stumble_[a] > 0 && state[a] == uint8_t(State::Active);
}
float World::attackProgress(int a) const {
    checkActor(a);
    return clamp(1 - attack_[a] / .85f, 0, 1);
}
void World::beginAttack(int a, Action act) {
    checkActor(a);
    if (act != Action::Strike && act != Action::Throw)
        throw std::invalid_argument("Attack action required");
    if (state[a] != uint8_t(State::Active) || health[a] <= 0)
        return;
    action[a] = uint8_t(act);
    attack_[a] = .85f;
    intent_[a] = {};
}
void World::dropAll(Vec v) {
    for (int a = 0; a < capacity; a++) {
        fall(a);
        impulse(a, 2, v);
    }
}
void World::updateState(int a) {
    elapsed_[a] += Step;
    hurt_[a] = std::max(0.f, hurt_[a] - Step);
    stumble_[a] = std::max(0.f, stumble_[a] - Step);
    attack_[a] = std::max(0.f, attack_[a] - Step);
    State s = State(state[a]);
    if (s == State::Falling && settings.autoRecovery && elapsed_[a] > 1.35f && contactCount_[a])
        recover(a);
    else if (s == State::Recovering) {
        int stage = recoveryStage_[a];
        float duration = recoveryTime[stage], pelvis = joint(a, 0).y;
        bool gate = stage < 4    ? contactCount_[a] > 0
                    : stage < 6  ? pelvis > .28f
                    : stage == 6 ? pelvis > .57f
                                 : pelvis > .68f;
        if (elapsed_[a] > duration && (gate || elapsed_[a] > duration + 1.1f)) {
            if (stage >= 7) {
                state[a] = uint8_t(pelvis > .65f ? State::Active : State::Falling);
                if (pelvis > .65f) {
                    recoveries++;
                    action[a] = uint8_t(Action::Idle);
                }
            } else {
                for (int j = 0; j < Joints; j++)
                    start_[size_t(a) * Joints + j] = goal[size_t(a) * Joints + j];
                recoveryStage_[a]++;
            }
            elapsed_[a] = 0;
        }
    } else if (s == State::Active && joint(a, 2).y < .45f && elapsed_[a] > .8f)
        fall(a);
    if (!attack_[a] &&
        (action[a] == uint8_t(Action::Strike) || action[a] == uint8_t(Action::Throw)))
        action[a] = uint8_t(Action::Idle);
}
void World::updateTarget(int a, float dt) {
    targetUpdates++;
    int offset = a * Joints;
    State s = State(state[a]);
    if (s != State::Active)
        footActive_[a] = 0;
    if (s == State::Dead || s == State::Passive)
        return;
    if (s == State::Falling) {
        for (int j = 0; j < Joints; j++) {
            goal[offset + j] = position[offset + j];
            goalVelocity[offset + j] = {};
        }
        float t = seconds * 6 + a;
        for (int n = 0; n < 2; n++) {
            Vec chest = position[offset + 2], pelvis = position[offset];
            goal[offset + (n ? 9 : 6)] =
                chest +
                Vec{(n ? .29f : -.29f) + std::sin(t + n * 2) * .12f,
                    .23f + std::sin(t * 1.13f + n * 2) * .18f, std::cos(t * .93f + n * 3) * .12f} +
                (chest - pelvis).normalized() * .12f;
        }
        return;
    }
    Action act = Action(action[a]), poseAction = act == Action::Idle && locomotionBlend_[a] > 0
                                                     ? Action(lastAction_[a])
                                                     : act;
    Family family = poseAction == Action::Walk                                   ? Family::Walk
                    : poseAction == Action::Run                                  ? Family::Run
                    : poseAction == Action::Crouch                               ? Family::Crouch
                    : poseAction == Action::Guard || poseAction == Action::Reach ? Family::Guard
                    : poseAction == Action::Strike                               ? Family::Strike
                    : poseAction == Action::Throw                                ? Family::Throw
                                                                                 : Family::Idle;
    if (poseAction != Action::Idle && poseAction != Action::Run && poseAction != Action::Crouch &&
        poseAction != Action::Guard && poseAction != Action::Reach &&
        poseAction != Action::Strike && poseAction != Action::Throw && family_[a] != 255)
        family = Family(family_[a]);
    int clip = MotionCache::select(family, uint32_t(a) + 1);
    const auto& motion = cache_->clip(clip);
    Family authoredFamily =
        poseAction == Action::Walk
            ? (family_[a] == uint8_t(Family::Drunk) && authored_[uint8_t(Family::Drunk)]
                   ? Family::Drunk
               : anatomy[a] == uint8_t(Anatomy::Zombie) ? Family::Zombie
                                                        : Family::Walk)
        : poseAction == Action::Run                                  ? Family::Run
        : poseAction == Action::Strike                               ? Family::Strike
        : poseAction == Action::Throw                                ? Family::Throw
        : poseAction == Action::Guard || poseAction == Action::Reach ? Family::Guard
        : poseAction == Action::Crouch                               ? Family::Crouch
                                                                     : Family::Idle;
    auto authored = authored_[uint8_t(authoredFamily)];
    bool weapon = visualArchetype[a] == 5 || visualArchetype[a] == 10;
    if (weapon && act == Action::Strike && swordAttack_)
        authored = swordAttack_;
    if (weapon && (act == Action::Idle || act == Action::Guard) && swordIdle_)
        authored = swordIdle_;
    bool gait = poseAction == Action::Walk || poseAction == Action::Run,
         locomotion = act == Action::Walk || act == Action::Run;
    float duration = authored ? authored->duration : motion.duration;
    float time = gait                                              ? gaitPhase_[a] + phase_[a]
                 : (authored ? authored->looping : motion.looping) ? seconds / duration + phase_[a]
                 : attacking(a)                                    ? attackProgress(a)
                                                                   : 0;
    if (gait)
        gaitRate_[a] = clamp(intent_[a].length() /
                                 std::max(.1f, authored ? authored->nominalSpeed : motion.speed),
                             .1f, 2) /
                       std::max(.1f, duration);
    std::array<Vec, Joints> previous{};
    for (int j = 0; j < Joints; j++) {
        int i = offset + j;
        previous[j] = goal[i];
        Vec local;
        if (s == State::Crawling) {
            float cycle = seconds * 3 + phase_[a] * 6.2831853f;
            local = data_->blend(8, 13, .15f, j);
            if (j == 5 || j == 6 || j == 8 || j == 9)
                local += Vec{0, .025f * std::abs(std::sin(cycle)),
                             .055f * std::sin(cycle + (j < 7 ? 0 : 3.14159265f))};
        } else if (s == State::Recovering) {
            int stage = recoveryStage_[a],
                pose = stage == 0 ? ((a & 1) ? 11 : 10) : recoveryPose[stage];
            local = data_->pose(pose, j);
        } else {
            local =
                authored ? authored->sample(time * duration, j) : cache_->normalized(clip, time, j);
            if (gait) {
                auto idle = authored_[uint8_t(Family::Idle)];
                local = Vec::lerp(idle ? idle->sample(seconds, j) : data_->pose(0, j), local,
                                  locomotionBlend_[a]);
            }
            if (gait && weapon && swordIdle_ && j >= 4 && j <= 9)
                local = Vec::lerp(local, swordIdle_->sample(seconds, j), .85f);
            if (!authored && act == Action::Strike && (j == 8 || j == 9)) {
                float attack = attackProgress(a);
                Vec windup = j == 9 ? Vec{.39f, 1.52f, -.12f} : Vec{.48f, 1.38f, -.12f},
                    contact = j == 9 ? Vec{.20f, 1.12f, .58f} : Vec{.42f, 1.25f, .26f};
                local = Vec::lerp(windup, contact, smooth((attack - .22f) / .32f));
                if (attack < .22f)
                    local = Vec::lerp(data_->pose(0, j), windup, attack / .22f);
                if (attack > .64f)
                    local = Vec::lerp(contact, data_->pose(0, j), (attack - .64f) / .36f);
            }
        }
        if (s == State::Active && hurt_[a] > 0 && authored_[uint8_t(Family::Hit)]) {
            auto hit = authored_[uint8_t(Family::Hit)];
            local = Vec::lerp(local, hit->sample((1 - hurt_[a] / .9f) * hit->duration, j),
                              std::min(1.f, hurt_[a] * .4f));
        }
        if (s == State::Active && stumble_[a] > 0 && stumbleClip_)
            local = Vec::lerp(
                local, stumbleClip_->sample((1 - stumble_[a] / .6f) * stumbleClip_->duration, j),
                std::sin(3.14159265f * (1 - stumble_[a] / .6f)) * .85f);
        Vec target = anchor_[a] + rotation_[a].rotate(local);
        if (s == State::Recovering)
            target =
                Vec::lerp(start_[i], target, smooth(elapsed_[a] / recoveryTime[recoveryStage_[a]]));
        else if (s == State::Crawling)
            target = Vec::lerp(start_[i], target, smooth(elapsed_[a] / .35f));
        if (act == Action::Reach && (j == 6 || j == 9))
            target += rotation_[a].rotate(
                {j == 6 ? -.08f : .08f, .14f, .52f * clamp(attack_[a] + .4f, 0, 1)});
        goalVelocity[i] = (target - goal[i]) / std::max(.008f, dt);
        goal[i] = target;
    }
    if (s == State::Active && stumble_[a] <= 0) {
        if (!footActive_[a]) {
            feet_.reset(a, position.data() + offset, rotation_[a]);
            footActive_[a] = 1;
        }
        uint8_t contacts = locomotion ? (authored ? authored->contactMask(time * duration)
                                                  : cache_->contacts(clip, time))
                                      : 3;
        feet_.apply(a, dt, time, contacts, duration, locomotion ? intent_[a] : Vec{}, rotation_[a],
                    anchor_[a], goal.data() + offset, goalVelocity.data() + offset,
                    detached.data() + offset, act == Action::Run, bool(authored));
        for (int j = 0; j < Joints; j++)
            if (!detached[offset + j]) {
                Vec delta = goal[offset + j] - previous[j];
                if (delta.squared() <= .004f * .004f) {
                    goal[offset + j] = previous[j];
                    goalVelocity[offset + j] = {};
                } else
                    goalVelocity[offset + j] = delta / std::max(.008f, dt);
            }
    } else if (stumble_[a] > 0)
        footActive_[a] = 0;
}
void World::updateResponse(int a) {
    int o = a * Joints;
    float error = 0, speed = 0;
    for (int j : {0, 2, 6, 9}) {
        error += (position[o + j] - goal[o + j]).length();
        auto v = velocity[o + j];
        speed += std::abs(v.x) + std::abs(v.y) + std::abs(v.z);
    }
    error *= .25f;
    speed /= 12;
    uint8_t eb = bin(error, .08f, .25f, .58f, errorBin_[a]),
            sb = bin(speed, .4f, 1.7f, 4.3f, speedBin_[a]);
    errorBin_[a] = eb;
    speedBin_[a] = sb;
    int count = contactCount_[a],
        cb = count > 5   ? 3
             : count > 2 ? 2
             : count > 0 ? 1
                         : 0,
        mode = state[a] == uint8_t(State::Falling)      ? 2
               : state[a] == uint8_t(State::Recovering) ? 3
               : attack_[a] > 0                         ? 1
                                                        : 0;
    uint8_t cell = uint8_t((mode << 6) | (eb << 4) | (sb << 2) | cb);
    if (pending_[a] == cell)
        dwell_[a] = uint8_t(std::min(255, int(dwell_[a]) + 1));
    else {
        pending_[a] = cell;
        dwell_[a] = 0;
    }
    if (dwell_[a] >= 6 || mode == 2)
        cell_[a] = cell;
    int profile =
        data_->responseCode(cell_[a] >> 6, (cell_[a] >> 4) & 3, (cell_[a] >> 2) & 3, cell_[a] & 3);
    float target = data_->gain[profile];
    if (mode == 2)
        target = std::min(target, .29f);
    if (state[a] == uint8_t(State::Dead) || state[a] == uint8_t(State::Passive))
        target = 0;
    motorGain_[a] += clamp(target - motorGain_[a], -.09f, .035f);
    motorDamp_[a] = data_->damping[profile];
}
void World::bodies() {
    std::fill(bodyOffsets_.begin(), bodyOffsets_.end(), 0u);
    auto cell = [&](Vec p) {
        int x = int(
                clamp(std::floor((p.x + settings.arenaHalfSize) / 2), 0, float(bodyWidth_ - 1))),
            z = int(
                clamp(std::floor((p.z + settings.arenaHalfSize) / 2), 0, float(bodyWidth_ - 1)));
        return z * bodyWidth_ + x;
    };
    for (int a = 0; a < capacity; a++)
        if (visualArchetype[a] != 7)
            bodyOffsets_[size_t(cell(joint(a, 0))) + 1]++;
    for (size_t k = 1; k < bodyOffsets_.size(); k++)
        bodyOffsets_[k] += bodyOffsets_[k - 1];
    std::copy(bodyOffsets_.begin(), bodyOffsets_.end() - 1, bodyCursor_.begin());
    for (int a = 0; a < capacity; a++)
        if (visualArchetype[a] != 7)
            bodyOrder_[bodyCursor_[cell(joint(a, 0))]++] = uint32_t(a);
    for (int a = 0; a < capacity; a++)
        if (visualArchetype[a] != 7) {
            Vec root = joint(a, 0);
            int c = cell(root), probes = 0;
            for (int dz = -1; dz <= 1 && probes < 64; dz++)
                for (int dx = -1; dx <= 1 && probes < 64; dx++) {
                    int x = c % bodyWidth_ + dx, z = c / bodyWidth_ + dz;
                    if (x < 0 || z < 0 || x >= bodyWidth_ || z >= bodyWidth_)
                        continue;
                    int at = z * bodyWidth_ + x;
                    for (uint32_t k = bodyOffsets_[at]; k < bodyOffsets_[at + 1] && probes < 64;
                         k++, probes++) {
                        int b = int(bodyOrder_[k]);
                        if (b <= a)
                            continue;
                        Vec other = joint(b, 0), delta{other.x - root.x, 0, other.z - root.z};
                        float squared = delta.squared();
                        if (squared >= .69f * .69f || squared < .000001f)
                            continue;
                        float correction = std::min(.045f, (.69f - std::sqrt(squared)) * .35f),
                              total = mass[a] + mass[b];
                        Vec push = delta.normalized() * correction,
                            shiftA = push * (mass[b] / total), shiftB = push * (mass[a] / total);
                        for (int j = 0; j < 3; j++) {
                            position[size_t(a) * Joints + j] -= shiftA;
                            position[size_t(b) * Joints + j] += shiftB;
                        }
                    }
                }
            bodyProbes += probes;
            if (probes >= 64)
                bodyBudgetReached++;
        }
}
void World::tick() {
    settings.validate();
    bodyProbes = bodyBudgetReached = 0;
    processedEvents = 0;
    Event event;
    while (processedEvents < settings.maximumEvents && events_.pop(event)) {
        processedEvents++;
        if (event.generation != generation_[event.actor])
            continue;
        if (event.kind == EventKind::Impulse)
            impulse(event.actor, event.joint, event.impulse);
        else if (event.kind == EventKind::Damage)
            damage(event.actor, event.joint, event.impulse, event.damage);
        else
            sever(event.actor, event.joint);
    }
    if (frame == INT32_MAX)
        throw std::overflow_error("Motor frame exhausted");
    frame++;
    seconds += Step;
    int every = std::max(1, 120 / settings.motorHz);
    for (int a = 0; a < capacity; a++) {
        updateState(a);
        bool moving = state[a] == uint8_t(State::Active) && intent_[a].squared() > .0001f;
        if (moving) {
            gaitPhase_[a] += Step * gaitRate_[a];
            gaitPhase_[a] -= std::floor(gaitPhase_[a]);
        }
        locomotionBlend_[a] = clamp(locomotionBlend_[a] + (moving ? 1.f : -1.f) * Step * 5, 0, 1);
        if (attacking(a)) {
            int opponent = melee->target(a);
            if (opponent >= 0)
                rotation_[a] = turn(rotation_[a], joint(opponent, 0) - joint(a, 0), 360 * Step);
        }
        if ((state[a] == uint8_t(State::Active) || state[a] == uint8_t(State::Crawling)) &&
            intent_[a].squared() > .002f) {
            Vec delta = joint(a, 0) - anchor_[a];
            delta.y = 0;
            if (delta.squared() < .8f) {
                anchor_[a] += intent_[a] * Step;
                rotation_[a] = turn(rotation_[a], intent_[a], 150 * Step);
            }
        }
        bool settled = state[a] == uint8_t(State::Active) && action[a] == uint8_t(Action::Idle) &&
                       intent_[a].squared() <= .0001f && hurt_[a] <= 0 && attack_[a] <= 0 &&
                       !feet_.unfinished(a);
        int targetEvery = settled ? 12 : 6;
        if ((frame - 1 + a) % targetEvery == 0)
            updateTarget(a, targetEvery * Step);
        if ((frame - 1 + a) % every == 0)
            updateResponse(a);
    }
    OUTMotorNativeBuffers b{reinterpret_cast<float*>(position.data()),
                            reinterpret_cast<float*>(velocity.data()),
                            reinterpret_cast<float*>(goal.data()),
                            reinterpret_cast<float*>(goalVelocity.data()),
                            inverseMass_.data(),
                            detached.data(),
                            contactCount_.data(),
                            reinterpret_cast<float*>(rotation_.data()),
                            motorGain_.data(),
                            motorDamp_.data(),
                            strength.data(),
                            state.data()};
    OUTMotorNativeConfig cfg{settings.muscle,
                             settings.damping,
                             settings.support,
                             settings.air,
                             settings.iterations,
                             settings.jointLimits ? 1 : 0,
                             settings.selfCollisions ? 1 : 0,
                             settings.workers};
    if (!out_motor_step(solver_, &b, &cfg, capacity))
        throw std::runtime_error("Native Motor solver failed");
    if (settings.bodyCollisions && (frame & 1) == 0)
        bodies();
    melee->tick();
}
bool World::valid() const {
    for (size_t i = 0; i < position.size(); i++)
        if (!position[i].finite() || !velocity[i].finite() || !goal[i].finite() ||
            !goalVelocity[i].finite())
            return false;
    for (float h : health)
        if (!std::isfinite(h) || h < 0)
            return false;
    return true;
}
InjuryScheduler::InjuryScheduler(size_t capacity) : flags_(capacity), stress_(capacity * 4) {
    if (!capacity || capacity > 65536)
        throw std::invalid_argument("Injury capacity");
}
void InjuryScheduler::reset() {
    std::fill(flags_.begin(), flags_.end(), uint8_t(0));
    std::fill(stress_.begin(), stress_.end(), uint8_t(0));
    lastFrame_ = -1;
    visited = changed = 0;
}
uint8_t InjuryScheduler::flags(int a) const {
    if (a < 0 || size_t(a) >= flags_.size())
        throw std::out_of_range("Injury actor");
    return flags_[a];
}
void InjuryScheduler::tick(World& w, bool tear, float ratio) {
    if (size_t(w.count()) > flags_.size())
        throw std::invalid_argument("Injury capacity smaller than world");
    visited = changed = 0;
    if (w.frame == lastFrame_)
        return;
    lastFrame_ = w.frame;
    ratio = std::isfinite(ratio) ? std::max(1.5f, ratio) : 1.8f;
    constexpr int limbJoints[] = {5, 8, 11, 15};
    for (int a = w.frame % 12; a < w.count(); a += 12) {
        visited++;
        uint8_t injury = 0;
        for (int limb = 0; limb < 4; limb++) {
            int j = limbJoints[limb], slot = a * 4 + limb;
            if (w.detached[size_t(a) * Joints + j]) {
                injury |= uint8_t(1 << limb);
                stress_[slot] = 0;
            } else {
                Vec upper = w.joint(a, j) - w.joint(a, w.bank().parents[j]),
                    lower = w.joint(a, j + 1) - w.joint(a, j);
                float rest =
                          (w.bank().pose(0, j) - w.bank().pose(0, w.bank().parents[j])).squared(),
                      product = upper.squared() * lower.squared(), dot = Vec::dot(upper, lower);
                bool overstressed = upper.squared() > rest * ratio * ratio ||
                                    (product > 1e-10f && dot < 0 && dot * dot > .9025f * product);
                if (tear && overstressed) {
                    stress_[slot] = uint8_t(std::min(3, int(stress_[slot]) + 1));
                    if (stress_[slot] >= 3 && w.queue(EventKind::Sever, a, j))
                        stress_[slot] = 0;
                } else
                    stress_[slot] = 0;
            }
        }
        if ((injury & 12) && w.anatomy[a] == uint8_t(Anatomy::Human)) {
            injury |= 16;
            if (w.state[a] != uint8_t(State::Dead))
                w.crawl(a);
        }
        uint8_t previous = flags_[a];
        if (previous != injury) {
            flags_[a] = injury;
            changed++;
            int losses = injury & ~previous & 15;
            for (int k = 0; k < 4; k++)
                if ((losses & (1 << k)) && limbLost)
                    limbLost(a, limbJoints[k]);
        }
    }
}
} // namespace out::motor
