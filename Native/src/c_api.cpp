// SPDX-License-Identifier: LicenseRef-OUT-Source-Review-1.0
// Copyright (c) 2026 Алексей Савельев (Alex Merqury). See ../../LICENSE.
#include "out_guard.hpp"
#include "out_motor.hpp"
#include "out_native_c.h"
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <type_traits>
namespace g = out::guard;
namespace m = out::motor;
namespace {
thread_local char lastError[256]{};
void error(const char* s) noexcept {
    std::snprintf(lastError, sizeof(lastError), "%s", s);
}
template <class F> int invoke(F f) noexcept {
    lastError[0] = 0;
    try {
        f();
        return 1;
    } catch (const std::exception& e) {
        error(e.what());
        return 0;
    } catch (...) {
        error("Unknown native exception");
        return 0;
    }
}
template <class T> T& handle(void* p) {
    if (!p)
        throw std::invalid_argument("Null native handle");
    return *static_cast<T*>(p);
}
g::Bytes input(const uint8_t* p, size_t n) {
    if ((!p && n) || n > g::Codec::MaximumPayloadBytes + 88)
        throw std::invalid_argument("Invalid byte span");
    return n ? g::Bytes(p, p + n) : g::Bytes{};
}
std::string context(const char* p) {
    if (!p)
        throw std::invalid_argument("Null context");
    size_t n = 0;
    while (n <= 4096 && p[n])
        n++;
    if (n > 4096)
        throw std::invalid_argument("Context exceeds bound");
    return std::string(p, n);
}
int copy(const g::Bytes& bytes, uint8_t* output, size_t capacity, size_t* written) {
    if (!written)
        throw std::invalid_argument("Null written count");
    *written = bytes.size();
    if (capacity < bytes.size() || (!output && !bytes.empty()))
        return 2;
    if (!bytes.empty())
        std::memcpy(output, bytes.data(), bytes.size());
    return 1;
}
} // namespace
int out_native_abi(void) {
    return 1;
}
const char* out_native_last_error(void) {
    return lastError;
}
void* out_guard_movement_create(const OUTGuardMovementLimits* p) {
    void* value = nullptr;
    invoke([&] {
        g::MovementLimits limits;
        if (p) {
            limits = {p->maximumSpeed,           p->maximumAcceleration,   p->maximumVerticalSpeed,
                      p->maximumPredictionError, p->relativeTolerance,     p->maximumSampleGap,
                      p->minimumSampleInterval,  p->tableEvidenceThreshold};
        }
        value = new g::MovementTracker(limits);
    });
    return value;
}
void out_guard_movement_destroy(void* p) {
    delete static_cast<g::MovementTracker*>(p);
}
int out_guard_movement_observe(void* p, double time, double x, double y, double z, int teleport,
                               OUTGuardMovementResult* output) {
    if (output)
        *output = {};
    return invoke([&] {
        if (!output)
            throw std::invalid_argument("Null result");
        auto r = handle<g::MovementTracker>(p).observe(time, x, y, z, teleport != 0);
        *output = {r.accepted,          r.hasNormalizedFeatures,
                   r.skipped,           r.hasEvidence,
                   r.rebased,           r.violations,
                   int32_t(r.signal),   r.speedRatio,
                   r.accelerationRatio, r.verticalRatio,
                   r.predictionRatio,   r.observed,
                   r.allowed,           r.tableRisk};
    });
}
void* out_guard_codec_create(const uint8_t* p, size_t size) {
    void* result = nullptr;
    invoke([&] {
        if (!p || size != 32)
            throw std::invalid_argument("Expected 32-byte key");
        g::Key key;
        std::copy_n(p, 32, key.begin());
        result = new g::Codec(key);
    });
    return result;
}
void out_guard_codec_destroy(void* p) {
    delete static_cast<g::Codec*>(p);
}
int out_guard_codec_encode(void* p, const uint8_t* data, size_t n, const char* name,
                           uint8_t* output, size_t capacity, size_t* written) {
    if (written)
        *written = 0;
    int status = 0;
    int ok = invoke([&] {
        auto bytes = handle<g::Codec>(p).encode(input(data, n), context(name));
        status = copy(bytes, output, capacity, written);
    });
    return ok ? status : 0;
}
int out_guard_codec_decode(void* p, const uint8_t* data, size_t n, const char* name,
                           uint8_t* output, size_t capacity, size_t* written) {
    if (written)
        *written = 0;
    int status = 0;
    int ok = invoke([&] {
        g::Bytes bytes;
        if (!handle<g::Codec>(p).decode(input(data, n), context(name), bytes))
            throw std::runtime_error("Envelope rejected");
        status = copy(bytes, output, capacity, written);
    });
    return ok ? status : 0;
}
void* out_motor_world_create(const OUTMotorBankView* p, int count) {
    void* result = nullptr;
    invoke([&] {
        if (!p || !p->poses || !p->field || !p->relativeMass || !p->mirrored || !p->parents ||
            !p->response || !p->gain || !p->damping)
            throw std::invalid_argument("Complete licensed bank view required");
        static_assert(sizeof(m::Vec) == 3 * sizeof(float) && std::is_trivially_copyable_v<m::Vec>,
                      "Float ABI mismatch");
        auto bank = std::make_shared<m::DataBank>();
        for (int i = 0; i < 17 * m::Joints; i++)
            bank->poses[i] = {p->poses[i * 3], p->poses[i * 3 + 1], p->poses[i * 3 + 2]};
        for (int i = 0; i < 27; i++)
            bank->field[i] = {p->field[i * 3], p->field[i * 3 + 1], p->field[i * 3 + 2]};
        std::copy_n(p->relativeMass, 18, bank->relativeMass.begin());
        std::copy_n(p->mirrored, 18, bank->mirrored.begin());
        std::copy_n(p->parents, 18, bank->parents.begin());
        std::copy_n(p->response, 128, bank->response.begin());
        std::copy_n(p->gain, 16, bank->gain.begin());
        std::copy_n(p->damping, 16, bank->damping.begin());
        result = new m::World(bank, count);
    });
    return result;
}
void out_motor_world_destroy(void* p) {
    delete static_cast<m::World*>(p);
}
int out_motor_world_tick(void* p) {
    return invoke([&] { handle<m::World>(p).tick(); });
}
int out_motor_world_workers(void* p, int workers) {
    return invoke([&] {
        if (workers < 1 || workers > 64)
            throw std::invalid_argument("Workers outside 1..64");
        handle<m::World>(p).settings.workers = workers;
    });
}
int out_motor_world_locomotion(void* p, int actor, float x, float z, float speed) {
    return invoke([&] { handle<m::World>(p).locomotion(actor, {x, 0, z}, speed); });
}
int out_motor_world_event(void* p, int kind, int actor, int joint, float x, float y, float z,
                          float damage) {
    return invoke([&] {
        if (kind < 0 || kind > 2 ||
            !handle<m::World>(p).queue(m::EventKind(kind), actor, joint, {x, y, z}, damage))
            throw std::invalid_argument("Event rejected or queue full");
    });
}
int out_motor_world_copy_positions(void* p, float* output, size_t capacity, size_t* written) {
    if (written)
        *written = 0;
    int status = 0;
    int ok = invoke([&] {
        auto& world = handle<m::World>(p);
        if (!written)
            throw std::invalid_argument("Null written count");
        size_t n = world.position.size() * 3;
        *written = n;
        if (capacity < n || !output) {
            status = 2;
            return;
        }
        for (size_t i = 0; i < world.position.size(); i++) {
            output[i * 3] = world.position[i].x;
            output[i * 3 + 1] = world.position[i].y;
            output[i * 3 + 2] = world.position[i].z;
        }
        status = 1;
    });
    return ok ? status : 0;
}
