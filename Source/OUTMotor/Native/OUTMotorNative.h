// SPDX-License-Identifier: LicenseRef-OUT-Source-Review-1.0
// Copyright (c) 2026 Алексей Савельев (Alex Merqury / AlexMerq). All rights reserved.
// Source review only. Project use and AI training require separate applicable licenses. See LICENSE.
#pragma once

#ifdef _WIN32

#define OUT_API extern "C" __declspec(dllexport)

#else

#define OUT_API extern "C" __attribute__((visibility("default")))

#endif

struct OUTMotorNativeConfig {

  float muscleStrength, damping, supportAssistance, airResistance;

  int iterations, jointLimits, selfCollisions, workerCount;

};

struct OUTMotorNativeBuffers {

  float *position, *velocity, *goal, *goalVelocity, *inverseMass;

  unsigned char *detached, *contactCount;

  float *rotation, *gain, *damping, *strength;

  unsigned char *state;

};

OUT_API int out_motor_abi();

OUT_API int out_motor_worker_limit();

OUT_API void *out_motor_create(int capacity, const float *mass,

                               const float *length, const float *field);

OUT_API void out_motor_destroy(void *context);

OUT_API int out_motor_step(void *context, const OUTMotorNativeBuffers *buffers,

                           const OUTMotorNativeConfig *config, int count);
