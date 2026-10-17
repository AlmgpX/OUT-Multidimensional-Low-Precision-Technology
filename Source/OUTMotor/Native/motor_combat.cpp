// SPDX-License-Identifier: LicenseRef-OUT-Source-Review-1.0
// Copyright (c) 2026 Алексей Савельев (Alex Merqury). See ../../../LICENSE.
#include "out_motor.hpp"
#include <algorithm>
#include <limits>
namespace out::motor {
namespace {
float unit(float v) {
    return std::max(0.f, std::min(1.f, v));
}
bool sweep(Vec center, Vec from, Vec path, float radius, float& t) {
    Vec offset = from - center;
    float c = offset.squared() - radius * radius;
    t = 0;
    if (c <= 0)
        return true;
    float a = path.squared();
    if (a < 1e-12f)
        return false;
    float b = Vec::dot(offset, path), discriminant = b * b - a * c;
    if (b > 0 || discriminant < 0)
        return false;
    t = (-b - std::sqrt(discriminant)) / a;
    return t >= 0 && t <= 1;
}
bool alive(const World& w, int a) {
    return a >= 0 && a < w.count() && w.health[a] > 0 && w.state[a] != uint8_t(State::Dead);
}
bool canSwing(const World& w, int a) {
    return alive(w, a) && w.state[a] == uint8_t(State::Active) && !w.stumbling(a) &&
           !w.detached[size_t(a) * Joints + 7] && !w.detached[size_t(a) * Joints + 8] &&
           !w.detached[size_t(a) * Joints + 9];
}
bool validTarget(const World& w, int a, int b) {
    return alive(w, b) && a != b && w.faction[a] != w.faction[b];
}
Weapon resolve(const World& w, int a, Weapon weapon) {
    return weapon == Weapon::Auto ? (w.visualArchetype[a] == 5    ? Weapon::Baton
                                     : w.visualArchetype[a] == 10 ? Weapon::Sword
                                                                  : Weapon::Unarmed)
                                  : weapon;
}
Vec weaponTip(const World& w, int a, Weapon weapon, Vec& origin, float& radius) {
    weapon = resolve(w, a, weapon);
    origin = w.joint(a, 9);
    Vec forearm = (origin - w.joint(a, 8)).normalized();
    if (forearm.squared() < .001f)
        forearm = w.heading(a).rotate({0, 1, 0});
    radius = weapon == Weapon::Unarmed ? .075f : .035f;
    return origin + forearm * (weapon == Weapon::Baton   ? .545f
                               : weapon == Weapon::Sword ? .94f
                                                         : 0);
}
} // namespace
Melee::Melee(World& w) : world_(w), attacks_(size_t(w.count())) {}
bool Melee::begin(int a, int b, float damage, float impulse, Weapon weapon) {
    auto& w = world_;
    if (!canSwing(w, a) || !validTarget(w, a, b) || attacks_[a].active || w.attacking(a) ||
        !std::isfinite(damage) || !std::isfinite(impulse) || damage < 0 || impulse < 0 ||
        damage > 1e6f || impulse > 1e6f || uint8_t(weapon) > 3)
        return false;
    auto& attack = attacks_[a];
    attack = {};
    attack.target = b;
    attack.attackerGeneration = w.generation(a);
    attack.targetGeneration = w.generation(b);
    attack.damage = damage;
    attack.impulse = impulse;
    attack.weapon = resolve(w, a, weapon);
    attack.active = true;
    Vec origin;
    float radius;
    attack.previous = weaponTip(w, a, attack.weapon, origin, radius);
    w.beginAttack(a, Action::Strike);
    started++;
    return true;
}
void Melee::cancel(int a) {
    if (a < 0 || size_t(a) >= attacks_.size() || !attacks_[a].active)
        return;
    attacks_[a].active = false;
    canceled++;
}
void Melee::cancelTarget(int target) {
    for (size_t a = 0; a < attacks_.size(); a++)
        if (attacks_[a].active && attacks_[a].target == target)
            cancel(int(a));
}
int Melee::target(int a) const {
    if (a < 0 || size_t(a) >= attacks_.size())
        return -1;
    return attacks_[a].active ? attacks_[a].target : -1;
}
MeleePhase Melee::phase(int a) const {
    if (a < 0 || size_t(a) >= attacks_.size() || !attacks_[a].active)
        return MeleePhase::Idle;
    float t = attacks_[a].time;
    return t < .18f ? MeleePhase::Windup : t < .60f ? MeleePhase::Contact : MeleePhase::Recovery;
}
void Melee::tick(float dt) {
    contactTests = 0;
    if (!std::isfinite(dt) || dt <= 0)
        return;
    dt = std::min(dt, .1f);
    auto& w = world_;
    for (int a = 0; a < w.count(); a++) {
        auto& attack = attacks_[a];
        if (!attack.active)
            continue;
        int b = attack.target;
        if (!canSwing(w, a) || !validTarget(w, a, b) ||
            attack.attackerGeneration != w.generation(a) ||
            attack.targetGeneration != w.generation(b)) {
            cancel(a);
            continue;
        }
        float before = attack.time;
        attack.time += dt;
        Vec origin;
        float radius;
        Vec tip = weaponTip(w, a, attack.weapon, origin, radius), from = attack.previous;
        attack.previous = tip;
        if (!attack.hit && attack.time >= .18f && before < .60f) {
            Vec full = tip - from;
            if (full.squared() < (.35f * dt) * (.35f * dt))
                continue;
            float start = unit((.18f - before) / dt), end = unit((.60f - before) / dt);
            from += full * start;
            Vec path = full * (end - start);
            float closest = 2;
            int joint = -1;
            Vec contact = tip;
            float reach = (attack.weapon == Weapon::Sword   ? .94f
                           : attack.weapon == Weapon::Baton ? .545f
                                                            : 0) +
                          2.4f;
            for (int j = 0;
                 (w.joint(a, 0) - w.joint(b, 0)).squared() <= reach * reach && j < Joints; j++) {
                if (w.detached[size_t(b) * Joints + j])
                    continue;
                contactTests++;
                float t;
                if (sweep(w.joint(b, j), from, path, radius + DataBank::radius(j), t) &&
                    t < closest) {
                    closest = t;
                    joint = j;
                    contact = from + path * t;
                }
                if (attack.weapon != Weapon::Unarmed &&
                    sweep(w.joint(b, j), origin, tip - origin, radius + DataBank::radius(j), t) &&
                    closest > 0) {
                    closest = 0;
                    joint = j;
                    contact = origin + (tip - origin) * t;
                }
            }
            if (joint >= 0) {
                Vec direction =
                    path.squared() > 1e-8f ? path.normalized() : w.heading(a).rotate({0, 0, 1});
                if (w.queue(EventKind::Damage, b, joint, direction * attack.impulse,
                            attack.damage)) {
                    attack.hit = true;
                    hits++;
                    lastHitPoint = contact;
                    lastHitJoint = joint;
                    lastHitDamage = attack.damage;
                    if (onHit)
                        onHit(a, b);
                }
            }
        }
        if (attack.time >= .85f) {
            attack.active = false;
            if (!attack.hit)
                misses++;
        }
    }
}
void Projectiles::clear() {
    for (auto& p : items_)
        p = {};
    cursor_ = 0;
    active = narrowTests = 0;
}
bool Projectiles::fire(World& w, Vec spawn, Vec direction, float speed, float impulse, float radius,
                       int shooter, ProjectileKind kind, float damage) {
    if (!spawn.finite() || !direction.finite() || direction.squared() < 1e-8f ||
        !std::isfinite(speed) || speed <= 0 || speed > 10000 || !std::isfinite(impulse) ||
        impulse < 0 || impulse > 1e6f || !std::isfinite(radius) || radius <= 0 || radius > 2 ||
        !std::isfinite(damage) || damage < 0 || damage > 1e6f || shooter < -1 ||
        shooter >= w.count() || uint8_t(kind) > 2)
        return false;
    size_t slot = items_.size();
    for (size_t n = 0; n < items_.size(); n++) {
        size_t i = (cursor_ + n) % items_.size();
        if (!items_[i].alive) {
            slot = i;
            break;
        }
    }
    if (slot == items_.size())
        return false;
    cursor_ = (slot + 1) % items_.size();
    items_[slot] = {spawn,   direction.normalized() * speed,
                    radius,  impulse,
                    damage,  kind == ProjectileKind::Bullet ? 2.f : 8.f,
                    shooter, shooter >= 0 ? w.generation(shooter) : 0,
                    kind,    true};
    active++;
    return true;
}
void Projectiles::tick(World& w, float dt) {
    narrowTests = 0;
    if (!std::isfinite(dt) || dt <= 0)
        return;
    dt = std::min(dt, .1f);
    for (auto& p : items_)
        if (p.alive) {
            p.time -= dt;
            if (p.time <= 0) {
                p.alive = false;
                active--;
                continue;
            }
            if (p.kind != ProjectileKind::Bullet)
                p.velocity.y -= 9.81f * dt;
            Vec path = p.velocity * dt;
            float closest = 1;
            int actor = -1, joint = -1;
            bool obstacle = false;
            if (obstacleFraction) {
                float t = obstacleFraction(p.position, p.position + path, p.radius);
                if (!std::isfinite(t) || t < 0 || t > 1)
                    throw std::runtime_error("Invalid obstacle sweep callback");
                if (t < 1) {
                    closest = t;
                    obstacle = true;
                }
            }
            if (p.position.y + path.y < p.radius && path.y < 0) {
                float t = unit((p.radius - p.position.y) / path.y);
                if (t < closest) {
                    closest = t;
                    obstacle = true;
                }
            }
            bool shooterValid = p.shooter >= 0 && p.generation == w.generation(p.shooter);
            for (int a = 0; a < w.count(); a++) {
                if (!alive(w, a) || (a == p.shooter && shooterValid))
                    continue;
                Vec root = w.joint(a, 0),
                    near = p.position + path * unit(Vec::dot(root - p.position, path) /
                                                    std::max(1e-8f, path.squared()));
                if ((root - near).squared() > 9)
                    continue;
                for (int j = 0; j < Joints; j++) {
                    if (w.detached[size_t(a) * Joints + j])
                        continue;
                    narrowTests++;
                    float t;
                    if (sweep(w.joint(a, j), p.position, path, p.radius + DataBank::radius(j), t) &&
                        t < closest) {
                        closest = t;
                        actor = a;
                        joint = j;
                        obstacle = false;
                    }
                }
            }
            if (actor >= 0) {
                Vec at = p.position + path * closest;
                float damage = p.damage > 0                       ? p.damage
                               : p.kind == ProjectileKind::Bullet ? 28.f
                               : p.kind == ProjectileKind::Arrow  ? 22.f
                                                                 : std::min(35.f, p.impulse * .32f);
                if (w.queue(EventKind::Damage, actor, joint, p.velocity.normalized() * p.impulse,
                            damage)) {
                    if (onImpact)
                        onImpact(actor, joint, at, damage);
                    if (onDamage)
                        onDamage(actor, shooterValid ? p.shooter : -1, damage);
                }
                p.position = at;
                p.alive = false;
                active--;
            } else if (obstacle) {
                p.position += path * closest;
                if (p.kind == ProjectileKind::Stone && p.velocity.length() > 2) {
                    p.velocity.y = std::abs(p.velocity.y) * .3f;
                    p.velocity.x *= .55f;
                    p.velocity.z *= .55f;
                    p.position.y = std::max(p.radius, p.position.y);
                } else {
                    p.alive = false;
                    active--;
                }
            } else
                p.position += path;
        }
}
BloodPool::BloodPool(size_t count) : drops_(count) {
    if (!count || count > 1048576)
        throw std::invalid_argument("Blood pool capacity");
}
void BloodPool::emit(Vec position, Vec normal, float damage, uint32_t seed) {
    if (!position.finite() || !normal.finite() || !std::isfinite(damage) || damage < 0)
        return;
    auto random = [&]() {
        seed ^= seed << 13;
        seed ^= seed >> 17;
        seed ^= seed << 5;
        return float(seed & 65535) / 65535.f;
    };
    int n = std::min(32, 3 + int(std::min(100.f, damage)));
    for (int i = 0; i < n; i++) {
        auto& p = drops_[cursor_++ % drops_.size()];
        p.position = position;
        p.velocity = normal * (.5f + random() * 2) +
                     Vec{(random() - .5f) * 2, random() * 2, (random() - .5f) * 2};
        p.time = 4 + random() * 12;
        p.size = .015f + random() * .04f;
        p.stain = false;
    }
}
void BloodPool::tick(float dt) {
    if (!std::isfinite(dt) || dt <= 0)
        return;
    dt = std::min(dt, .1f);
    for (auto& p : drops_)
        if (p.time > 0) {
            p.time = std::max(0.f, p.time - dt);
            if (!p.stain) {
                p.velocity.y -= 9.81f * dt;
                p.position += p.velocity * dt;
                if (p.position.y < p.size) {
                    p.position.y = p.size;
                    p.velocity = {};
                    p.stain = true;
                }
            }
        }
}
size_t writeCapsules(const World& w, CapsuleInstance* out, size_t capacity) {
    if (!out && capacity)
        throw std::invalid_argument("Null render buffer");
    size_t used = 0;
    for (int a = 0; a < w.count(); a++)
        for (int k = 0; k < 17; k++) {
            if (w.visualArchetype[a] == 7)
                continue;
            int j = DataBank::edgeA[k], child = DataBank::edgeB[k];
            if (w.detached[size_t(a) * Joints + j] != w.detached[size_t(a) * Joints + child])
                continue;
            if (used == capacity)
                return used;
            out[used++] = {w.joint(a, j),
                           w.joint(a, child),
                           std::min(DataBank::radius(j), DataBank::radius(child)),
                           w.faction[a],
                           w.state[a],
                           a,
                           child};
        }
    return used;
}
} // namespace out::motor
