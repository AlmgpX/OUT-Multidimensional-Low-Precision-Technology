// SPDX-License-Identifier: LicenseRef-OUT-Source-Review-1.0
// Copyright (c) 2026 Алексей Савельев (Alex Merqury). See ../../../LICENSE.
#include "out_guard.hpp"
#include <algorithm>
#include <cmath>
#include <limits>

namespace out::guard {
bool Vec::finite() const {
    return std::isfinite(x) && std::isfinite(y) && std::isfinite(z);
}
void Box::validate() const {
    if (!min.finite() || !max.finite() || min.x >= max.x || min.y >= max.y || min.z >= max.z ||
        std::max({std::abs(min.x), std::abs(min.y), std::abs(min.z), std::abs(max.x),
                  std::abs(max.y), std::abs(max.z)}) > 1000000)
        throw std::invalid_argument("Invalid server box");
}
namespace {
constexpr Vec Half{.35, .9, .35};
constexpr double Skin = .000001;
Box body(Vec p) {
    return {p - Half, p + Half};
}
bool overlap(Vec p, Box b) {
    return p.x > b.min.x - Half.x && p.x < b.max.x + Half.x && p.y > b.min.y - Half.y &&
           p.y < b.max.y + Half.y && p.z > b.min.z - Half.z && p.z < b.max.z + Half.z;
}
void timeCheck(double t) {
    if (!std::isfinite(t) || t < 0 || t > 1e12)
        throw std::invalid_argument("Invalid server monotonic time");
}
bool intersect(Vec origin, Vec delta, Box box, bool moving, double& fraction, Vec& normal) {
    double enter = -std::numeric_limits<double>::infinity(),
           leave = std::numeric_limits<double>::infinity();
    normal = {};
    fraction = 0;
    for (int axis = 0; axis < 3; axis++) {
        double p = axis == 0   ? origin.x
                   : axis == 1 ? origin.y
                               : origin.z,
               d = axis == 0   ? delta.x
                   : axis == 1 ? delta.y
                               : delta.z,
               lo = axis == 0   ? box.min.x
                    : axis == 1 ? box.min.y
                                : box.min.z,
               hi = axis == 0   ? box.max.x
                    : axis == 1 ? box.max.y
                                : box.max.z;
        if (std::abs(d) < 1e-15) {
            if (moving ? (p <= lo || p >= hi) : (p < lo || p > hi))
                return false;
            continue;
        }
        double a = (lo - p) / d, b = (hi - p) / d, sign = -1;
        if (a > b) {
            std::swap(a, b);
            sign = 1;
        }
        if (a > enter) {
            enter = a;
            normal = axis == 0 ? Vec{sign, 0, 0} : axis == 1 ? Vec{0, sign, 0} : Vec{0, 0, sign};
        }
        leave = std::min(leave, b);
        if (enter > leave)
            return false;
    }
    if (leave < 0 || enter > 1 || (moving && enter < -1e-10))
        return false;
    fraction = std::max(0., enter);
    return true;
}
} // namespace
struct AuthoritativeWorld::Impl {
    struct History {
        int64_t tick = 0;
        Vec position;
        bool valid = false;
    };
    struct Actor {
        std::string id;
        Vec position, velocity;
        int health = 100, ammo = 30, epoch = 1, rewind = 0;
        bool grounded = false, resync = false;
        int64_t queued = 0, processed = 0, shot = 0, nextFire = 0, dashUntil = 0, nextDash = 0;
        double lastInput = 0;
        ServerInput held{};
        std::array<ServerInput, 3> pending{};
        size_t head = 0, count = 0;
        std::array<History, 32> history{};
    };
    std::mutex mutex;
    std::vector<Box> walls;
    std::array<Actor, 64> actors{};
    size_t count = 0;
    double last = 0, accumulator = 0;
    int64_t tick = 0;
    Actor& find(const std::string& id) {
        for (size_t i = 0; i < count; i++)
            if (actors[i].id == id)
                return actors[i];
        throw std::out_of_range("Unknown server actor");
    }
    void record(Actor& a) {
        a.history[size_t(tick % 32)] = {tick, a.position, true};
    }
    bool history(const Actor& a, int64_t requested, Vec& p) const {
        if (requested < 0)
            return false;
        const auto& h = a.history[size_t(requested % 32)];
        if (!h.valid || h.tick != requested)
            return false;
        p = h.position;
        return true;
    }
    void interrupt(Actor& a) {
        if (!a.resync) {
            if (a.epoch == INT_MAX)
                throw std::overflow_error("Server epoch exhausted");
            a.epoch++;
        }
        a.resync = true;
        a.head = a.count = 0;
        a.held = {};
    }
    ServerSnapshot snapshot(const Actor& a) const {
        ServerSnapshot s;
        s.tick = tick;
        s.position = a.position;
        s.velocity = a.velocity;
        s.epoch = a.epoch;
        s.grounded = a.grounded;
        s.health = a.health;
        s.ammo = a.ammo;
        s.acknowledgedInput = a.processed;
        s.nextInputSequence = a.queued + 1;
        s.nextShotSequence = a.shot + 1;
        s.resyncRequired = a.resync;
        return s;
    }
    static void consider(Vec p, Vec d, Box wall, double& earliest, Vec& normal, double& contact,
                         bool& collided) {
        Box expanded{wall.min - Half, wall.max + Half};
        double fraction = 0;
        Vec hit;
        if (intersect(p, d, expanded, true, fraction, hit) && fraction <= earliest) {
            earliest = fraction;
            normal = hit;
            collided = true;
            contact = normal.x != 0   ? (normal.x < 0 ? expanded.min.x : expanded.max.x)
                      : normal.y != 0 ? (normal.y < 0 ? expanded.min.y : expanded.max.y)
                                      : (normal.z < 0 ? expanded.min.z : expanded.max.z);
        }
    }
    void move(Actor& a, Vec delta) {
        a.grounded = false;
        for (auto wall : walls)
            if (overlap(a.position, wall)) {
                a.velocity = {};
                return;
            }
        for (size_t i = 0; i < count; i++) {
            auto& other = actors[i];
            if (&other != &a && other.health > 0 && overlap(a.position, body(other.position))) {
                a.velocity = {};
                return;
            }
        }
        for (int iteration = 0; iteration < 4; iteration++) {
            double earliest = 1, contact = 0;
            Vec normal;
            bool collided = false;
            for (auto wall : walls)
                consider(a.position, delta, wall, earliest, normal, contact, collided);
            for (size_t i = 0; i < count; i++) {
                auto& other = actors[i];
                if (&other != &a && other.health > 0)
                    consider(a.position, delta, body(other.position), earliest, normal, contact,
                             collided);
            }
            if (!collided) {
                a.position = a.position + delta;
                break;
            }
            a.position = a.position + delta * std::max(0., earliest);
            if (normal.x != 0)
                a.position.x = contact;
            if (normal.y != 0)
                a.position.y = contact;
            if (normal.z != 0)
                a.position.z = contact;
            delta = delta * std::max(0., 1 - earliest);
            if (normal.x != 0) {
                a.velocity.x = 0;
                delta.x = 0;
            }
            if (normal.y != 0) {
                a.velocity.y = 0;
                delta.y = 0;
                if (normal.y > 0)
                    a.grounded = true;
            }
            if (normal.z != 0) {
                a.velocity.z = 0;
                delta.z = 0;
            }
            if (std::abs(delta.x) + std::abs(delta.y) + std::abs(delta.z) < 1e-12)
                break;
        }
    }
    void advance(double now) {
        timeCheck(now);
        now = std::max(now, last);
        accumulator += now - last;
        last = now;
        int64_t steps = int64_t(std::floor((accumulator + 1e-10) / FixedDelta));
        if (steps > 8) {
            tick += steps;
            accumulator = std::max(0., accumulator - steps * FixedDelta);
            for (size_t i = 0; i < count; i++) {
                interrupt(actors[i]);
                record(actors[i]);
            }
            return;
        }
        for (int64_t step = 0; step < steps; step++) {
            double stepTime = now - accumulator + FixedDelta;
            accumulator = std::max(0., accumulator - FixedDelta);
            tick++;
            for (size_t i = 0; i < count; i++) {
                auto& a = actors[i];
                if (a.health <= 0) {
                    a.velocity = {};
                    record(a);
                    continue;
                }
                if (stepTime - a.lastInput > .5 && !a.resync)
                    interrupt(a);
                ServerInput input{};
                if (!a.resync && a.count) {
                    input = a.pending[a.head];
                    a.head = (a.head + 1) % 3;
                    a.count--;
                    a.held = input;
                    a.processed = input.sequence;
                } else if (!a.resync && stepTime - a.lastInput <= .1) {
                    input = a.held;
                    input.jump = input.dash = false;
                }
                double n = std::sqrt(input.moveX * input.moveX + input.moveZ * input.moveZ),
                       x = n > 1 ? input.moveX / n : input.moveX,
                       z = n > 1 ? input.moveZ / n : input.moveZ;
                if (input.dash && tick >= a.nextDash) {
                    a.dashUntil = tick + 6;
                    a.nextDash = tick + 60;
                }
                double speed = tick < a.dashUntil ? 16 : Speed, dx = x * speed - a.velocity.x,
                       dz = z * speed - a.velocity.z, change = std::sqrt(dx * dx + dz * dz),
                       maximum = Acceleration * FixedDelta,
                       blend = change > maximum ? maximum / change : 1;
                a.velocity.x += dx * blend;
                a.velocity.z += dz * blend;
                if (input.jump && a.grounded) {
                    a.velocity.y = JumpSpeed;
                    a.grounded = false;
                }
                a.velocity.y = std::max(-30., a.velocity.y - Gravity * FixedDelta);
                move(a, a.velocity * FixedDelta);
                if (a.position.y < -100 || std::abs(a.position.x) > 10000 ||
                    std::abs(a.position.z) > 10000) {
                    a.health = 0;
                    a.velocity = {};
                }
                record(a);
            }
        }
    }
};
AuthoritativeWorld::AuthoritativeWorld(std::vector<Box> walls, double t)
    : impl_(std::make_unique<Impl>()) {
    timeCheck(t);
    if (walls.size() > 1024)
        throw std::invalid_argument("At most 1024 static boxes");
    for (auto& b : walls)
        b.validate();
    impl_->walls = std::move(walls);
    impl_->last = t;
}
AuthoritativeWorld::~AuthoritativeWorld() = default;
void AuthoritativeWorld::addActor(const std::string& id, Vec p, double t) {
    validateText(id, 256);
    if (!p.finite() || std::max({std::abs(p.x), std::abs(p.y), std::abs(p.z)}) > 10000)
        throw std::invalid_argument("Invalid trusted spawn");
    auto& w = *impl_;
    std::lock_guard<std::mutex> lock(w.mutex);
    w.advance(t);
    if (w.count >= 64)
        throw std::invalid_argument("Arena full");
    for (auto& b : w.walls)
        if (overlap(p, b))
            throw std::invalid_argument("Spawn intersects wall");
    for (size_t i = 0; i < w.count; i++)
        if (w.actors[i].id == id || overlap(p, body(w.actors[i].position)))
            throw std::invalid_argument("Duplicate or overlapping actor");
    auto& a = w.actors[w.count++];
    a.id = id;
    a.position = p;
    a.lastInput = w.last;
    w.move(a, {0, -.001, 0});
    w.record(a);
}
void AuthoritativeWorld::advance(double t) {
    std::lock_guard<std::mutex> lock(impl_->mutex);
    impl_->advance(t);
}
ServerSnapshot AuthoritativeWorld::snapshot(const std::string& id, double t) {
    auto& w = *impl_;
    std::lock_guard<std::mutex> lock(w.mutex);
    w.advance(t);
    return w.snapshot(w.find(id));
}
ServerSnapshot AuthoritativeWorld::resynchronize(const std::string& id, double t) {
    auto& w = *impl_;
    std::lock_guard<std::mutex> lock(w.mutex);
    w.advance(t);
    auto& a = w.find(id);
    a.resync = false;
    a.lastInput = w.last;
    return w.snapshot(a);
}
InputStatus AuthoritativeWorld::submit(const std::string& id, ServerInput input, double t) {
    auto& w = *impl_;
    std::lock_guard<std::mutex> lock(w.mutex);
    w.advance(t);
    auto& a = w.find(id);
    if (!std::isfinite(input.moveX) || !std::isfinite(input.moveZ) || std::abs(input.moveX) > 1 ||
        std::abs(input.moveZ) > 1)
        return InputStatus::InvalidInput;
    if (a.health <= 0)
        return InputStatus::Dead;
    if (a.resync || input.epoch != a.epoch)
        return InputStatus::ResyncRequired;
    if (input.sequence <= 0 || a.queued == INT64_MAX || input.sequence != a.queued + 1)
        return InputStatus::SequenceMismatch;
    if (input.acknowledgedTick > w.tick || input.acknowledgedTick < w.tick - 30)
        return InputStatus::StaleAcknowledgement;
    if (a.count >= 3)
        return InputStatus::QueueFull;
    a.pending[(a.head + a.count) % 3] = input;
    a.count++;
    a.queued = input.sequence;
    a.lastInput = w.last;
    return InputStatus::Queued;
}
void AuthoritativeWorld::setLagCompensation(const std::string& id, int ticks) {
    if (ticks < 0 || ticks > MaximumRewindTicks)
        throw std::invalid_argument("Untrusted rewind interval");
    std::lock_guard<std::mutex> lock(impl_->mutex);
    impl_->find(id).rewind = ticks;
}
ShotResult AuthoritativeWorld::fire(const std::string& id, int64_t sequence, Vec aim, double t) {
    auto& w = *impl_;
    std::lock_guard<std::mutex> lock(w.mutex);
    w.advance(t);
    auto& shooter = w.find(id);
    ShotResult r;
    r.tick = w.tick;
    r.remainingAmmo = shooter.ammo;
    double length = aim.x * aim.x + aim.y * aim.y + aim.z * aim.z;
    if (!aim.finite() || !std::isfinite(length) || length < .000001) {
        r.status = ShotStatus::InvalidAim;
        return r;
    }
    if (shooter.health <= 0) {
        r.status = ShotStatus::Dead;
        return r;
    }
    if (shooter.resync) {
        r.status = ShotStatus::ResyncRequired;
        return r;
    }
    if (sequence <= 0 || shooter.shot == INT64_MAX || sequence != shooter.shot + 1) {
        r.status = ShotStatus::SequenceMismatch;
        return r;
    }
    if (w.tick < shooter.nextFire) {
        r.status = ShotStatus::Cooldown;
        return r;
    }
    if (shooter.ammo <= 0) {
        r.status = ShotStatus::NoAmmo;
        return r;
    }
    shooter.shot = sequence;
    shooter.nextFire = w.tick + 6;
    shooter.ammo--;
    r.remainingAmmo = shooter.ammo;
    Vec origin = shooter.position + Vec{0, .6, 0}, ray = aim * (100 / std::sqrt(length));
    double wallDistance = 1;
    bool blocked = false;
    for (auto wall : w.walls) {
        double fraction;
        Vec normal;
        if (intersect(origin, ray, wall, false, fraction, normal) && fraction <= wallDistance) {
            blocked = true;
            wallDistance = fraction;
        }
    }
    Impl::Actor* hit = nullptr;
    double nearest = wallDistance;
    for (size_t i = 0; i < w.count; i++) {
        auto& target = w.actors[i];
        if (&target == &shooter || target.health <= 0)
            continue;
        Vec p = target.position;
        if (shooter.rewind > 0 && !w.history(target, w.tick - shooter.rewind, p))
            continue;
        double fraction;
        Vec normal;
        if (intersect(origin, ray, body(p), false, fraction, normal) && fraction < nearest - Skin) {
            nearest = fraction;
            hit = &target;
        }
    }
    if (hit) {
        r.status = ShotStatus::Hit;
        r.hitActor = hit->id;
        r.damage = std::min(25, hit->health);
        hit->health -= r.damage;
    } else
        r.status = blocked ? ShotStatus::Blocked : ShotStatus::Miss;
    return r;
}
} // namespace out::guard
