// Copyright (c) 2026 Алексей Савельев (Alex Merqury / AlexMerq). All rights reserved.
// SPDX-License-Identifier: LicenseRef-OUT-Source-Review-1.0
// Source review only. Project use and AI training require separate applicable licenses. See LICENSE.
#include "OUTMotorNative.h"

#include <algorithm>

#include <cmath>

#include <condition_variable>

#include <cstdint>

#include <mutex>

#include <new>

#include <thread>

#include <vector>

namespace {

constexpr int N = 18, L = 30;

constexpr float H = 1.f / 120.f;

struct V {

  float x, y, z;

  V operator+(V b) const { return {x + b.x, y + b.y, z + b.z}; }

  V operator-(V b) const { return {x - b.x, y - b.y, z - b.z}; }

  V operator-() const { return {-x, -y, -z}; }

  V operator*(float s) const { return {x * s, y * s, z * s}; }

  V operator/(float s) const { return {x / s, y / s, z / s}; }

  V &operator+=(V b) {

    *this = *this + b;

    return *this;

  }

  V &operator-=(V b) {

    *this = *this - b;

    return *this;

  }

};

struct Q {

  float x, y, z, w;

};

float dot(V a, V b) { return a.x * b.x + a.y * b.y + a.z * b.z; }

float sq(V a) { return dot(a, a); }

float mag(V a) { return std::sqrt(sq(a)); }

V cross(V a, V b) {

  return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};

}

V norm(V a) {

  float l = mag(a);

  return l > 1e-5f ? a / l : V{};

}

V lerp(V a, V b, float t) { return a + (b - a) * t; }

V rotate(Q q, V v) {

  // Match UnityEngine.Quaternion.operator*(Quaternion, Vector3) arithmetic order.

  // Algebraically equivalent cross-product rotation accumulated larger rounding drift.

  float num = q.x * 2.f, num2 = q.y * 2.f, num3 = q.z * 2.f;

  float num4 = q.x * num, num5 = q.y * num2, num6 = q.z * num3;

  float num7 = q.x * num2, num8 = q.x * num3, num9 = q.y * num3;

  float num10 = q.w * num, num11 = q.w * num2, num12 = q.w * num3;

  return {(1.f - (num5 + num6)) * v.x + (num7 - num12) * v.y + (num8 + num11) * v.z,

          (num7 + num12) * v.x + (1.f - (num4 + num6)) * v.y + (num9 - num10) * v.z,

          (num8 - num11) * v.x + (num9 + num10) * v.y + (1.f - (num4 + num5)) * v.z};

}

float clamp(float v, float lo, float hi) {

  return std::max(lo, std::min(hi, v));

}

const int ea[L] = {0,  1,  2, 2, 4,  5, 2, 7, 8, 0, 10, 11, 12, 0, 14,

                   15, 16, 0, 4, 10, 4, 7, 4, 7, 1, 1,  1,  1,  3, 3};

const int eb[L] = {1,  2,  3, 4, 5,  6,  7,  8,  9,  10, 11, 12, 13, 14, 15,

                   16, 17, 2, 7, 14, 10, 14, 14, 10, 10, 14, 4,  7,  4,  7};

const int ca[] = {6, 9, 12, 16, 5, 8, 13, 6, 9, 11, 12, 16},

          cb[] = {2, 2, 2, 2, 7, 4, 17, 9, 14, 15, 16, 12};

const int controlled[] = {0, 2, 6, 9, 12, 16, 3};

const float radii[N] = {.12f,  .13f,  .16f,  .145f, .075f, .065f,

                        .068f, .075f, .065f, .068f, .1f,   .078f,

                        .073f, .052f, .1f,   .078f, .073f, .052f};

struct Context {

  std::vector<std::thread> workers;

  std::mutex mutex;

  std::condition_variable begin, done;

  bool stopping = false;

  uint64_t generation = 0;

  int remaining = 0, active = 1, jobCount = 0;

  const OUTMotorNativeBuffers *jobBuffers = nullptr;

  const OUTMotorNativeConfig *jobConfig = nullptr;

  void startWorkers(int total);

  void step(const OUTMotorNativeBuffers &b, const OUTMotorNativeConfig &cfg,

            int count);

  ~Context() {

    {

      std::lock_guard<std::mutex> guard(mutex);

      stopping = true;

    }

    begin.notify_all();

    for (auto &worker : workers)

      worker.join();

  }

  int capacity;

  std::vector<V> old, force;

  std::vector<float> lambda;

  float mass[N], length[L], alpha[L];

  V field[27];

  Context(int capacity, const float *mass, const float *length,

          const float *field)

