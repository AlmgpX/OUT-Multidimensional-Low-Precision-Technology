// SPDX-License-Identifier: LicenseRef-OUT-Source-Review-1.0
// Copyright (c) 2026 Алексей Савельев (Alex Merqury). See ../../../LICENSE.
#pragma once
#include <array>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <functional>
#include <limits>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <vector>

namespace out::guard {
using Bytes = std::vector<uint8_t>;
using Key = std::array<uint8_t, 32>;
using Id = std::array<uint8_t, 16>;
Key randomKey();
Key sha256(const uint8_t*, size_t);
Key hmac(const Key&, const uint8_t*, size_t);
std::string hex(const uint8_t*, size_t);
Bytes unhex(const std::string&);
std::string base64(const uint8_t*, size_t);
bool equal(const uint8_t*, const uint8_t*, size_t) noexcept;
void validateText(const std::string&, size_t maximum = 1024, bool allowEmpty = false);
enum class Signal {
    None,
    MemoryTamper,
    SavedDataTamper,
    MovementSpeed,
    MovementAcceleration,
    MovementVertical,
    MovementPattern,
    InvalidSample,
    ClockDrift,
    FileIntegrity,
    ObservationGap,
    TimeScaleMismatch,
    RiskThreshold,
    ClockObservationGap
};
enum class Severity { Notice, Suspicious, High };
struct Evidence {
    Signal signal = Signal::None;
    Severity severity = Severity::Notice;
    std::string subject, detail;
    double observed = 0, allowed = 0, utcSeconds = 0;
};
class Events {
  public:
    using Handler = std::function<void(const Evidence&)>;
    static uint64_t subscribe(Handler);
    static void unsubscribe(uint64_t);
    static void report(Evidence);
};
class Risk {
    double score_ = 0, last_ = 0, half_;
    bool initialized_ = false;

  public:
    explicit Risk(double halfLife = 30);
    double advance(double monotonic, double contribution = 0);
    double score() const noexcept {
        return score_;
    }
    void reset() noexcept {
        score_ = last_ = 0;
        initialized_ = false;
    }
};
struct Binding {
    uint64_t id;
    explicit Binding(const std::string&);
};
class TamperError : public std::runtime_error {
  public:
    TamperError() : std::runtime_error("OUT Guard rejected a modified protected value") {};
};
uint64_t memoryTag(uint64_t encoded, uint64_t key, uint64_t binding);
uint64_t random64();
[[noreturn]] void rejectMemory();
// Runtime obfuscation/tamper evidence; the process owner can still patch callers and secrets.
template <class T> class Protected {
    static_assert(std::is_arithmetic_v<T> && sizeof(T) <= 8, "Supported scalar required");
    uint64_t encoded_ = 0, key_ = 0, tag_ = 0, binding_ = 0;
    uint8_t initialized_ = 0;
    T decode() const {
        if (initialized_ != 1)
            rejectMemory();
        if (tag_ != memoryTag(encoded_, key_, binding_))
            rejectMemory();
        uint64_t bits = encoded_ ^ key_;
        T value{};
        if constexpr (std::is_same_v<T, bool>) {
            if (bits > 1)
                rejectMemory();
            value = bits != 0;
        } else
            std::memcpy(&value, &bits, sizeof(T));
        return value;
    }

  public:
    Protected() = default;
    explicit Protected(T v) {
        assign(v);
    }
    Protected(T v, const Binding& b) {
        assign(v, b.id);
    }
    void assign(T v, uint64_t binding = 0) {
        uint64_t bits = 0;
        if constexpr (std::is_same_v<T, bool>)
            bits = v ? 1 : 0;
        else
            std::memcpy(&bits, &v, sizeof(T));
        binding_ = binding;
        key_ = random64();
        encoded_ = bits ^ key_;
        tag_ = memoryTag(encoded_, key_, binding_);
        initialized_ = true;
    }
    T value() const {
        if (binding_)
            throw std::logic_error("Bound value requires read(expectedBinding)");
        if (!initialized_) {
            if (encoded_ || key_ || tag_)
                rejectMemory();
            return T{};
        }
        return decode();
    }
    T read(const Binding& expected) const {
        if (binding_ != expected.id)
            rejectMemory();
        return decode();
    }
    void rekey() {
        assign(value());
    }
    void rekey(const Binding& b) {
        assign(read(b), b.id);
    }
    void add(T delta, const Binding* expected = nullptr) {
        T current = expected ? read(*expected) : value();
        if constexpr (std::is_integral_v<T> && !std::is_same_v<T, bool>) {
            if constexpr (std::is_signed_v<T>) {
                if ((delta > 0 && current > std::numeric_limits<T>::max() - delta) ||
                    (delta < 0 && current < std::numeric_limits<T>::min() - delta))
                    throw std::overflow_error("Protected addition overflow");
            } else if (current > std::numeric_limits<T>::max() - delta)
                throw std::overflow_error("Protected addition overflow");
            assign(T(current + delta), binding_);
        } else if constexpr (std::is_floating_point_v<T>)
            assign(T(current + delta), binding_);
        else
            throw std::logic_error("Boolean arithmetic is not supported");
    }
    void subtract(T delta, const Binding* expected = nullptr) {
        T current = expected ? read(*expected) : value();
        if constexpr (std::is_integral_v<T> && !std::is_same_v<T, bool>) {
            if constexpr (std::is_signed_v<T>) {
                if ((delta > 0 && current < std::numeric_limits<T>::min() + delta) ||
                    (delta < 0 && current > std::numeric_limits<T>::max() + delta))
                    throw std::overflow_error("Protected subtraction overflow");
            } else if (current < delta)
                throw std::overflow_error("Protected subtraction overflow");
            assign(T(current - delta), binding_);
        } else if constexpr (std::is_floating_point_v<T>)
            assign(T(current - delta), binding_);
        else
            throw std::logic_error("Boolean arithmetic is not supported");
    }
};
using ProtectedLong = Protected<int64_t>;
using ProtectedInt = Protected<int32_t>;
using ProtectedFloat = Protected<float>;
using ProtectedBool = Protected<bool>;
class BehaviorTable {
    std::array<uint8_t, 2048> packed_{};

