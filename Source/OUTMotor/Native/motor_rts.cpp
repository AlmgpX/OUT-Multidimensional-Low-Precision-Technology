// SPDX-License-Identifier: LicenseRef-OUT-Source-Review-1.0
// Copyright (c) 2026 Алексей Савельев (Alex Merqury). See ../../../LICENSE.
#include "out_motor.hpp"
#include <algorithm>
#include <limits>
namespace out::motor {
namespace {
float flat(Vec a, Vec b) {
    a.y = b.y = 0;
    return (a - b).squared();
}
float clamp(float x, float a, float b) {
    return std::max(a, std::min(b, x));
}
const std::array<Vec, 16> headings{{{0, 0, 1},
                                    {.38268343f, 0, .92387953f},
                                    {.70710678f, 0, .70710678f},
                                    {.92387953f, 0, .38268343f},
                                    {1, 0, 0},
                                    {.92387953f, 0, -.38268343f},
                                    {.70710678f, 0, -.70710678f},
                                    {.38268343f, 0, -.92387953f},
                                    {0, 0, -1},
                                    {-.38268343f, 0, -.92387953f},
                                    {-.70710678f, 0, -.70710678f},
                                    {-.92387953f, 0, -.38268343f},
                                    {-1, 0, 0},
                                    {-.92387953f, 0, .38268343f},
                                    {-.70710678f, 0, .70710678f},
                                    {-.38268343f, 0, .92387953f}}};
constexpr float speeds[] = {0, .55f, .85f, 1.35f};
} // namespace
RTS::RTS(World& w, Projectiles& projectiles, int nodes) : world_(w), projectiles_(projectiles) {
    size_t n = size_t(w.count());
    lastDecision_.resize(n);
    gatherTime_.resize(n);
    cooldown_.resize(n);
    probe_.resize(n);
    lastDamager_.resize(n, -1);
    wasAlive_.resize(n);
    reserved_.resize(n);
    roles.resize(n);
    orders.resize(n);
    targetActor.resize(n, -1);
    targetResource.resize(n, -1);
    carried.resize(n);
    carriedKind.resize(n);
    goals.resize(n);
    command.resize(n);
    speedCode.resize(n);
    maximumDecisions = std::max(1, (w.count() + 1) / 2);
    nodes = std::max(1, std::min(16, nodes));
    resourceKinds.resize(nodes);
    resourcePositions.resize(nodes);
    resourceRemaining.resize(nodes, 1200);
    // One RTS controller owns these notification slots for its world/projectile pair.
    if (projectiles.onDamage || w.melee->onHit)
        throw std::invalid_argument("RTS requires unowned combat notification slots");
    projectiles.onDamage = [this](int victim, int shooter, float damage) {
        if (victim >= 0 && victim < world_.count() && shooter >= 0 && shooter < world_.count() &&
            damage > 0)
            lastDamager_[victim] = shooter;
    };
    w.melee->onHit = [this](int attacker, int victim) {
        if (victim >= 0 && victim < world_.count() && attacker >= 0 && attacker < world_.count())
            lastDamager_[victim] = attacker;
    };
    for (int i = 0; i < nodes; i++) {
        float angle = i * (6.2831853f / nodes);
        resourcePositions[i] = quantizeGoal({std::cos(angle) * 12, 0, std::sin(angle) * 12});
        resourceKinds[i] = ResourceKind(i & 1);
    }
    for (int a = 0; a < w.count(); a++) {
        int mix = (a >> 1) % 8;
        roles[a] = mix < 3   ? RTSRole::Worker
                   : mix < 5 ? RTSRole::Melee
                   : mix < 7 ? RTSRole::Archer
                             : RTSRole::Slinger;
        w.visualArchetype[a] = roles[a] == RTSRole::Melee ? 10 : uint8_t(uint8_t(roles[a]) + 1);
        probe_[a] = (a * 17) % w.count();
        wasAlive_[a] = uint8_t(alive(a));
    }
}
RTS::~RTS() {
    projectiles_.onDamage = {};
    world_.melee->onHit = {};
}
bool RTS::alive(int a) const {
    return a >= 0 && a < world_.count() && world_.health[a] > 0 &&
           world_.state[a] != uint8_t(State::Dead);
}
void RTS::order(int a, Order value) {
    orders[a] = value;
    command[a] = uint8_t((command[a] & 240) | uint8_t(value));
}
void RTS::stop(int a) {
    speedCode[a] = 0;
    world_.locomotion(a, {}, 0);
}
Vec RTS::quantizeGoal(Vec p) {
    if (!p.finite() || std::max(std::abs(p.x), std::abs(p.z)) > 1e6f)
        throw std::invalid_argument("Invalid RTS goal");
    return {std::nearbyint(p.x / .125f) * .125f, 0, std::nearbyint(p.z / .125f) * .125f};
}
bool RTS::setMove(int a, Vec goal) {
    if (!alive(a) || !goal.finite() || std::max(std::abs(goal.x), std::abs(goal.z)) > 1e6f)
        return false;
    goals[a] = quantizeGoal(goal);
    targetActor[a] = -1;
    order(a, Order::Move);
    return true;
}
bool RTS::setGather(int a, int node) {
    if (!alive(a) || roles[a] != RTSRole::Worker || node < 0 ||
        size_t(node) >= resourcePositions.size() || resourceRemaining[node] <= 0)
        return false;
    targetResource[a] = node;
    targetActor[a] = -1;
    gatherTime_[a] = 0;
    order(a, carried[a] > 0 ? Order::ReturnResources : Order::Gather);
    return true;
}
bool RTS::setAttack(int a, int target) {
    if (!alive(a) || !alive(target) || a == target || world_.faction[a] == world_.faction[target])
        return false;
    targetActor[a] = target;
    order(a, Order::Attack);
    return true;
}
int RTS::nearestResource(Vec p) const {
    int result = -1;
    float best = std::numeric_limits<float>::infinity();
    for (size_t i = 0; i < resourcePositions.size(); i++)
        if (resourceRemaining[i] > 0) {
            float d = flat(p, resourcePositions[i]);
            if (d < best) {
                best = d;
                result = int(i);
            }
        }
    return result;
}
int RTS::findEnemy(int a) {
    int result = -1, start = probe_[a], count = world_.count();
    float best = std::numeric_limits<float>::infinity();
    Vec me = world_.joint(a, 0);
    for (int probe = 0; probe < std::min(32, count); probe++) {
        int b = (start + probe) % count;
        lastProbes++;
        if (!alive(b) || world_.faction[a] == world_.faction[b])
            continue;
        float d = flat(me, world_.joint(b, 0));
        if (d < best) {
            best = d;
            result = b;
        }
    }
    probe_[a] = (start + std::min(32, count)) % count;
    return result;
}
bool RTS::move(int a, Vec goal, float stopDistance) {
    Vec delta = goal - world_.joint(a, 0);
    delta.y = 0;
    if (delta.squared() <= stopDistance * stopDistance) {
        stop(a);
        if (orders[a] == Order::Move)
            order(a, Order::Idle);
        return true;
    }
    int heading = 0;
    float best = -std::numeric_limits<float>::infinity();
    for (int bin = 0; bin < 16; bin++) {
        float value = Vec::dot(delta, headings[bin]);
        if (value > best) {
            best = value;
            heading = bin;
        }
    }
    int speed = roles[a] == RTSRole::Melee ? 3 : roles[a] == RTSRole::Slinger ? 1 : 2;
    command[a] = uint8_t((heading << 4) | uint8_t(orders[a]));
    speedCode[a] = uint8_t(speed);
    world_.locomotion(a, headings[heading], speeds[speed]);
    return false;
}
bool RTS::reserve(int a) {
    if (a < 0 || a >= world_.count())
        return false;
    stop(a);
    world_.visualArchetype[a] = 7;
    world_.health[a] = world_.strength[a] = 0;
    world_.state[a] = uint8_t(State::Dead);
    reserved_[a] = 1;
    wasAlive_[a] = 0;
    carried[a] = 0;
    targetActor[a] = targetResource[a] = lastDamager_[a] = -1;
    order(a, Order::Idle);
    return true;
}
void RTS::accountDeath(int a) {
    if (!wasAlive_[a])
        return;
    int killer = lastDamager_[a];
    if (killer >= 0 && killer < world_.count() && world_.faction[killer] != world_.faction[a])
        kills[world_.faction[killer] & 1]++;
    wasAlive_[a] = 0;
    carried[a] = 0;
    order(a, Order::Idle);
}
bool RTS::recruit(int team, RTSRole role) {
    if (team < 0 || team > 1 || uint8_t(role) > 3)
        return false;
    int wc = role == RTSRole::Worker ? 15 : 20, sc = role == RTSRole::Worker ? 0 : 10;
    if (wood[team] < wc || stone[team] < sc)
        return false;
    int a = -1;
    for (int i = 0; i < world_.count(); i++)
        if ((world_.faction[i] & 1) == team && !alive(i)) {
            a = i;
            break;
        }
    if (a < 0)
        return false;
    accountDeath(a);
    Vec spawn = bases[team] + Vec{(a % 4 - 1.5f) * .8f, 0, ((a / 4) % 2) * .8f};
    float mass = role == RTSRole::Worker   ? 65.f
                 : role == RTSRole::Melee  ? 85.f
                 : role == RTSRole::Archer ? 70.f
                                           : 68.f;
    world_.spawn(a, spawn, team == 0 ? 90.f : -90.f, mass, team);
    wood[team] -= wc;
    stone[team] -= sc;
    roles[a] = role;
    world_.visualArchetype[a] = role == RTSRole::Melee ? 10 : uint8_t(uint8_t(role) + 1);
    reserved_[a] = 0;
    wasAlive_[a] = 1;
    carried[a] = 0;
    targetActor[a] = targetResource[a] = lastDamager_[a] = -1;
    gatherTime_[a] = 0;
    cooldown_[a] = .5f;
    order(a, Order::Idle);
    return true;
}
void RTS::decide(int a, float elapsed) {
    if (!alive(a)) {
        accountDeath(a);
        return;
    }
    wasAlive_[a] = 1;
    if (world_.state[a] != uint8_t(State::Active) && world_.state[a] != uint8_t(State::Crawling))
        return;
    cooldown_[a] = std::max(0.f, cooldown_[a] - elapsed);
    if (enemyAI && world_.faction[a] == 1) {
        if (roles[a] == RTSRole::Worker && orders[a] == Order::Idle) {
            int node = nearestResource(world_.joint(a, 0));
            if (node >= 0)
                setGather(a, node);
        } else if (roles[a] != RTSRole::Worker &&
                   (orders[a] != Order::Attack || !alive(targetActor[a]))) {
            int b = findEnemy(a);
            if (b >= 0)
                setAttack(a, b);
        }
    } else if (playerAutoDefend && world_.faction[a] == 0 && orders[a] == Order::Idle &&
               roles[a] != RTSRole::Worker) {
        int b = findEnemy(a);
        if (b >= 0 && flat(world_.joint(a, 0), world_.joint(b, 0)) <= 64)
            setAttack(a, b);
    }
    switch (orders[a]) {
    case Order::Move:
        move(a, goals[a], .3f);
        break;
    case Order::Gather: {
        int node = targetResource[a];
        if (node < 0 || size_t(node) >= resourcePositions.size() || resourceRemaining[node] <= 0) {
            if (carried[a] > 0) {
                order(a, Order::ReturnResources);
                break;
            }
            node = nearestResource(world_.joint(a, 0));
            if (node < 0) {
                order(a, Order::Idle);
                stop(a);
                break;
            }
            targetResource[a] = node;
        }
        if (!move(a, resourcePositions[node], 1.15f))
            break;
        gatherTime_[a] += elapsed;
        int take =
            std::min(int(gatherTime_[a] / .35f), std::min(resourceRemaining[node], 8 - carried[a]));
        if (take > 0) {
            if (!carried[a])
                carriedKind[a] = resourceKinds[node];
            gatherTime_[a] -= take * .35f;
            carried[a] += take;
            resourceRemaining[node] -= take;
        }
        if (carried[a] >= 8 || resourceRemaining[node] <= 0)
            order(a, Order::ReturnResources);
        break;
    }
    case Order::ReturnResources: {
        int team = world_.faction[a] & 1, node = targetResource[a];
        if (!move(a, bases[team], 1.6f))
            break;
        if (carried[a] > 0 && node >= 0 && size_t(node) < resourcePositions.size()) {
            if (carriedKind[a] == ResourceKind::Wood)
                wood[team] += carried[a];
            else
                stone[team] += carried[a];
            delivered[team] += carried[a] * (carriedKind[a] == ResourceKind::Wood ? 1 : 2);
            carried[a] = 0;
        }
        if (node < 0 || size_t(node) >= resourcePositions.size() || resourceRemaining[node] <= 0)
            node = nearestResource(world_.joint(a, 0));
        targetResource[a] = node;
        gatherTime_[a] = 0;
        order(a, node >= 0 ? Order::Gather : Order::Idle);
        break;
    }
    case Order::Attack: {
        int b = targetActor[a];
        if (!alive(b) || world_.faction[a] == world_.faction[b]) {
            order(a, Order::Idle);
            stop(a);
            break;
        }
        auto role = roles[a];
        bool ranged = role == RTSRole::Archer || role == RTSRole::Slinger;
        float range = ranged                   ? (role == RTSRole::Archer ? 13.f : 10.f)
                      : role == RTSRole::Melee ? 1.8f
                                               : 1.15f;
        if (!move(a, world_.joint(b, 0), range))
            break;
        if (cooldown_[a] > 0)
            break;
        if (ranged) {
            Vec spawn = world_.joint(a, role == RTSRole::Archer ? 9 : 6),
                delta = world_.joint(b, 2) - spawn;
            float speed = role == RTSRole::Archer ? 25.f : 18.f, flight = delta.length() / speed;
            delta.y += .5f * 9.81f * flight * flight;
            projectiles_.fire(world_, spawn, delta, speed, role == RTSRole::Archer ? 28.f : 42.f,
                              role == RTSRole::Archer ? .035f : .12f, a,
                              role == RTSRole::Archer ? ProjectileKind::Arrow
                                                      : ProjectileKind::Stone,
                              role == RTSRole::Archer ? 22.f : 18.f);
            cooldown_[a] = role == RTSRole::Archer ? 1.3f : 1.7f;
            world_.beginAttack(a, Action::Throw);
        } else if (world_.melee->begin(a, b, role == RTSRole::Melee ? 24.f : 12.f,
                                       role == RTSRole::Melee ? 32.f : 18.f))
            cooldown_[a] = 1.05f;
        break;
    }
    default:
        stop(a);
        break;
    }
}
void RTS::tick(float dt) {
    lastDecisions = lastProbes = 0;
    if (!std::isfinite(dt) || dt <= 0)
        return;
    dt = std::min(.1f, dt);
    now_ += dt;
    int count = world_.count();
    credit_ = std::min(float(count), credit_ + count * 10.f * dt);
    int budget = std::min(std::max(1, std::min(maximumDecisions, count)), int(credit_));
    credit_ -= budget;
    for (int n = 0; n < budget; n++) {
        int a = cursor_;
        cursor_ = (cursor_ + 1) % count;
        float elapsed = clamp(now_ - lastDecision_[a], 0, .5f);
        lastDecision_[a] = now_;
        lastDecisions++;
        totalDecisions++;
        decide(a, elapsed);
    }
    recruitClock_ += dt;
    if (enemyAI && recruitClock_ >= 5) {
        recruitClock_ = std::fmod(recruitClock_, 5.f);
        int workers = 0;
        for (int a = 0; a < count; a++)
            if (world_.faction[a] == 1 && alive(a) && roles[a] == RTSRole::Worker)
                workers++;
        recruit(1, workers < 2 ? RTSRole::Worker : RTSRole(1 + (int(now_ * .2f) % 3)));
    }
    for (int team = 0; team < 2; team++)
        score[team] = delivered[team] + kills[team] * 100;
}
} // namespace out::motor