      : capacity(capacity), old(capacity * N), force(capacity * N),

        lambda(capacity * L) {

    std::copy(mass, mass + N, this->mass);

    std::copy(length, length + L, this->length);

    for (int i = 0; i < L; i++)

      alpha[i] = (i < 17 ? 2e-8f : 5e-8f) / (H * H);

    std::copy(reinterpret_cast<const V *>(field),

              reinterpret_cast<const V *>(field) + 27, this->field);

  }

  V sample(V v) const {

    float x = clamp(v.x / 1.2f + 1, 0, 2), y = clamp(v.y / 1.2f + 1, 0, 2),

          z = clamp(v.z / 1.2f + 1, 0, 2);

    int ix = std::min(1, int(x)), iy = std::min(1, int(y)),

        iz = std::min(1, int(z));

    x -= ix;

    y -= iy;

    z -= iz;

    int k = (iz * 3 + iy) * 3 + ix;

    return lerp(lerp(lerp(field[k], field[k + 1], x),

                     lerp(field[k + 3], field[k + 4], x), y),

                lerp(lerp(field[k + 9], field[k + 10], x),

                     lerp(field[k + 12], field[k + 13], x), y),

                z);

  }

};

struct Actor {

  Context &c;

  const OUTMotorNativeConfig &cfg;

  V *p, *v, *goal, *gv, *old, *force;

  float *im, *lambda;

  const unsigned char *detached;

  unsigned char *contacts;

  const Q rot;

  float gain, damp, strength;

  unsigned char state;

  bool same(int a, int b) const { return detached[a] == detached[b]; }

  void length(int k) {

    int i = ea[k], j = eb[k];

    if (!same(i, j))

      return;

    V d = p[j] - p[i];

    float l = mag(d);

    if (l < 1e-6f)

      return;

    float wi = im[i], wj = im[j], a = c.alpha[k];

    float dl = (-(l - c.length[k]) - a * lambda[k]) / (wi + wj + a);

    lambda[k] += dl;

    V direction = d * (dl / l);

    p[i] -= direction * wi;

    p[j] += direction * wj;

  }

  void cone(int i, int j, int k, float minCos, float maxCos) {

    if (!same(i, j) || !same(j, k))

      return;

    V u = p[j] - p[i], v = p[k] - p[j];

    float lu = mag(u), lv = mag(v);

    if (lu < 1e-5f || lv < 1e-5f)

      return;

    u = u / lu;

    v = v / lv;

    float cosine = dot(u, v), target = clamp(cosine, minCos, maxCos);

    if (std::abs(target - cosine) < 1e-5f)

      return;

    V g0 = -(v - u * cosine) / lu, g2 = (u - v * cosine) / lv, g1 = -g0 - g2;

    float wi = im[i], wj = im[j], wk = im[k],

          denom = wi * sq(g0) + wj * sq(g1) + wk * sq(g2);

    if (denom < 1e-7f)

      return;

    float l = clamp((target - cosine) / denom, -.1f, .1f);

    p[i] += g0 * (wi * l);

    p[j] += g1 * (wj * l);

    p[k] += g2 * (wk * l);

  }

  void hinge(int i, int j, int k, V reference, float slack) {

    if (!same(i, j) || !same(j, k))

      return;

    V chord = norm(p[k] - p[i]);

    float dd = sq(chord);

    V normal = dd < 1.17549435e-38f

                   ? reference

                   : reference - chord * (dot(reference, chord) / dd);

    if (sq(normal) < .0001f)

      return;

    normal = norm(normal);

    V middle = p[j] - (p[i] + p[k]) * .5f;

    float side = dot(middle, normal);

    if (side < .007f) {

      float m = std::min(.08f, .007f - side);

      p[j] += normal * (m * .8f);

      p[i] -= normal * (m * .2f);

      p[k] -= normal * (m * .2f);

    }

    V binormal = cross(chord, normal);

    float plane = dot(middle, binormal);

    if (std::abs(plane) > slack)

      p[j] -= binormal * ((plane - (plane >= 0 ? 1.f : -1.f) * slack) * .5f);

  }

  void footYaw(int ankle, int toe, V forward, V right) {

    // Coarse ankle yaw cone, not another motor: keep intact active feet within

    // +/-60 degrees of actor heading instead of letting floor friction drag toes backward.

    if (state != 0 || detached[ankle] != 0 || detached[toe] != 0 || !same(ankle, toe))

      return;

    V delta = p[toe] - p[ankle];

    V horizontal{delta.x, 0.f, delta.z};

    float length = std::sqrt(delta.x * delta.x + delta.z * delta.z);

    if (length < 1e-5f || dot(forward, horizontal) >= .5f * length)

      return;

    // Coarse dead zone avoids opposite cone-side choices from micrometer JIT/native noise.

    float side = dot(right, horizontal) < -1e-5f ? -1.f : 1.f;

    V clamped = forward * (length * .5f) + right * (side * length * .8660254f);

    // Change only the toe's X/Z. Preserve toe height and all ankle coordinates exactly.

    p[toe].x = p[ankle].x + clamped.x;

    p[toe].z = p[ankle].z + clamped.z;

  }