  public:
    BehaviorTable();
    explicit BehaviorTable(const Bytes&);
    static int bin(double);
    double evaluate(double, double, double, double) const;
    Bytes exportBytes() const {
        return {packed_.begin(), packed_.end()};
    }
};
struct MovementLimits {
    double maximumSpeed = 8, maximumAcceleration = 60, maximumVerticalSpeed = 20,
           maximumPredictionError = 2, relativeTolerance = .1, maximumSampleGap = .5,
           minimumSampleInterval = .001, tableEvidenceThreshold = .8;
    void validate() const;
};
enum Violation : uint32_t {
    None = 0,
    Speed = 1,
    Acceleration = 2,
    Vertical = 4,
    Prediction = 8,
    Policy = 16,
    Gap = 32,
    Invalid = 64
};
struct MovementResult {
    bool accepted = false, hasNormalizedFeatures = false, skipped = false, hasEvidence = false,
         rebased = false;
    uint32_t violations = 0;
    Signal signal = Signal::None;
    double speedRatio = 0, accelerationRatio = 0, verticalRatio = 0, predictionRatio = 0,
           observed = 0, allowed = 0, tableRisk = 0;
};
class MovementTracker {
    MovementLimits limits_;
    std::shared_ptr<const BehaviorTable> table_;
    double x_ = 0, y_ = 0, z_ = 0, time_ = 0, observed_ = 0, vx_ = 0, vy_ = 0, vz_ = 0;
    bool initialized_ = false, velocity_ = false, observedTime_ = false;

  public:
    explicit MovementTracker(MovementLimits = {}, std::shared_ptr<const BehaviorTable> = nullptr);
    MovementTracker(MovementLimits, const BehaviorTable&);
    const BehaviorTable& policy()const noexcept{return *table_;}
    void reset() noexcept {
        initialized_ = velocity_ = observedTime_ = false;
    }
    MovementResult observe(double time, double x, double y, double z, bool trustedTeleport = false);
};
class Clock {
    double engine_ = 0, last_ = 0, start_ = 0, fast_ = 0, slow_ = 0, totalEngine_ = 0,
           totalReference_ = 0;
    bool initialized_ = false, suspended_ = false, skip_ = false;
    int windows_ = 0;
    bool close(double, double&);

