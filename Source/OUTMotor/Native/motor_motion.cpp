// SPDX-License-Identifier: LicenseRef-OUT-Source-Review-1.0
// Copyright (c) 2026 Алексей Савельев (Alex Merqury). See ../../../LICENSE.
#include "out_motor.hpp"
#include <algorithm>
#include <limits>
namespace out::motor {
namespace {
constexpr float Pi = 3.14159265358979323846f;
float clamp(float v, float lo, float hi) {
    return std::max(lo, std::min(hi, v));
}
float unit(float v) {
    return clamp(v, 0.f, 1.f);
}
constexpr int RecoveryPoses[] = {10, 8, 12, 13, 14, 15, 16, 0};
const char* Names[] = {"Idle relaxed",
                       "Idle alert",
                       "Idle tired",
                       "Idle nervous",
                       "Idle breathing",
                       "Idle look left",
                       "Idle look right",
                       "Idle wounded",
                       "Walk relaxed",
                       "Walk careful",
                       "Walk heavy",
                       "Walk brisk",
                       "Walk limp left",
                       "Walk limp right",
                       "Walk tired",
                       "Walk patrol",
                       "Run jog",
                       "Run sprint",
                       "Run heavy",
                       "Run chase",
                       "Run wounded left",
                       "Run wounded right",
                       "Run escape",
                       "Run athletic",
                       "Crouch settle",
                       "Crouch sneak",
                       "Crouch cover",
                       "Crouch tired",
                       "Crouch brace left",
                       "Crouch brace right",
                       "Crouch inspect",
                       "Crouch recover",
                       "Guard boxing",
                       "Guard close",
                       "Guard wide",
                       "Guard low",
                       "Guard left",
                       "Guard right",
                       "Guard retreat",
                       "Guard flinch",
                       "Strike jab left",
                       "Strike jab right",
                       "Strike hook left",
                       "Strike hook right",
                       "Strike shove",
                       "Strike overhead",
                       "Strike body left",
                       "Strike body right",
                       "Kick front left",
                       "Kick front right",
                       "Kick low left",
                       "Kick low right",
                       "Kick knee left",
                       "Kick knee right",
                       "Kick push left",
                       "Kick push right",
                       "Throw overhand left",
                       "Throw overhand right",
                       "Throw underhand left",
                       "Throw underhand right",
                       "Throw heavy left",
                       "Throw heavy right",
                       "Throw quick left",
                       "Throw quick right",
                       "Hit chest",
                       "Hit back",
                       "Hit shoulder left",
                       "Hit shoulder right",
                       "Hit stomach",
                       "Hit head",
                       "Hit leg left",
                       "Hit leg right",
                       "Fall forward",
                       "Fall backward",
                       "Fall left",
                       "Fall right",
                       "Fall knees",
                       "Fall curl",
                       "Fall stumble left",
                       "Fall stumble right",
                       "Recovery prone left",
                       "Recovery prone right",
                       "Recovery supine left",
                       "Recovery supine right",
                       "Recovery tired left",
                       "Recovery tired right",
                       "Recovery quick left",
                       "Recovery quick right",
                       "Drunk sway",
                       "Drunk stumble",
                       "Drunk swagger",
                       "Drunk stagger",
                       "Drunk lean left",
                       "Drunk lean right",
                       "Drunk argue",
                       "Drunk exhausted",
                       "Sport warmup",
                       "Sport bounce",
                       "Sport spar left",
                       "Sport spar right",
                       "Sport dodge left",
                       "Sport dodge right",
                       "Sport victory",
                       "Sport fatigued",
                       "Zombie shamble",
                       "Zombie reach",
                       "Zombie heavy",
                       "Zombie frantic",
                       "Zombie limp left",
                       "Zombie limp right",
                       "Zombie lurch",
                       "Zombie exhausted",
                       "Police patrol",
                       "Police shield brace",
                       "Police advance",
                       "Police push",
                       "Police guard left",
                       "Police guard right",
                       "Police command",
                       "Police restraint",
                       "Protest march",
                       "Protest chant",
                       "Protest raised arms",
                       "Protest push",
                       "Protest evade left",
                       "Protest evade right",
                       "Protest rally",
                       "Protest cower"};
void index(int n, int size) {
    if (n < 0 || n >= size)
        throw std::out_of_range("Motor bank index");
}
} // namespace
const std::array<int, Links> DataBank::edgeA = {0,  1,  2, 2, 4,  5, 2, 7, 8, 0, 10, 11, 12, 0, 14,
                                                15, 16, 0, 4, 10, 4, 7, 4, 7, 1, 1,  1,  1,  3, 3};
const std::array<int, Links> DataBank::edgeB = {1,  2,  3,  4,  5,  6,  7,  8, 9, 10,
                                                11, 12, 13, 14, 15, 16, 17, 2, 7, 14,
                                                10, 14, 14, 10, 10, 14, 4,  7, 4, 7};
float DataBank::radius(int joint) {
    static constexpr float radii[] = {.12f,  .13f, .16f,  .145f, .075f, .065f, .068f, .075f, .065f,
                                      .068f, .1f,  .078f, .073f, .052f, .1f,   .078f, .073f, .052f};
    index(joint, Joints);
    return radii[joint];
}
int DataBank::signedNibble(const uint8_t* p, size_t bytes, int n) {
    if (!p || n < 0 || size_t(n / 2) >= bytes)
        throw std::out_of_range("Packed int4 index");
    int x = (p[n >> 1] >> ((n & 1) * 4)) & 15;
    return x > 7 ? x - 16 : x;
}
void DataBank::validate() const {
    for (auto p : poses)
        if (!p.finite() || std::max({std::abs(p.x), std::abs(p.y), std::abs(p.z)}) > 10000)
            throw std::invalid_argument("Invalid pose bank");
    for (auto p : field)
        if (!p.finite() || p.length() > 1000)
            throw std::invalid_argument("Invalid vector field");
    for (int i = 0; i < Joints; i++)
        if (!std::isfinite(relativeMass[i]) || relativeMass[i] < 1e-6f || relativeMass[i] > 10000 ||
            mirrored[i] < 0 || mirrored[i] >= Joints || mirrored[mirrored[i]] != i ||
            parents[i] < -1 || parents[i] >= i)
            throw std::invalid_argument("Invalid skeleton metadata");
    for (int k = 0; k < Links; k++)
        if (Vec::distance(pose(0, edgeA[k]), pose(0, edgeB[k])) < .001f)
            throw std::invalid_argument("Degenerate rest constraint");
    for (float v : gain)
        if (!std::isfinite(v) || v < 0 || v > 5)
            throw std::invalid_argument("Invalid gain vocabulary");
    for (float v : damping)
        if (!std::isfinite(v) || v < 0 || v > 10)
            throw std::invalid_argument("Invalid damping vocabulary");
}
Vec DataBank::pose(int p, int joint) const {
    index(p, 17);
    index(joint, Joints);
    return poses[size_t(p) * Joints + joint];
}
Vec DataBank::blend(int p, int q, float t, int j) const {
    return Vec::lerp(pose(p, j), pose(q, j), t);
}
int DataBank::responseCode(int m, int e, int s, int c) const {
    int i = ((m & 3) << 6) | ((e & 3) << 4) | ((s & 3) << 2) | (c & 3);
    return (response[i >> 1] >> ((i & 1) * 4)) & 15;
}
Vec DataBank::sampleField(Vec delta) const {
    if (!delta.finite())
        throw std::invalid_argument("Nonfinite field coordinate");
    Vec q{clamp(delta.x / 1.2f + 1, 0, 2), clamp(delta.y / 1.2f + 1, 0, 2),
          clamp(delta.z / 1.2f + 1, 0, 2)};
    int x = std::min(1, int(q.x)), y = std::min(1, int(q.y)), z = std::min(1, int(q.z)),
        k = (z * 3 + y) * 3 + x;
    float fx = q.x - x, fy = q.y - y, fz = q.z - z;
    return Vec::lerp(Vec::lerp(Vec::lerp(field[k], field[k + 1], fx),
                               Vec::lerp(field[k + 3], field[k + 4], fx), fy),
                     Vec::lerp(Vec::lerp(field[k + 9], field[k + 10], fx),
                               Vec::lerp(field[k + 12], field[k + 13], fx), fy),
                     fz);
}
Quaternion Quaternion::yaw(float degrees) {
    float half = degrees * Pi / 360;
    return {0, std::sin(half), 0, std::cos(half)};
}
Vec Quaternion::rotate(Vec v) const {
    float a = x * 2, b = y * 2, c = z * 2, xx = x * a, yy = y * b, zz = z * c, xy = x * b,
          xz = x * c, yz = y * c, wx = w * a, wy = w * b, wz = w * c;
    return {(1 - (yy + zz)) * v.x + (xy - wz) * v.y + (xz + wy) * v.z,
            (xy + wz) * v.x + (1 - (xx + zz)) * v.y + (yz - wx) * v.z,
            (xz - wy) * v.x + (yz + wx) * v.y + (1 - (xx + yy)) * v.z};
}
MotionCache::MotionCache(std::shared_ptr<const DataBank> data)
    : data_(std::move(data)), frames_(128 * 17 * Joints) {
    if (!data_)
        throw std::invalid_argument("Licensed data bank required");
    data_->validate();
    for (int id = 0; id < 128; id++) {
        Family family = Family(id / 8);
        int v = id % 8;
        bool loop = family != Family::Strike && family != Family::Kick && family != Family::Throw &&
                    family != Family::Hit && family != Family::Fall && family != Family::Recovery;
        float duration = family == Family::Run                                ? .61f
                         : family == Family::Walk                             ? 1.25f
                         : family == Family::Recovery                         ? 5.6f
                         : family == Family::Fall                             ? .9f
                         : family == Family::Strike || family == Family::Kick ? .48f
                         : family == Family::Throw                            ? .65f
                         : family == Family::Hit                              ? .42f
                                                                              : 1.8f;
        duration *= 1 + (v - 3.5f) * .035f;
        float speed = family == Family::Run                                   ? 2.6f
                      : family == Family::Walk                                ? .8f
                      : family == Family::Zombie                              ? .55f
                      : family == Family::Drunk                               ? .45f
                      : family == Family::Police || family == Family::Protest ? .7f
                                                                              : 0;
        float muscle = family == Family::Drunk    ? .68f
                       : family == Family::Zombie ? .78f
                       : family == Family::Fall   ? .18f
                       : family == Family::Hit    ? .72f
                                                  : 1;
        clips_[id] = {Names[id], family, duration, speed, muscle, loop};
        for (int f = 0; f < 17; f++)
            for (int j = 0; j < Joints; j++)
                frames_[(id * 17 + f) * Joints + j] =
                    loop && f == 16 ? frames_[id * 17 * Joints + j] : bake(family, v, f / 16.f, j);
    }
}
const Clip& MotionCache::clip(int id) const {
    index(id, 128);
    return clips_[id];
}
int MotionCache::select(Family f, uint32_t seed) {
    index(int(f), 16);
    seed ^= seed >> 16;
    seed *= 0x7feb352du;
    seed ^= seed >> 15;
    return int(f) * 8 + int(seed & 7);
}
Vec MotionCache::sample(int id, float seconds, int joint) const {
    return normalized(id, seconds / clip(id).duration, joint);
}
Vec MotionCache::normalized(int id, float t, int joint) const {
    auto& c = clip(id);
    index(joint, Joints);
    if (!std::isfinite(t))
        t = 0;
    t = c.looping ? t - std::floor(t) : unit(t);
    float f = t * 16;
    int frame = std::min(int(f), 15), offset = (id * 17 + frame) * Joints + joint;
    return Vec::lerp(frames_[offset], frames_[offset + Joints], f - frame);
}
float MotionCache::footPhase(int id, float t, bool left) {
    index(id, 128);
    if (!std::isfinite(t))
        t = 0;
    float p = t + (left ? 0.f : .5f) + ((id & 1) ? .5f : 0.f);
    return p - std::floor(p);
}
uint8_t MotionCache::contacts(int id, float t) const {
    auto f = clip(id).family;
    if (f != Family::Walk && f != Family::Run && f != Family::Drunk && f != Family::Zombie &&
        f != Family::Police && f != Family::Protest && f != Family::Sport)
        return 3;
    return uint8_t((footPhase(id, t, true) < .6f ? 1 : 0) |
                   (footPhase(id, t, false) < .6f ? 2 : 0));
}

Vec MotionCache::gait(float t, int joint, bool running, float variation) const {
    Vec upper = data_->pose(0, joint);
    float pelvisDrop = running ? .08f : .04f;
    float cycle = t * Pi * 2.f;
    Vec bodyOffset =
        Vec(.015f * std::cos(cycle), -pelvisDrop + .008f * (1.f - std::cos(cycle * 2.f)), 0);
    if (joint < 10) {
        if (joint == 5 || joint == 6 || joint == 8 || joint == 9) {
            int shoulderJoint = joint < 7 ? 4 : 7;
            float side = shoulderJoint == 4 ? -1.f : 1.f;
            float angle = std::cos(cycle) * (side < 0 ? -1.f : 1.f) * (running ? .55f : .30f);
            float forearmAngle = angle + (running ? 1.05f : .30f);
            Vec shoulder = data_->pose(0, shoulderJoint) + bodyOffset;
            float armLength =
                Vec::distance(data_->pose(0, shoulderJoint), data_->pose(0, shoulderJoint + 1));
            float forearmLength =
                Vec::distance(data_->pose(0, shoulderJoint + 1), data_->pose(0, shoulderJoint + 2));
            Vec elbow =
                shoulder +
                Vec(side * .10f, -std::cos(angle), std::sin(angle)).normalized() * armLength;
            return joint == shoulderJoint + 1
                       ? elbow
                       : elbow + Vec(side * .07f, -std::cos(forearmAngle), std::sin(forearmAngle))
                                         .normalized() *
                                     forearmLength;
        }
        return upper + bodyOffset;
    }
    int hipJoint = joint < 14 ? 10 : 14, kneeJoint = hipJoint + 1, ankleJoint = hipJoint + 2;
    Vec hip = data_->pose(0, hipJoint) + bodyOffset;
    if (joint == hipJoint)
        return hip;
    Vec baseKnee = data_->pose(0, kneeJoint), baseAnkle = data_->pose(0, ankleJoint);
    float footPhase = t + (hipJoint == 10 ? 0.f : .5f);
    footPhase -= std::floor(footPhase);
    float stride = (running ? .3f : .22f) * clamp(variation, .85f, 1.1f);
    float clearance = running ? .19f : .13f;
    float z, lift = 0;
    if (footPhase < .6f)
        z = stride * (1.f - 2.f * footPhase / .6f);
    else {
        float swing = (footPhase - .6f) / (1.f - .6f);
        float ease = swing * swing * (3.f - 2.f * swing);
        z = stride * (2.f * ease - 1.f);
        lift = clearance * std::sin(swing * Pi);
    }
    Vec ankle = Vec(baseAnkle.x, std::max(baseAnkle.y, DataBank::radius(ankleJoint)) + lift,
                    baseAnkle.z + z);
    if (joint == ankleJoint)
        return ankle;
    if (joint == hipJoint + 3) {
        Vec toeOffset = data_->pose(0, joint) - baseAnkle;
        return ankle + toeOffset;
    }
    // Bake an anatomically forward-bending knee, preserving the source segment lengths.
    float thigh = Vec::distance(data_->pose(0, hipJoint), baseKnee);
    float shin = Vec::distance(baseKnee, baseAnkle);
    Vec delta = ankle - hip;
    float distance = clamp(delta.length(), .001f, thigh + shin - .001f);
    Vec direction = delta.normalized();
    float along = (thigh * thigh - shin * shin + distance * distance) / (2.f * distance);
    float bend = std::sqrt(std::max(0.f, thigh * thigh - along * along));
    Vec forwardBend = Vec::plane(Vec(0, 0, 1), direction).normalized();
    return hip + direction * along + forwardBend * bend;
}
Vec MotionCache::bake(Family family, int v, float t, int joint) const {
    bool mirror = (v & 1) != 0;
    int j = mirror ? data_->mirrored[joint] : joint;
    float wave = std::sin(t * Pi * 2.f), pulse = std::sin(t * Pi);
    float variation = .78f + v * .065f;
    Vec p = data_->pose(0, j);
    switch (family) {
    case Family::Walk:
    case Family::Run:
        p = gait(t, j, family == Family::Run, variation);
        if (family == Family::Run && (j == 0 || j == 1 || j == 2 || j == 3))
            p.z += .05f * variation;
        if ((v == 4 || v == 5) && j < 10)
            p.x += .025f * wave;
        break;
    case Family::Crouch:
        p = data_->blend(0, 2, .65f + .2f * variation, j);
        break;
    case Family::Guard:
        p = data_->blend(0, 3, .72f + .12f * variation, j);
        break;
    case Family::Strike:
    case Family::Throw:
        p = data_->blend(0, 3, .3f + .4f * pulse, j);
        if (j == 6 || (v == 4 && j == 9))
            p += Vec(-.06f, family == Family::Throw ? .24f * pulse : .08f * pulse,
                     .46f * pulse * variation);
        if (j == 5)
            p.z += .16f * pulse;
        break;
    case Family::Kick:
        p = data_->blend(0, 3, .35f, j);
        if (j == 11)
            p += Vec(0, .16f * pulse, .19f * pulse);
        if (j == 12 || j == 13)
            p += Vec(0, .24f * pulse * variation, .42f * pulse * variation);
        if (j <= 3)
            p.z -= .045f * pulse;
        break;
    case Family::Hit:
        p = data_->blend(0, v == 4 ? 2 : 3, .35f * pulse, j);
        if (j <= 3)
            p += Vec((v == 2 || v == 3 ? .1f : .025f) * pulse, -.06f * pulse,
                     (v == 1 ? .16f : -.12f) * pulse);
        break;
    case Family::Fall:
        p = data_->blend(0, v == 1 ? 11 : v == 4 ? 12 : 10, t * t * (3.f - 2.f * t), j);
        break;
    case Family::Recovery: {
        float path = t * (8 - 1);
        int key = std::min(int(path), 8 - 2);
        int first = key == 0 && (v == 2 || v == 3) ? 11 : RecoveryPoses[key];
        p = data_->blend(first, RecoveryPoses[key + 1], path - key, j);
        break;
    }
    case Family::Drunk:
        p = gait(t, j);
        if (j < 10)
            p.x += (.025f + .004f * v) * wave * unit(p.y);
        if (j == 6 || j == 9)
            p.y += .08f * std::sin(t * Pi * 4.f + j);
        break;
    case Family::Sport:
        p = data_->blend(0, 3, .65f, j);
        p.y += .045f * (1.f - std::cos(t * Pi * 4.f));
        if (j >= 10)
            p = gait(t, j, false, variation);
        if (j == 6 || j == 9)
            p.z += .1f * wave;
        break;
    case Family::Zombie:
        p = gait(t, j);
        if (j == 5 || j == 8)
            p.z += .14f * variation;
        if (j == 6 || j == 9)
            p += Vec(0, .14f + wave * .025f, .38f * variation);
        if (j == 2 || j == 3)
            p.z += .08f;
        break;
    case Family::Police:
        p = data_->blend(0, 3, .7f, j);
        if (j >= 10)
            p = gait(t, j, false, variation);
        else if (v == 0 || v == 2)
            p = Vec::lerp(gait(t, j), p, j >= 4 && j <= 9 ? .8f : .15f);
        if (j == 6)
            p.z += .15f * variation;
        break;
    case Family::Protest:
        p = v == 0 ? gait(t, j) : data_->blend(0, v == 7 ? 2 : 3, .25f, j);
        if (j >= 10)
            p = gait(t, j, false, variation);
        if (j == 6 || j == 9)
            p.y += (v == 2 || v == 6 ? .65f : .16f) + .06f * wave;
        break;
    default:
        if (j == 2 || j == 3)
            p.y += .008f * wave;
        if (j == 3)
            p.x += (v - 3.5f) * .005f;
        break;
    }
    // Small deterministic posture variants, with feet kept at source height.
    // Motion remains synthetic; these offsets do not replace authored mocap clips.
    if (j == 2 || j == 3)
        p.x += (v - 3.5f) * .003f;
    if (j == 6 || j == 9)
        p.z += (v - 3.5f) * .008f;
    if (mirror)
        p.x = -p.x;
    return p;
}
void AuthoredClip::validate() const {
    if (name.empty() || int(family) > 15 || !std::isfinite(duration) || duration <= 0 ||
        !std::isfinite(nominalSpeed) || nominalSpeed < 0 || !std::isfinite(quantization) ||
        quantization <= 0 || quantization > 10000 || frames < 2 || frames > 65536 ||
        (!contacts.empty() && contacts.size() != size_t(frames)))
        throw std::invalid_argument("Invalid authored motion metadata");
    bool floats = poses.size() == size_t(frames) * Joints && packed.empty(),
         integers = packed.size() == size_t(frames) * Joints * 3 && poses.empty();
    if (!floats && !integers)
        throw std::invalid_argument("Supply one complete float or packed int16 clip");
    for (auto p : poses)
        if (!p.finite())
            throw std::invalid_argument("Nonfinite authored target");
    for (auto mask : contacts)
        if (mask > 3)
            throw std::invalid_argument("Invalid contact mask");
}
Vec AuthoredClip::sample(float seconds, int joint) const {
    index(joint, Joints);
    if (!std::isfinite(seconds))
        seconds = 0;
    float t = seconds / duration;
    t = looping ? t - std::floor(t) : unit(t);
    float f = t * (frames - 1);
    int a = std::min(int(f), frames - 2);
    if (!poses.empty())
        return Vec::lerp(poses[size_t(a) * Joints + joint], poses[size_t(a + 1) * Joints + joint],
                         f - a);
    size_t first = (size_t(a) * Joints + joint) * 3, next = first + Joints * 3;
    float blend = f - a;
    return {(packed[first] + (packed[next] - packed[first]) * blend) * quantization,
            (packed[first + 1] + (packed[next + 1] - packed[first + 1]) * blend) * quantization,
            (packed[first + 2] + (packed[next + 2] - packed[first + 2]) * blend) * quantization};
}
uint8_t AuthoredClip::contactMask(float seconds) const {
    if (contacts.empty())
        return 0;
    if (!std::isfinite(seconds))
        seconds = 0;
    float t = seconds / duration;
    t = looping ? t - std::floor(t) : unit(t);
    return contacts[std::min(size_t(std::nearbyint(t * (frames - 1))), contacts.size() - 1)];
}
} // namespace out::motor
