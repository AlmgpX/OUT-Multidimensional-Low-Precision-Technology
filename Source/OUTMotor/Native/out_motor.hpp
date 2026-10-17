// SPDX-License-Identifier: LicenseRef-OUT-Source-Review-1.0
// Copyright (c) 2026 Алексей Савельев (Alex Merqury). See ../../../LICENSE.
#pragma once
#include "OUTMotorNative.h"
#include <array>
#include <cmath>
#include <cstdint>
#include <functional>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

namespace out::motor {
constexpr int Joints = 18, Links = 30;
constexpr float Step = 1.f / 120;
struct Vec {
    float x = 0, y = 0, z = 0;
    Vec() = default;
    Vec(float x, float y, float z) : x(x), y(y), z(z) {}
    Vec operator+(Vec b) const {
        return {x + b.x, y + b.y, z + b.z};
    }
    Vec operator-(Vec b) const {
        return {x - b.x, y - b.y, z - b.z};
    }
    Vec operator-() const {
        return {-x, -y, -z};
    }
    Vec operator*(float s) const {
        return {x * s, y * s, z * s};
    }
    Vec operator/(float s) const {
        return {x / s, y / s, z / s};
    }
    Vec& operator+=(Vec b) {
        *this = *this + b;
        return *this;
    }
    Vec& operator-=(Vec b) {
        *this = *this - b;
        return *this;
    }
    Vec& operator*=(float s) {
        *this = *this * s;
        return *this;
    }
    float squared() const {
        return x * x + y * y + z * z;
    }
    float length() const {
        return std::sqrt(squared());
    }
    Vec normalized() const {
        float n = length();
        return n > 1e-5f ? *this / n : Vec{};
    }
    bool finite() const {
        return std::isfinite(x) && std::isfinite(y) && std::isfinite(z);
    }
    static Vec lerp(Vec a, Vec b, float t) {
        return a + (b - a) * t;
    }
    static float dot(Vec a, Vec b) {
        return a.x * b.x + a.y * b.y + a.z * b.z;
    }
    static Vec cross(Vec a, Vec b) {
        return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
    }
    static float distance(Vec a, Vec b) {
        return (a - b).length();
    }
    static Vec plane(Vec v, Vec n) {
        float d = n.squared();
        return d > 1e-8f ? v - n * (dot(v, n) / d) : v;
    }
};
struct Quaternion {
    float x = 0, y = 0, z = 0, w = 1;
    static Quaternion yaw(float degrees);
    Vec rotate(Vec) const;
    Quaternion operator*(Quaternion) const;
    Quaternion inverse() const;
    static Quaternion fromTo(Vec, Vec);
    static Quaternion slerp(Quaternion, Quaternion, float);
};
struct Settings {
    int iterations = 3, motorHz = 20, workers = 1, maximumEvents = 4096;
    float muscle = 1, damping = 1, support = .9f, air = .18f, arenaHalfSize = 32;
    bool jointLimits = true, selfCollisions = true, bodyCollisions = true, autoRecovery = true;
    void validate() const;
};
enum class State : uint8_t { Active, Falling, Recovering, Passive, Dead, Crawling };
enum class Anatomy : uint8_t { Human, Zombie };
enum class Action : uint8_t { Idle, Walk, Run, Crouch, Guard, Reach, Strike, Throw };
enum class Family : uint8_t {
    Idle,
    Walk,
    Run,
    Crouch,
    Guard,
    Strike,
    Kick,
    Throw,
    Hit,
    Fall,
    Recovery,
    Drunk,
    Sport,
    Zombie,
    Police,
    Protest
};
struct Clip {
    std::string name;
    Family family = Family::Idle;
    float duration = 1, speed = 0, muscle = 1;
    bool looping = true;
};
// The caller supplies licensed data. This repository contains no default pose/response payload.
struct DataBank {
    std::array<Vec, 17 * Joints> poses{};
    std::array<Vec, 27> field{};
    std::array<float, Joints> relativeMass{};
    std::array<int, Joints> mirrored{}, parents{};
    std::array<uint8_t, 128> response{};
    std::array<float, 16> gain{}, damping{};
    void validate() const;
    Vec pose(int p, int joint) const;
    Vec blend(int p, int q, float t, int joint) const;
    Vec sampleField(Vec) const;
    int responseCode(int mode, int error, int speed, int contacts) const;
    static int signedNibble(const uint8_t*, size_t bytes, int index);
    static float radius(int joint);
    static const std::array<int, Links> edgeA, edgeB;
};
class MotionCache {
    std::shared_ptr<const DataBank> data_;
    std::array<Clip, 128> clips_{};
    std::vector<Vec> frames_;
    Vec gait(float, int, bool running = false, float variation = 1) const;
    Vec bake(Family, int, float, int) const;