  public:
    const double window, tolerance;
    const int required;
    Clock(double window = 5, double tolerance = .15, int required = 3);
    void reset();
    void start(double);
    bool suspend(double, double&);
    void resume(double);
    bool observe(double reference, double delta, double& ratio);
};
bool unexpectedTimeScale(double expected, double actual, double scaled, double unscaled,
                         double tolerance = .05);
enum class TemporalStatus {
    NeedsHistory,
    Ready,
    UnseenState,
    ContextMismatch,
    InvalidObservation,
    UnsupportedInterval
};
enum TemporalReason : uint32_t {
    NoReason = 0,
    StrictLimitExceeded = 1,
    ObservationJitter = 2,
    ConflictingHypotheses = 4,
    LowSupport = 8,
    UnsupportedContext = 16,
    InvalidObservation = 32,
    IntervalGap = 64
};
struct TemporalObservation {
    double time = 0, speedRatio = 0, accelerationRatio = 0, verticalRatio = 0, predictionRatio = 0;
    bool valid() const;
};
struct TemporalAssessment {
    TemporalStatus status = TemporalStatus::NeedsHistory;
    uint32_t reasons = 0;
    double normal = 0, lag = 0, anomaly = 0, uncertain = 0;
    uint8_t support = 0;
    bool reviewSuggested = false;
    Id modelId{};
};
class TemporalModel {
    Bytes bytes_;
    TemporalModel() = default;

  public:
    static constexpr size_t HeaderBytes = 80, TableBytes = 2048, PayloadBytes = 10240,
                            FileBytes = 10352;
    Id modelId{}, contextId{};
    double nominal = 0, maximum = 0, memory = 0, reviewThreshold = 0;
    uint8_t minimumSupport = 0;
    static std::shared_ptr<const TemporalModel> load(const Bytes&, const Id& expectedContext);
    int nibble(int table, int index) const;
    Bytes exportBytes() const {
        return bytes_;
    }
};
class TemporalRisk {
    std::shared_ptr<const TemporalModel> model_;
    double normal_ = .88, lag_ = .07, anomaly_ = .02, uncertain_ = .03, last_ = 0, previous_ = 0,
           change_ = 0;
    bool initialized_ = false, needsReset_ = false;
    TemporalAssessment abstain(TemporalStatus, uint32_t) const;

  public:
    explicit TemporalRisk(std::shared_ptr<const TemporalModel>);
    void reset();
    TemporalAssessment observe(TemporalObservation, const Id& trustedContext);
};
class Codec {
    Key master_;
    bool disposed_ = false;
    Key derive(const uint8_t* salt, const char*) const;

  public:
    static constexpr size_t MaximumPayloadBytes = 8 * 1024 * 1024;
    explicit Codec(Key);
    ~Codec();
    Codec(const Codec&) = delete;
    Codec& operator=(const Codec&) = delete;
    Bytes encode(const Bytes&, const std::string& context) const;
    bool decode(const Bytes&, const std::string& context, Bytes&) const;
    void dispose() noexcept;
    static Key deriveUserKey(const Key& token, const std::string& user,
                             const std::string& application);
    static std::string userScope(const std::string&);
};
enum class LoadStatus { Success, Missing, Invalid, IOError, StaleRevision, UnsupportedPlatform };
class SaveStore {
    std::filesystem::path directory_;
    Codec codec_;
    std::string scope_;

  public:
    SaveStore(std::filesystem::path, Key, const std::string& user = "");
    std::filesystem::path slotPath(const std::string&) const;
    void save(const std::string&, const Bytes&);
    LoadStatus load(const std::string&, Bytes&, bool backup = false) const;
    void saveVersioned(const std::string&, const Bytes&, int64_t revision);
    LoadStatus loadVersioned(const std::string&, int64_t trustedMinimum, Bytes&, int64_t& revision,
                             bool backup = false) const;
    void erase(const std::string&);
};
struct Challenge {
    int64_t sequence = 0;
    std::string nonce;
    double expiresAt = 0;
};
struct Reply {
    int64_t sequence = 0;
    std::string nonce;
    Key digest{}, authentication{};
};
Reply createReply(const Key&, const Challenge&, const Bytes& canonicalState);
enum class ReplyStatus { Accepted, ReplayOrUnknown, Expired, BadAuthentication, StateMismatch };
class ChallengeSession {
    Key key_;
    std::mutex mutex_;
    int64_t sequence_ = 0;
    Challenge pending_;
    bool pendingValid_ = false, hasTime_ = false;
    double last_ = 0;
    void checkTime(double);

