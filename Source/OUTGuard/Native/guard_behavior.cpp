// SPDX-License-Identifier: LicenseRef-OUT-Source-Review-1.0
// Copyright (c) 2026 Алексей Савельев (Alex Merqury). See ../../../LICENSE.
#include "out_guard.hpp"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <map>

namespace out::guard {
namespace {
std::mutex eventMutex;
uint64_t nextHandler = 0;
std::map<uint64_t, Events::Handler> handlers;
void require(bool v) {
    if (!v)
        throw std::invalid_argument("OUT Guard: invalid finite parameter");
}
} // namespace
uint64_t Events::subscribe(Handler h) {
    require(bool(h));
    std::lock_guard<std::mutex> lock(eventMutex);
    if (nextHandler == UINT64_MAX)
        throw std::overflow_error("Handler identity exhausted");
    handlers[++nextHandler] = std::move(h);
    return nextHandler;
}
void Events::unsubscribe(uint64_t id) {
    std::lock_guard<std::mutex> lock(eventMutex);
    handlers.erase(id);
}
void Events::report(Evidence e) {
    e.utcSeconds =
        std::chrono::duration<double>(std::chrono::system_clock::now().time_since_epoch()).count();
    std::vector<Handler> copy;
    {
        std::lock_guard<std::mutex> lock(eventMutex);
        for (auto& item : handlers)
            copy.push_back(item.second);
    }
    for (auto& callback : copy)
        try {
            callback(e);
        } catch (...) { /* A failed subscriber cannot authorize rejected data. */
        }
}
Risk::Risk(double halfLife) : half_(halfLife) {
    require(std::isfinite(half_) && half_ > 0);
}
double Risk::advance(double t, double contribution) {
    require(std::isfinite(t) && std::isfinite(contribution) && contribution >= 0 &&
            (!initialized_ || t >= last_));
    if (initialized_)
        score_ *= std::pow(.5, (t - last_) / half_);
    initialized_ = true;
    last_ = t;
    score_ = std::min(100., score_ + contribution);
    return score_;
}
Binding::Binding(const std::string& identity) {
    validateText(identity);
    auto digest = sha256(reinterpret_cast<const uint8_t*>(identity.data()), identity.size());
    id = 0;
    for (int i = 0; i < 8; i++)
        id |= uint64_t(digest[i]) << (i * 8);
    if (!id)
        id = 1;
}
[[noreturn]] void rejectMemory() {
    Events::report({Signal::MemoryTamper, Severity::High, "Protected value",
                    "Integrity check failed; value rejected."});
    throw TamperError();
}
BehaviorTable::BehaviorTable() {
    for (int i = 0; i < 4096; i++) {
        int a = i & 7, b = (i >> 3) & 7, c = (i >> 6) & 7, d = (i >> 9) & 7;
        int excess = std::max(0, std::max({a, b, c, d}) - 4),
            corroboration = (a >= 5) + (b >= 5) + (c >= 5) + (d >= 5);
        int profile = std::min(15, excess * 3 + std::max(0, corroboration - 1) * 2);
        packed_[i >> 1] |= uint8_t(profile << ((i & 1) * 4));
    }
}
BehaviorTable::BehaviorTable(const Bytes& packed) {
    require(packed.size() == packed_.size());
    std::copy(packed.begin(), packed.end(), packed_.begin());
}
int BehaviorTable::bin(double v) {
    require(std::isfinite(v) && v >= 0);
    return v >= 1.75 ? 7 : int(v * 4);
}
double BehaviorTable::evaluate(double s, double a, double v, double p) const {
    int i = bin(s) | (bin(a) << 3) | (bin(v) << 6) | (bin(p) << 9);
    return ((packed_[i >> 1] >> ((i & 1) * 4)) & 15) / 15.;
}
void MovementLimits::validate() const {
    for (double v : {maximumSpeed, maximumAcceleration, maximumVerticalSpeed,
                     maximumPredictionError, maximumSampleGap, minimumSampleInterval})
        require(std::isfinite(v) && v > 0);
    require(std::isfinite(relativeTolerance) && relativeTolerance >= 0 && relativeTolerance <= 1 &&
            maximumSampleGap >= minimumSampleInterval && std::isfinite(tableEvidenceThreshold) &&
            tableEvidenceThreshold > 0 && tableEvidenceThreshold <= 1);
}
MovementTracker::MovementTracker(MovementLimits limits, std::shared_ptr<const BehaviorTable> table)
    : limits_(limits), table_(std::move(table)) {
    limits_.validate();
    if(!table_){static const auto sharedDefault=std::make_shared<const BehaviorTable>();table_=sharedDefault;}
}
MovementTracker::MovementTracker(MovementLimits limits,const BehaviorTable& table):MovementTracker(limits,std::make_shared<const BehaviorTable>(table)){}
MovementResult MovementTracker::observe(double t, double x, double y, double z, bool teleport) {
    auto invalid = []() {
        MovementResult r;
        r.hasEvidence = true;
        r.signal = Signal::InvalidSample;
        r.violations = Invalid;
        return r;
    };
    if (!std::isfinite(t))
        return invalid();
    if (observedTime_ && t <= observed_) {
        auto r = invalid();
        r.observed = t;
        r.allowed = observed_;
        return r;
    }
    observed_ = t;
    observedTime_ = true;
    if (!std::isfinite(x) || !std::isfinite(y) || !std::isfinite(z))
        return invalid();
    double dt = initialized_ ? t - time_ : 0;
    if (!initialized_ || teleport) {
        initialized_ = true;
        velocity_ = false;
        time_ = t;
        x_ = x;
        y_ = y;
        z_ = z;
        MovementResult r;
        r.accepted = r.rebased = true;
        return r;
    }
    if (dt > limits_.maximumSampleGap) {
        MovementResult r;
        r.hasEvidence = true;
        r.signal = Signal::ObservationGap;
        r.violations = Gap;
        r.observed = dt;
        r.allowed = limits_.maximumSampleGap;
        return r;
    }
    if (dt < limits_.minimumSampleInterval) {
        MovementResult r;
        r.skipped = true;
        return r;
    }
    double dx = x - x_, dy = y - y_, dz = z - z_, nx = dx / dt, ny = dy / dt, nz = dz / dt,
           speed = std::sqrt(nx * nx + nz * nz), ax = (nx - vx_) / dt, az = (nz - vz_) / dt,
           acceleration = velocity_ ? std::sqrt(ax * ax + az * az) : 0;
    double px = dx - vx_ * dt, py = dy - vy_ * dt, pz = dz - vz_ * dt,
           prediction = velocity_ ? std::sqrt(px * px + py * py + pz * pz) : 0;
    if (!std::isfinite(speed) || !std::isfinite(acceleration) || !std::isfinite(prediction) ||
        !std::isfinite(ny))
        return invalid();
    double tolerance = 1 + limits_.relativeTolerance;
    MovementResult r;
    r.hasNormalizedFeatures = true;
    r.speedRatio = speed / (limits_.maximumSpeed * tolerance);
    r.accelerationRatio = acceleration / (limits_.maximumAcceleration * tolerance);
    r.verticalRatio = std::abs(ny) / (limits_.maximumVerticalSpeed * tolerance);
    r.predictionRatio = prediction / (limits_.maximumPredictionError * tolerance);
    r.tableRisk =
        table_->evaluate(r.speedRatio, r.accelerationRatio, r.verticalRatio, r.predictionRatio);
    if (r.speedRatio > 1)
        r.violations |= Speed;
    if (r.verticalRatio > 1)
        r.violations |= Vertical;
    if (r.accelerationRatio > 1)
        r.violations |= Acceleration;
    if (r.predictionRatio > 1)
        r.violations |= Prediction;
    if (r.tableRisk >= limits_.tableEvidenceThreshold)
        r.violations |= Policy;
    auto evidence = [&](Signal s, double observed, double allowed) {
        r.hasEvidence = true;
        r.signal = s;
        r.observed = observed;
        r.allowed = allowed;
    };
    if (r.speedRatio > 1)
        evidence(Signal::MovementSpeed, speed, limits_.maximumSpeed * tolerance);
    else if (r.verticalRatio > 1)
        evidence(Signal::MovementVertical, std::abs(ny), limits_.maximumVerticalSpeed * tolerance);
    else if (r.accelerationRatio > 1)
        evidence(Signal::MovementAcceleration, acceleration,
                 limits_.maximumAcceleration * tolerance);
    else if (r.predictionRatio > 1)
        evidence(Signal::MovementPattern, prediction, limits_.maximumPredictionError * tolerance);
    else if (r.tableRisk >= limits_.tableEvidenceThreshold)
        evidence(Signal::MovementPattern, r.tableRisk, limits_.tableEvidenceThreshold);
    if (!r.hasEvidence) {
        r.accepted = true;
        time_ = t;
        x_ = x;
        y_ = y;
        z_ = z;
        vx_ = nx;
        vy_ = ny;
        vz_ = nz;
        velocity_ = true;
    }
    return r;
}
Clock::Clock(double w, double tol, int r) : window(w), tolerance(tol), required(r) {
    require(std::isfinite(w) && w >= .1 && std::isfinite(tol) && tol > 0 && tol < 1 && r >= 1);
}
void Clock::reset() {
    initialized_ = suspended_ = skip_ = false;
    engine_ = fast_ = slow_ = totalEngine_ = totalReference_ = 0;
    windows_ = 0;
}
void Clock::start(double t) {
    require(std::isfinite(t));
    reset();
    initialized_ = true;
    last_ = start_ = t;
}
bool Clock::suspend(double t, double& ratio) {
    ratio = 1;
    if (!initialized_ || suspended_ || !std::isfinite(t) || t < last_) {
        suspended_ = true;
        return false;
    }
    bool detected = !skip_ && t > start_ && close(t, ratio);
    suspended_ = true;
    last_ = start_ = t;
    engine_ = 0;
    return detected;
}
void Clock::resume(double t) {
    require(std::isfinite(t));
    initialized_ = true;
    suspended_ = false;
    skip_ = true;
    last_ = start_ = t;
    engine_ = 0;
}
bool Clock::observe(double t, double dt, double& ratio) {
    ratio = 1;
    if (suspended_ || !std::isfinite(t) || !std::isfinite(dt) || dt < 0)
        return false;
    if (skip_) {
        skip_ = false;
        last_ = start_ = t;
        engine_ = 0;
        return false;
    }
    if (!initialized_) {
        initialized_ = true;
        last_ = start_ = t;
        return false;
    }
    double gap = t - last_;
    if (gap < 0 || gap > std::max(2., window * 2)) {
        last_ = start_ = t;
        engine_ = 0;
        return false;
    }
    last_ = t;
    engine_ += dt;
    if (t - start_ < window)
        return false;
    return close(t, ratio);
}
bool Clock::close(double t, double& ratio) {
    double elapsed = t - start_;
    if (elapsed <= 0) {
        ratio = 1;
        return false;
    }
    ratio = engine_ / elapsed;
    fast_ = std::max(0., fast_ + engine_ - elapsed * (1 + tolerance));
    slow_ = std::max(0., slow_ + elapsed * (1 - tolerance) - engine_);
    totalEngine_ += engine_;
    totalReference_ += elapsed;
    engine_ = 0;
    start_ = t;
    windows_ = std::min(required, windows_ + 1);
    if (windows_ < required || std::max(fast_, slow_) <= window * tolerance)
        return false;
    ratio = totalEngine_ / totalReference_;
    windows_ = 0;
    fast_ = slow_ = totalEngine_ = totalReference_ = 0;
    return true;
}
bool unexpectedTimeScale(double expected, double actual, double scaled, double unscaled,
                         double tolerance) {
    for (double v : {expected, actual, scaled, unscaled, tolerance})
        if (!std::isfinite(v) || v < 0)
            return true;
    if (tolerance <= 0)
        return true;
    double allowance = tolerance * std::max(1., expected);
    return std::abs(actual - expected) > allowance ||
           (unscaled > 0 && scaled / unscaled > expected + allowance);
}
bool TemporalObservation::valid() const {
    return std::isfinite(time) && std::isfinite(speedRatio) && speedRatio >= 0 &&
           std::isfinite(accelerationRatio) && accelerationRatio >= 0 &&
           std::isfinite(verticalRatio) && verticalRatio >= 0 && std::isfinite(predictionRatio) &&
           predictionRatio >= 0;
}
namespace {
uint64_t le(const Bytes& b, size_t at, size_t n) {
    uint64_t v = 0;
    for (size_t i = 0; i < n; i++)
        v |= uint64_t(b.at(at + i)) << (i * 8);
    return v;
}
double real(const Bytes& b, size_t at) {
    uint64_t v = le(b, at, 8);
    double d;
    std::memcpy(&d, &v, 8);
    return d;
}
} // namespace
std::shared_ptr<const TemporalModel> TemporalModel::load(const Bytes& source, const Id& context) {
    require(source.size() == FileBytes);
    auto m = std::shared_ptr<TemporalModel>(new TemporalModel);
    m->bytes_ = source;
    auto digest = sha256(m->bytes_.data(), HeaderBytes + PayloadBytes);
    require(equal(digest.data(), m->bytes_.data() + HeaderBytes + PayloadBytes, 32));
    auto& b = m->bytes_;
    require(le(b, 0, 4) == 0x5254474f && le(b, 4, 2) == 1 && le(b, 6, 2) == 1);
    std::copy_n(b.begin() + 8, 16, m->modelId.begin());
    std::copy_n(b.begin() + 24, 16, m->contextId.begin());
    require(m->modelId != Id{} && m->contextId != Id{} && m->contextId == context);
    m->nominal = real(b, 40);
    m->maximum = real(b, 48);
    m->memory = real(b, 56);
    m->reviewThreshold = le(b, 64, 2) / 65535.;
    m->minimumSupport = b[66];
    require(b[67] == 0 && le(b, 68, 2) == 4096 && b[70] == 4 && b[71] == 0 &&
            le(b, 72, 4) == PayloadBytes && le(b, 76, 4) == 0);
    require(std::isfinite(m->nominal) && m->nominal >= .001 && m->nominal <= 1 &&
            std::isfinite(m->maximum) && m->maximum >= m->nominal && m->maximum <= 10 &&
            std::isfinite(m->memory) && m->memory >= .01 && m->memory <= 10 &&
            m->minimumSupport >= 1 && m->minimumSupport <= 15);
    return m;
}
int TemporalModel::nibble(int table, int i) const {
    require(table >= 0 && table < 5 && i >= 0 && i < 4096);
    return (bytes_[HeaderBytes + table * TableBytes + (i >> 1)] >> ((i & 1) * 4)) & 15;
}
TemporalRisk::TemporalRisk(std::shared_ptr<const TemporalModel> m) : model_(std::move(m)) {
    require(bool(model_));
}
void TemporalRisk::reset() {
    initialized_ = needsReset_ = false;
    normal_ = .88;
    lag_ = .07;
    anomaly_ = .02;
    uncertain_ = .03;
    last_ = previous_ = change_ = 0;
}
TemporalAssessment TemporalRisk::abstain(TemporalStatus s, uint32_t reason) const {
    TemporalAssessment a;
    a.status = s;
    a.reasons = reason;
    a.normal = normal_;
    a.lag = lag_;
    a.anomaly = anomaly_;
    a.uncertain = uncertain_;
    a.modelId = model_->modelId;
    return a;
}
TemporalAssessment TemporalRisk::observe(TemporalObservation o, const Id& context) {
    if (context != model_->contextId)
        return abstain(TemporalStatus::ContextMismatch, UnsupportedContext);
    if (!o.valid())
        return abstain(TemporalStatus::InvalidObservation, InvalidObservation);
    if (needsReset_)
        return abstain(TemporalStatus::UnsupportedInterval, IntervalGap);
    if (!initialized_) {
        initialized_ = true;
        last_ = o.time;
        previous_ = o.speedRatio;
        return abstain(TemporalStatus::NeedsHistory, NoReason);
    }
    double dt = o.time - last_;
    if (!std::isfinite(dt) || dt <= 0)
        return abstain(TemporalStatus::InvalidObservation, InvalidObservation);
    if (dt < model_->nominal * .1 || dt > model_->maximum) {
        needsReset_ = true;
        return abstain(TemporalStatus::UnsupportedInterval, IntervalGap);
    }
    double persistence = std::exp(-dt / model_->memory),
           delta = std::abs(std::min(16., o.speedRatio) - std::min(16., previous_));
    change_ = persistence * change_ + (1 - persistence) * std::min(16., delta * 4);
    previous_ = o.speedRatio;
    last_ = o.time;
    auto bin = [](double v) { return v <= 0 ? 0 : v >= .875 ? 7 : int(v * 8); };
    int index =
        bin(o.speedRatio / 1.75) |
        (bin(std::max({o.accelerationRatio, o.verticalRatio, o.predictionRatio}) / 1.75) << 3) |
        (bin((dt / model_->nominal - .8) / 3.2) << 6) | (bin(change_) << 9);
    uint8_t support = uint8_t(model_->nibble(4, index));
    if (support < model_->minimumSupport) {
        uncertain_ = 1;
        normal_ = lag_ = anomaly_ = 0;
        auto a = abstain(TemporalStatus::UnseenState, LowSupport);
        a.support = support;
        return a;
    }
    double* probabilities[] = {&normal_, &lag_, &anomaly_, &uncertain_};
    double prior[] = {.88, .07, .02, .03}, sum = 0;
    for (int i = 0; i < 4; i++) {
        *probabilities[i] = (*probabilities[i] * persistence + (1 - persistence) * prior[i]) *
                            std::exp((model_->nibble(i, index) - 15) / 2.);
        sum += *probabilities[i];
    }
    for (auto p : probabilities)
        *p /= sum;
    auto a = abstain(TemporalStatus::Ready, NoReason);
    a.support = support;
    a.reviewSuggested = anomaly_ > model_->reviewThreshold;
    if (std::max({o.speedRatio, o.accelerationRatio, o.verticalRatio, o.predictionRatio}) > 1)
        a.reasons |= StrictLimitExceeded;
    if (dt / model_->nominal > 1.5)
        a.reasons |= ObservationJitter;
    if (anomaly_ > .2 && (normal_ > .2 || lag_ > .2 || uncertain_ > .2))
        a.reasons |= ConflictingHypotheses;
    return a;
}
} // namespace out::guard