  void joints() {

    cone(10, 11, 12, -.866f, 1);

    cone(14, 15, 16, -.866f, 1);

    cone(4, 5, 6, -.906f, 1);

    cone(7, 8, 9, -.906f, 1);

    cone(0, 1, 2, .906f, 1);

    cone(0, 2, 3, .573f, 1);

    V f = rotate(rot, {0, 0, 1}), r = rotate(rot, {1, 0, 0}),

      u = rotate(rot, {0, 1, 0});

    hinge(10, 11, 12, f, .035f);

    hinge(14, 15, 16, f, .035f);

    hinge(4, 5, 6, r * -.6f - u * .7f + f * .45f, .11f);

    hinge(7, 8, 9, r * .6f - u * .7f + f * .45f, .11f);

  }

  unsigned char ground[N]{};

  void groundContacts() {

    for (int j = 0; j < N; j++)

      // Match the managed 0.1 mm contact skin: stable friction near the floor

      // despite tiny JIT/native floating-point position differences.

      if (p[j].y < radii[j] + .0001f) {

        p[j].y = radii[j];

        ground[j] = 1;

      }

  }

  void selfCollision() {

    for (int k = 0; k < 12; k++) {

      int i = ca[k], j = cb[k];

      if (!same(i, j))

        continue;

      V delta = p[j] - p[i];

      float d = mag(delta), limit = (radii[i] + radii[j]) * .89f;

      if (d >= limit || d < .0001f)

        continue;

      float wi = im[i], wj = im[j], correction = std::min(.065f, limit - d);

      V n = delta * (correction / d / (wi + wj));

      p[i] -= n * wi;

      p[j] += n * wj;

    }

  }

  void tick() {

    for (int j = 0; j < N; j++) {

      old[j] = p[j];

      force[j] = {};

    }

    if (state != 3 && state != 4) {

      for (int k = 0; k < 7; k++) {

        int j = controlled[k];

        if (detached[j])

          continue;

        V sample = c.sample(p[j] - goal[j]);

        float g = gain * strength * cfg.muscleStrength;

        if (state == 1)

          g *= j == 6 || j == 9 ? .72f : .05f;

        float stiff = k == 0 ? 420.f : k >= 4 ? 240.f : 230.f;

        float damper = (k == 0 ? 34.f : 25.f) * damp * cfg.damping,

              maxForce = c.mass[j] * 95.f * g;

        V desired =

            (sample * stiff - (v[j] - gv[j] * .55f) * damper) * (c.mass[j] * g);

        force[j] = {clamp(desired.x, -maxForce, maxForce),

                    clamp(desired.y, -maxForce, maxForce),

                    clamp(desired.z, -maxForce, maxForce)};

      }

      if (*contacts > 0) {

        for (int j = 0; j < N; j++)

          if (!detached[j])

            force[j].y +=

                9.81f * (state == 5 ? .2f : cfg.supportAssistance) / im[j];

      } else {

        V total{};

        for (int j = 0; j < N; j++)

          total += force[j];

        for (int j = 0; j < N; j++)

          force[j] -= total * (c.mass[j] / 70.f);

      }

    }

    float drag = 1 / (1 + cfg.airResistance * H);

    for (int j = 0; j < N; j++) {

      v[j] = (v[j] + (force[j] * im[j] + V{0, -9.81f, 0}) * H) * drag;

      p[j] += v[j] * H;

    }

    std::fill(lambda, lambda + L, 0.f);

    for (int iteration = 0; iteration < cfg.iterations; iteration++) {

      if (cfg.jointLimits)

        joints();

      for (int k = 0; k < L; k++)

        length(k);

      groundContacts();

    }

    if (cfg.selfCollisions)

      selfCollision();

    for (int k = 0; k < L; k++)

      length(k);

    groundContacts();

    // Match managed order: no later length projection may move the ankle after yaw.

    if (cfg.jointLimits) {

      V f = rotate(rot, {0, 0, 1}), r = rotate(rot, {1, 0, 0});

      footYaw(12, 13, f, r);

      footYaw(16, 17, f, r);

    }

    unsigned char count = 0;

    for (int j = 0; j < N; j++) {

      V velocity = (p[j] - old[j]) / H;

      v[j] = {clamp(velocity.x, -18, 18), clamp(velocity.y, -18, 18),

              clamp(velocity.z, -18, 18)};

      if (ground[j]) {

        count++;

        float friction = j == 12 || j == 13 || j == 16 || j == 17 ? .72f : .87f;

        v[j] = {v[j].x * friction, std::max(0.f, v[j].y), v[j].z * friction};

      }

    }

    *contacts = count;

    float coeff = cfg.damping * H * 7;

    coeff /= 1 + coeff;

    for (int k = 0; k < 17; k++) {

      int i = ea[k], j = eb[k];

      if (!same(i, j))

        continue;

      float wi = im[i], wj = im[j];

      V impulse = (v[j] - v[i]) * (coeff / (wi + wj));

      v[i] += impulse * wi;

      v[j] -= impulse * wj;

    }

  }

};