  public:
    explicit ChallengeSession(Key);
    ~ChallengeSession();
    Challenge issue(double monotonic, double lifetime = 10);
    ReplyStatus verify(const Reply&, const Bytes& authoritativeState, double monotonic);
};
class DeltaBudget {
    double capacity_, refill_, tokens_, last_ = 0;
    bool initialized_ = false;
    mutable std::mutex mutex_;
    void refill(double);

  public:
    DeltaBudget(double capacity, double refillPerSecond);
    double available() const;
    double refresh(double monotonic);
    bool spend(double trustedCost, double monotonic);
};
enum class FileStatus {
    Valid,
    Missing,
    Modified,
    InvalidPath,
    IOError,
    UnsupportedPlatform,
    UntrustedManifest
};
Bytes signatureRecord(const std::string& relative, const std::string& shaHex,
                      const std::string& release);
FileStatus verifyFile(const std::filesystem::path& root, const std::string& relative,
                      const std::string& expectedSha);
FileStatus verifySignedFile(const std::filesystem::path& root, const std::string& relative,
                            const std::string& sha, const std::string& release,
                            const Bytes& signature, const Bytes& modulus, const Bytes& exponent);
std::string hashFile(const std::filesystem::path&);

struct Vec {
    double x = 0, y = 0, z = 0;
    Vec operator+(Vec b) const {
        return {x + b.x, y + b.y, z + b.z};
    }
    Vec operator-(Vec b) const {
        return {x - b.x, y - b.y, z - b.z};
    }
    Vec operator*(double b) const {
        return {x * b, y * b, z * b};
    }
    bool finite() const;
};
struct Box {
    Vec min, max;
    void validate() const;
};
struct ServerInput {
    int64_t sequence = 0, acknowledgedTick = 0;
    int epoch = 0;
    double moveX = 0, moveZ = 0;
    bool jump = false, dash = false;
};
struct ServerSnapshot {
    int64_t tick = 0, acknowledgedInput = 0, nextInputSequence = 0, nextShotSequence = 0;
    int epoch = 0, health = 0, ammo = 0;
    bool grounded = false, resyncRequired = false;
    Vec position, velocity;
};
enum class InputStatus {
    Queued,
    InvalidInput,
    SequenceMismatch,
    StaleAcknowledgement,
    QueueFull,
    ResyncRequired,
    Dead
};
enum class ShotStatus {
    Hit,
    Miss,
    Blocked,
    InvalidAim,
    SequenceMismatch,
    Cooldown,
    NoAmmo,
    ResyncRequired,
    Dead
};
struct ShotResult {
    ShotStatus status = ShotStatus::Miss;
    std::string hitActor;
    int damage = 0, remainingAmmo = 0;
    int64_t tick = 0;
};
class AuthoritativeWorld {
    struct Impl;
    std::unique_ptr<Impl> impl_;

  public:
    static constexpr double FixedDelta = 1. / 60, Speed = 8, Acceleration = 60, Gravity = 20,
                            JumpSpeed = 7;
    static constexpr int MaximumQueuedInputs = 3, MaximumRewindTicks = 12;
    explicit AuthoritativeWorld(std::vector<Box> staticObstacles, double serverNow = 0);
    ~AuthoritativeWorld();
    AuthoritativeWorld(const AuthoritativeWorld&) = delete;
    void addActor(const std::string&, Vec trustedSpawn, double serverNow);
    void advance(double serverNow);
    ServerSnapshot snapshot(const std::string&, double serverNow);
    ServerSnapshot resynchronize(const std::string&, double serverNow);
    InputStatus submit(const std::string&, ServerInput, double serverNow);
    void setLagCompensation(const std::string&, int trustedTicks);
    ShotResult fire(const std::string&, int64_t sequence, Vec aim, double serverNow);
};
} // namespace out::guard