  public:
    explicit MotionCache(std::shared_ptr<const DataBank>);
    const Clip& clip(int) const;
    static int select(Family, uint32_t seed);
    Vec sample(int, float seconds, int joint) const;
    Vec normalized(int, float time, int joint) const;
    uint8_t contacts(int, float normalizedTime) const;
    static float footPhase(int, float normalizedTime, bool left);
};
struct AuthoredClip {
    std::string name;
    Family family = Family::Idle;
    float duration = 1, nominalSpeed = 0, quantization = .001f;
    bool looping = true;
    int frames = 0;
    std::vector<Vec> poses;
    std::vector<int16_t> packed;
    std::vector<uint8_t> contacts;
    void validate() const;
    Vec sample(float seconds, int joint) const;
    uint8_t contactMask(float seconds) const;
};
class FootPlanner {
    struct Foot {
        Vec planted, liftOff, landing, forward, landingForward;
        float progress = 0, duration = 1;
        uint8_t swinging = 0;
    };
    std::vector<std::array<Foot, 2>> feet_;
    std::vector<uint8_t> initialized_;
    std::shared_ptr<const DataBank> data_;
    float upper_, lower_, toe_;

  public:
    FootPlanner(size_t, std::shared_ptr<const DataBank>);
    void reset(int, const Vec* positions, Quaternion);
    bool unfinished(int) const;
    void apply(int, float dt, float phase, uint8_t contacts, float duration, Vec velocity,
               Quaternion, Vec anchor, Vec* goals, Vec* velocities, const uint8_t* detached,
               bool running, bool authoredSwing = false);
};
class FlowField {
    int width_;
    float cell_;
    Vec origin_;
    std::vector<uint8_t> blocked_;
    std::array<std::vector<uint16_t>, 2> distance_;
    std::vector<int> queue_;
    std::array<int, 2> cell(Vec) const;
    void flood(Vec, std::vector<uint16_t>&);

  public:
    FlowField(int width, float cellSize, Vec centre = {});
    void bake(const std::function<bool(Vec, Vec)>& overlapQuery);
    void rebuild(Vec team0, Vec team1);
    Vec direction(Vec, int team) const;
    bool blocked(Vec) const;
    int blockedCount() const;
    int width() const {
        return width_;
    }
};
enum class EventKind : uint8_t { Impulse, Damage, Sever };
struct Event {
    EventKind kind = EventKind::Impulse;
    int actor = 0, joint = 0;
    Vec impulse;
    float damage = 0;
    uint32_t generation = 0;
};
template <class T> class Ring {
    std::vector<T> items_;
    size_t head_ = 0, tail_ = 0, count_ = 0;

  public:
    explicit Ring(size_t capacity) : items_(capacity) {
        if (!capacity || capacity > 1048576)
            throw std::invalid_argument("Invalid ring capacity");
    }
    bool push(T e) {
        if (count_ == items_.size())
            return false;
        items_[tail_] = e;
        tail_ = (tail_ + 1) % items_.size();
        count_++;
        return true;
    }
    bool pop(T& e) {
        if (!count_)
            return false;
        e = items_[head_];
        head_ = (head_ + 1) % items_.size();
        count_--;
        return true;
    }
    size_t size() const {
        return count_;
    }
};
class Melee;
class Projectiles;
class InjuryScheduler;
class World {
    std::shared_ptr<const DataBank> data_;
    std::shared_ptr<const MotionCache> cache_;
    void* solver_ = nullptr;
    Ring<Event> events_;
    std::vector<uint32_t> generation_;
    std::vector<Vec> anchor_, intent_, start_;
    std::vector<Quaternion> rotation_;
    std::vector<float> elapsed_, phase_, gaitPhase_, gaitRate_, attack_, motorGain_, motorDamp_,
        inverseMass_;
    std::vector<uint8_t> contactCount_, cell_, pending_, dwell_, errorBin_, speedBin_, family_;
    std::vector<uint8_t> recoveryStage_, lastAction_;
    std::vector<float> locomotionBlend_;
    std::array<std::shared_ptr<const AuthoredClip>, 16> authored_{};
    std::shared_ptr<const AuthoredClip> stumbleClip_, swordAttack_, swordIdle_;
    std::vector<float> hurt_, stumble_;
    std::vector<uint8_t> footActive_;
    std::vector<uint32_t> bodyOffsets_, bodyOrder_, bodyCursor_;
    int bodyWidth_ = 0;
    FootPlanner feet_;
    std::array<float, Joints> baseMass_{};
    std::array<float, Links> length_{};
    void updateTarget(int, float);
    void updateResponse(int);
    void updateState(int);
    void checkActor(int) const;
    void bodies();

