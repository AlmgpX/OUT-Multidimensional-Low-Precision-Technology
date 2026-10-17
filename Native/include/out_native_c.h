// SPDX-License-Identifier: LicenseRef-OUT-Source-Review-1.0
// Copyright (c) 2026 Алексей Савельев (Alex Merqury). See ../../LICENSE.
#pragma once
#include <stddef.h>
#include <stdint.h>
#if defined(_WIN32)
#define OUT_NATIVE_API __declspec(dllexport)
#else
#define OUT_NATIVE_API __attribute__((visibility("default")))
#endif
#ifdef __cplusplus
extern "C" {
#endif
// Return 1 = completed, 2 = output buffer too small (written = required), 0 = error.
// All handles and pointer spans belong to a trusted host in the same process.
OUT_NATIVE_API int out_native_abi(void);
OUT_NATIVE_API const char* out_native_last_error(void);
typedef struct OUTGuardMovementLimits {
    double maximumSpeed, maximumAcceleration, maximumVerticalSpeed, maximumPredictionError;
    double relativeTolerance, maximumSampleGap, minimumSampleInterval, tableEvidenceThreshold;
} OUTGuardMovementLimits;
typedef struct OUTGuardMovementResult {
    int32_t accepted, hasNormalizedFeatures, skipped, hasEvidence, rebased;
    uint32_t violations;
    int32_t signal;
    double speedRatio, accelerationRatio, verticalRatio, predictionRatio, observed, allowed,
        tableRisk;
} OUTGuardMovementResult;
OUT_NATIVE_API void* out_guard_movement_create(const OUTGuardMovementLimits* limits);
OUT_NATIVE_API void out_guard_movement_destroy(void* tracker);
OUT_NATIVE_API int out_guard_movement_observe(void* tracker, double time, double x, double y,
                                              double z, int trustedTeleport,
                                              OUTGuardMovementResult* result);
OUT_NATIVE_API void* out_guard_codec_create(const uint8_t* key, size_t length);
OUT_NATIVE_API void out_guard_codec_destroy(void* codec);
OUT_NATIVE_API int out_guard_codec_encode(void* codec, const uint8_t* payload, size_t length,
                                          const char* utf8Context, uint8_t* output, size_t capacity,
                                          size_t* written);
OUT_NATIVE_API int out_guard_codec_decode(void* codec, const uint8_t* envelope, size_t length,
                                          const char* utf8Context, uint8_t* output, size_t capacity,
                                          size_t* written);
typedef struct OUTMotorBankView {
    const float* poses;        /* 17 * 18 * 3 floats; no built-in licensed bank */
    const float* field;        /* 27 * 3 floats */
    const float* relativeMass; /* 18 floats */
    const int32_t* mirrored;   /* 18 joint indices */
    const int32_t* parents;    /* 18 joint indices; root = -1 */
    const uint8_t* response;   /* 128 packed bytes */
    const float* gain;         /* 16 floats */
    const float* damping;      /* 16 floats */
} OUTMotorBankView;
OUT_NATIVE_API void* out_motor_world_create(const OUTMotorBankView* bank, int count);
OUT_NATIVE_API void out_motor_world_destroy(void* world);
OUT_NATIVE_API int out_motor_world_tick(void* world);
OUT_NATIVE_API int out_motor_world_workers(void* world, int workers);
OUT_NATIVE_API int out_motor_world_locomotion(void* world, int actor, float x, float z,
                                              float speed);
OUT_NATIVE_API int out_motor_world_event(void* world, int kind, int actor, int joint, float x,
                                         float y, float z, float damage);
OUT_NATIVE_API int out_motor_world_copy_positions(void* world, float* output, size_t floatCapacity,
                                                  size_t* written);
#ifdef __cplusplus
}
#endif