void actorStep(Context &c, const OUTMotorNativeBuffers &b,

               const OUTMotorNativeConfig &cfg, int actor) {

  int o = actor * N;

  Actor a{c,

          cfg,

          reinterpret_cast<V *>(b.position) + o,

          reinterpret_cast<V *>(b.velocity) + o,

          reinterpret_cast<V *>(b.goal) + o,

          reinterpret_cast<V *>(b.goalVelocity) + o,

          c.old.data() + o,

          c.force.data() + o,

          b.inverseMass + o,

          c.lambda.data() + actor * L,

          b.detached + o,

          b.contactCount + actor,

          reinterpret_cast<Q *>(b.rotation)[actor],

          b.gain[actor],

          b.damping[actor],

          b.strength[actor],

          b.state[actor]};

  a.tick();

}

void Context::startWorkers(int total) {

  while (int(workers.size()) < total - 1) {

    int index = int(workers.size()) + 1;

    uint64_t initialGeneration = generation;

    workers.emplace_back([this, index, initialGeneration] {

      uint64_t seen = initialGeneration;

      for (;;) {

        std::unique_lock<std::mutex> guard(mutex);

        begin.wait(guard, [&] { return stopping || generation != seen; });

        if (stopping)

          return;

        seen = generation;

        if (index >= active)

          continue;

        int first = jobCount * index / active,

            last = jobCount * (index + 1) / active;

        auto *b = jobBuffers;

        auto *cfg = jobConfig;

        guard.unlock();

        for (int actor = first; actor < last; actor++)

          actorStep(*this, *b, *cfg, actor);

        guard.lock();

        if (--remaining == 0)

          done.notify_one();

      }

    });

  }

}

void Context::step(const OUTMotorNativeBuffers &b,

                   const OUTMotorNativeConfig &cfg, int count) {

  int limit =

      std::min(8, std::max(1, int(std::thread::hardware_concurrency())));

  int wanted = count >= 128 ? std::max(1, std::min(limit, cfg.workerCount)) : 1;

  if (wanted == 1) {

    for (int actor = 0; actor < count; actor++)

      actorStep(*this, b, cfg, actor);

    return;

  }

  startWorkers(wanted);

  {

    std::lock_guard<std::mutex> guard(mutex);

    jobBuffers = &b;

    jobConfig = &cfg;

    jobCount = count;

    active = wanted;

    remaining = wanted - 1;

    ++generation;

  }

  begin.notify_all();

  for (int actor = 0; actor < count / wanted; actor++)

    actorStep(*this, b, cfg, actor);

  std::unique_lock<std::mutex> guard(mutex);

  done.wait(guard, [&] { return remaining == 0; });

}

} // namespace

OUT_API int out_motor_abi() { return 1; }

OUT_API int out_motor_worker_limit() {

  return std::min(8, std::max(1, int(std::thread::hardware_concurrency())));

}

OUT_API void *out_motor_create(int capacity, const float *mass,

                               const float *length, const float *field) {

  if (capacity < 1 || capacity > 65536 || !mass || !length || !field)

    return nullptr;

  try {

    return new Context(capacity, mass, length, field);

  } catch (...) {

    return nullptr;

  }

}

OUT_API void out_motor_destroy(void *context) {

  delete static_cast<Context *>(context);

}

OUT_API int out_motor_step(void *context, const OUTMotorNativeBuffers *b,

                           const OUTMotorNativeConfig *cfg, int count) {

  auto *c = static_cast<Context *>(context);

  if (!c || !b || !cfg || count < 0 || count > c->capacity ||

      cfg->iterations < 1 || cfg->iterations > 16)

    return 0;

  try {

    c->step(*b, *cfg, count);

    return 1;

  } catch (...) {

    return 0;

  }

}