  public:
    const int capacity;
    Settings settings;
    int frame = 0, recoveries = 0, severed = 0;
    float seconds = 0;
    uint64_t targetUpdates = 0, rejectedEvents = 0;
    int processedEvents = 0, bodyProbes = 0, bodyBudgetReached = 0;
    std::vector<Vec> position, velocity, goal, goalVelocity;
    std::vector<uint8_t> detached, faction, state, anatomy, action, visualArchetype, role;
    std::vector<float> health, mass, strength;
    std::unique_ptr<Melee> melee;
    World(std::shared_ptr<const DataBank>, int count, Settings = {}, size_t eventCapacity = 8192);
    ~World();
    World(const World&) = delete;
    int count() const {
        return capacity;
    }
    const DataBank& bank() const {
        return *data_;
    }
    const MotionCache& cache() const {
        return *cache_;
    }
    Vec joint(int actor, int joint) const;
    Quaternion heading(int) const;
    Vec anchor(int) const;
    uint32_t generation(int) const;
    void spawn(int, Vec origin, float yaw, float massKg, int team);
    bool queue(EventKind, int, int, Vec impulse = {}, float damage = 0);
    void impulse(int, int, Vec);
    void damage(int, int, Vec, float);
    void sever(int, int);
    void setClip(AuthoredClip);
    bool supportLegLoss(int) const;
    void crawl(int);
    void fall(int);
    void recover(int);
    void locomotion(int, Vec direction, float speed);
    void setAction(int, Action);
    void setFamily(int, Family);
    void clearFamily(int);
    void tick();
    bool attacking(int) const;
    bool stumbling(int) const;
    float attackProgress(int) const;
    void beginAttack(int, Action);
    void dropAll(Vec);
    bool valid() const;
    size_t pendingEvents() const {
        return events_.size();
    }
};
class InjuryScheduler {
    std::vector<uint8_t> flags_, stress_;
    int lastFrame_ = -1;

  public:
    explicit InjuryScheduler(size_t capacity);
    int visited = 0, changed = 0;
    std::function<void(int, int)> limbLost;
    void reset();
    uint8_t flags(int) const;
    void tick(World&, bool tear = false, float stretch = 1.8f);
};
enum class Weapon : uint8_t { Auto, Unarmed, Baton, Sword };
enum class MeleePhase : uint8_t { Idle, Windup, Contact, Recovery };
class Melee {
    struct Attack {
        int target = -1;
        uint32_t attackerGeneration = 0, targetGeneration = 0;
        float time = 0, damage = 0, impulse = 0;
        Weapon weapon = Weapon::Auto;
        bool active = false, hit = false;
        Vec previous;
    };
    World& world_;
    std::vector<Attack> attacks_;

  public:
    explicit Melee(World&);
    uint64_t started = 0, hits = 0, misses = 0, canceled = 0;
    int contactTests = 0, lastHitJoint = -1;
    Vec lastHitPoint{};
    float lastHitDamage = 0;
    std::function<void(int, int)> onHit;
    bool begin(int attacker, int target, float damage, float impulse, Weapon = Weapon::Auto);
    void cancel(int);
    void cancelTarget(int);
    int target(int) const;
    MeleePhase phase(int) const;
    void tick(float dt = Step);
};
enum class ProjectileKind : uint8_t { Stone, Arrow, Bullet };
class Projectiles {
    struct Projectile {
        Vec position, velocity;
        float radius = 0, impulse = 0, damage = 0, time = 0;
        int shooter = -1;
        uint32_t generation = 0;
        ProjectileKind kind = ProjectileKind::Stone;
        bool alive = false;
    };
    std::array<Projectile, 256> items_{};
    size_t cursor_ = 0;

  public:
    int active = 0, narrowTests = 0;
    std::function<void(int, int, Vec, float)> onImpact;
    std::function<void(int, int, float)> onDamage;
    std::function<float(Vec from, Vec to, float radius)> obstacleFraction;
    void clear();
    bool fire(World&, Vec spawn, Vec direction, float speed, float impulse, float radius,
              int shooter = -1, ProjectileKind = ProjectileKind::Stone, float damage = 0);
    void tick(World&, float dt);
};
enum class RTSRole : uint8_t { Worker, Melee, Archer, Slinger };
enum class Order : uint8_t { Idle, Move, Gather, ReturnResources, Attack };
enum class ResourceKind : uint8_t { Wood, Stone };
class RTS {
    World& world_;
    Projectiles& projectiles_;
    float now_ = 0, credit_ = 0, recruitClock_ = 0;
    int cursor_ = 0;
    std::vector<float> lastDecision_, gatherTime_, cooldown_;
    std::vector<int> probe_, lastDamager_;
    std::vector<uint8_t> wasAlive_, reserved_;
    bool alive(int) const;
    bool move(int, Vec, float);
    void stop(int);
    void order(int, Order);
    int findEnemy(int);
    int nearestResource(Vec) const;
    void decide(int, float);
    void accountDeath(int);

  public:
    std::vector<RTSRole> roles;
    std::vector<Order> orders;
    std::vector<int> targetActor, targetResource, carried;
    std::vector<ResourceKind> carriedKind, resourceKinds;
    std::vector<Vec> goals, resourcePositions;
    std::vector<int> resourceRemaining;
    std::vector<uint8_t> command, speedCode;
    std::array<Vec, 2> bases{{{-22, 0, 0}, {22, 0, 0}}};
    std::array<int, 2> wood{{60, 60}}, stone{{30, 30}}, kills{}, score{}, delivered{};
    bool enemyAI = true, playerAutoDefend = false;
    int maximumDecisions = 0, lastDecisions = 0, lastProbes = 0;
    uint64_t totalDecisions = 0;
    RTS(World&, Projectiles&, int nodes = 12);
    ~RTS();
    bool reserve(int);
    bool recruit(int team, RTSRole);
    bool setMove(int, Vec);
    bool setGather(int, int node);
    bool setAttack(int, int target);
    void tick(float dt);
    static Vec quantizeGoal(Vec);
};
struct BloodDrop {
    Vec position, velocity;
    float time = 0, size = 0;
    bool stain = false;
};
class BloodPool {
    std::vector<BloodDrop> drops_;
    size_t cursor_ = 0;

  public:
    explicit BloodPool(size_t capacity = 2048);
    void emit(Vec position, Vec normal, float damage, uint32_t seed);
    void tick(float);
    const std::vector<BloodDrop>& drops() const {
        return drops_;
    }
};
struct CapsuleInstance {
    Vec from, to;
    float radius;
    uint8_t team, state;
    int actor, joint;
};
// Renderer-neutral output. GPU submission/materials/skinned meshes remain engine adapter
// responsibilities.
size_t writeCapsules(const World&, CapsuleInstance* destination, size_t capacity);
enum class Equipment : uint8_t { None, Baton, Rifle, Stone, Flag, Sword };
struct Archetype {
    Anatomy anatomy = Anatomy::Human;
    Family motion = Family::Walk;
    float mass = 70, strength = 1, health = 100, speed = .65f, attackInterval = 1, damage = 7,
          impulse = 35;
    Equipment equipment = Equipment::None, variant = Equipment::None;
    int variantEvery = 0;
    void apply(World&, int actor, int variantIndex = 0) const;
};
class Crowd {
    World& world_;
    float brainClock_ = 0, accumulator_ = 0;
    std::vector<float> cooldown_;

  public:
    Projectiles projectiles;
    BloodPool blood;
    InjuryScheduler injuries;
    FlowField navigation;
    bool ai = true, simulate = true, useNavigation = true, enableBlood = true, tearLimbs = false;
    float tearRatio = 1.8f, brainHz = 10;
    std::shared_ptr<const Archetype> archetype;
    Vec teamGoalA{-12, 0, 0}, teamGoalB{12, 0, 0};
    double droppedSeconds = 0, lastSimulationMs = 0;
    explicit Crowd(World&);
    ~Crowd();
    void updateBrains();
    void advance(float engineDelta);
    void refreshNavigation(const std::function<bool(Vec, Vec)>& overlap = {});
    void volley(Vec origin);
};
struct RigPose {
    Vec rootPosition;
    Quaternion rootRotation;
    std::array<Quaternion, Joints> bones{};
};
class RigBinding {
    std::array<Quaternion, Joints> restRotation_{}, current_{};
    std::array<Vec, Joints> restDirection_{};
    std::array<int, Joints> parent_{};
    Vec hipOffset_{};
    bool ready_ = false;

  public:
    void capture(const std::array<Vec, Joints>& worldPositions,
                 const std::array<Quaternion, Joints>& rotations, Vec rootPosition,
                 Quaternion rootRotation, const DataBank&);
    RigPose apply(const World&, int actor, float dt, float smoothing = 0);
};
struct EquipmentInstance {
    Equipment kind;
    Vec position;
    Quaternion rotation;
    int actor;
};
size_t writeEquipment(const World&, EquipmentInstance*, size_t capacity);
} // namespace out::motor
