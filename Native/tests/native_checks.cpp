// SPDX-License-Identifier: LicenseRef-OUT-Source-Review-1.0
// Copyright (c) 2026 Алексей Савельев (Alex Merqury). See ../../LICENSE.
#include "out_guard.hpp"
#include "out_motor.hpp"
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdlib>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
#include <new>
#include <set>
#include <thread>
std::atomic<uint64_t> nativeAllocations{0};
void* operator new(std::size_t n) {
    if (void* p = std::malloc(n ? n : 1)) {
        nativeAllocations.fetch_add(1, std::memory_order_relaxed);
        return p;
    }
    throw std::bad_alloc();
}
void* operator new[](std::size_t n) {
    return ::operator new(n);
}
void operator delete(void* p) noexcept {
    std::free(p);
}
void operator delete[](void* p) noexcept {
    std::free(p);
}
void operator delete(void* p, std::size_t) noexcept {
    std::free(p);
}
void operator delete[](void* p, std::size_t) noexcept {
    std::free(p);
}
namespace g = out::guard;
namespace m = out::motor;
namespace {
int checks = 0;
void require(bool p, const char* name) {
    if (!p)
        throw std::runtime_error(name);
    checks++;
}
template <class F> bool rejects(F f) {
    try {
        f();
        return false;
    } catch (const std::exception&) {
        return true;
    }
}
bool near(double a, double b, double epsilon = 1e-9) {
    return std::abs(a - b) <= epsilon;
}
// Small synthetic regression fixture. No licensed pose/field/response dataset is used.
std::shared_ptr<m::DataBank> fixture() {
    auto b = std::make_shared<m::DataBank>();
    b->parents.fill(-1);
    for (int i = 0; i < 17; i++)
        b->parents[m::DataBank::edgeB[i]] = m::DataBank::edgeA[i];
    for (int j = 0; j < m::Joints; j++) {
        b->relativeMass[j] = 1;
        b->mirrored[j] = j;
        if (j >= 4 && j <= 6)
            b->mirrored[j] = j + 3;
        else if (j >= 7 && j <= 9)
            b->mirrored[j] = j - 3;
        else if (j >= 10 && j <= 13)
            b->mirrored[j] = j + 4;
        else if (j >= 14)
            b->mirrored[j] = j - 4;
    }
    for (int p = 0; p < 17; p++)
        for (int j = 0; j < m::Joints; j++) {
            m::Vec v;
            if (j < 4)
                v = {0, .95f + j * .24f, 0};
            else if (j < 10) {
                int k = (j - 4) % 3;
                float side = j < 7 ? -1.f : 1.f;
                v = {side * (.2f + k * .2f), 1.45f - k * .2f, .02f * k};
            } else {
                int k = (j - 10) % 4;
                float side = j < 14 ? -1.f : 1.f;
                v = {side * .13f,
                     k == 0   ? .92f
                     : k == 1 ? .5f
                     : k == 2 ? .085f
                              : .055f,
                     k == 3   ? .18f
                     : k == 1 ? .02f
                              : 0};
            }
            if (p == 2)
                v.y *= .72f;
            if (p >= 8 && p <= 15) {
                float y = v.y;
                v.y = .12f + std::abs(v.x) * .2f;
                v.z += y * .72f;
            }
            b->poses[size_t(p) * m::Joints + j] = v;
        }
    for (int z = 0; z < 3; z++)
        for (int y = 0; y < 3; y++)
            for (int x = 0; x < 3; x++)
                b->field[(z * 3 + y) * 3 + x] = {-(x - 1) * 1.2f, -(y - 1) * 1.2f, -(z - 1) * 1.2f};
    b->response.fill(0x44);
    b->gain.fill(.6f);
    b->damping.fill(1.4f);
    b->validate();
    return b;
}
void guardChecks() {
    g::Risk risk;
    require(near(risk.advance(0, 40), 40), "Risk contribution");
    require(near(risk.advance(30), 20), "Risk half life");
    require(rejects([&] { risk.advance(29); }), "Risk rejects reversed time");
    g::BehaviorTable table;
    require(table.exportBytes().size() == 2048, "Packed behavior footprint");
    for (int a = 0; a < 8; a++)
        for (int b = 0; b < 8; b++)
            for (int c = 0; c < 8; c++)
                for (int d = 0; d < 8; d++) {
                    int count = (a >= 5) + (b >= 5) + (c >= 5) + (d >= 5),
                        profile = std::min(15, std::max(0, std::max({a, b, c, d}) - 4) * 3 +
                                                   std::max(0, count - 1) * 2);
                    require(near(table.evaluate(a * .25, b * .25, c * .25, d * .25), profile / 15.),
                            "Behavior 4D reference formula");
                }
    g::MovementTracker movement;g::MovementTracker sharedTracker;require(&movement.policy()==&sharedTracker.policy(),"Default 2048-byte Guard policy is shared across actors");
    require(movement.observe(0, 0, 0, 0).accepted, "Initial trusted baseline");
    auto bad = movement.observe(.1, 100, 100, 0);
    require(!bad.accepted && (bad.violations & g::Speed) && (bad.violations & g::Vertical),
            "Simultaneous movement violations");
    require(movement.observe(.2, 1, 0, 0).accepted, "Rejected sample never poisons baseline");
    require(!movement.observe(1, 2, 0, 0).accepted, "Gap cannot authorize teleport");
    require(movement.observe(1.1, 2, 0, 0, true).rebased, "Trusted teleport is explicit");
    require(!movement.observe(1.1, 2, 0, 0).accepted, "Time replay rejected");
    g::Clock clock(1, .1, 3);
    double ratio = 1;
    clock.start(0);
    bool detected = false;
    for (int i = 1; i <= 40; i++)
        detected |= clock.observe(i * .1, .2, ratio);
    require(detected, "Fast engine clock detected");
    require(!g::unexpectedTimeScale(.2, .2, .02, .1), "Declared slow motion valid");
    require(g::unexpectedTimeScale(1, 2, .2, .1), "Unexpected game scale");
    auto key = g::randomKey();
    g::Codec codec(key);
    g::Bytes input = {'a', 0, 'z'}, decoded;
    auto bytes = codec.encode(input, "save/a");
    require(codec.decode(bytes, "save/a", decoded) && decoded == input,
            "Authenticated save roundtrip");
    require(!codec.decode(bytes, "save/b", decoded) && decoded.empty(), "Cross-slot save rejected");
    for (size_t i = 0; i < bytes.size(); i++) {
        auto changed = bytes;
        changed[i] ^= 1;
        require(!codec.decode(changed, "save/a", decoded) && decoded.empty(),
                "Every envelope byte authenticated/validated");
    }
    require(rejects([] { g::validateText(std::string("\xc0\xaf", 2)); }), "Overlong UTF8 rejected");
    require(rejects([] { g::validateText(std::string("\xed\xa0\x80", 3)); }),
            "UTF8 surrogate rejected");
    g::validateText("Алексей / 日本語");
    checks++;
    require(g::Codec::deriveUserKey(key, "alice", "app") !=
                g::Codec::deriveUserKey(key, "bob", "app"),
            "User key derivation separated");
    g::Binding money("player/gold"), health("player/health");
    g::ProtectedInt value(42, money);
    require(value.read(money) == 42, "Bound protected value");
    require(rejects([&] { value.read(health); }), "Field substitution rejected");
    auto edited = value;
    reinterpret_cast<uint8_t*>(&edited)[0] ^= 1;
    require(rejects([&] { edited.read(money); }), "Memory edit rejected");
    g::ProtectedInt zero;
    require(zero.value() == 0 && rejects([&] { zero.read(money); }),
            "Zero default cannot forge bound value");
    int callbacks = 0;
    auto subscription = g::Events::subscribe([&](const g::Evidence&) {
        callbacks++;
        throw std::runtime_error("Subscriber failure");
    });
    require(rejects([&] { edited.read(money); }) && callbacks == 1,
            "Subscriber failure cannot authorize tamper");
    g::Events::unsubscribe(subscription);
    g::ChallengeSession session(key);
    auto challenge = session.issue(0);
    auto reply = g::createReply(key, challenge, input), invalid = reply;
    invalid.authentication[0] ^= 1;
    require(session.verify(invalid, input, 1) == g::ReplyStatus::BadAuthentication,
            "Invalid MAC does not consume challenge");
    require(session.verify(reply, input, 2) == g::ReplyStatus::Accepted, "Challenge accepted");
    require(session.verify(reply, input, 3) == g::ReplyStatus::ReplayOrUnknown,
            "Consumed challenge cannot replay");
    auto expired = session.issue(4, 1);
    require(session.verify(g::createReply(key, expired, input), input, 5) ==
                g::ReplyStatus::Expired,
            "Challenge expires inclusively");
    g::DeltaBudget budget(100, 10);
    std::atomic<int> successful{0};
    std::vector<std::thread> threads;
    for (int t = 0; t < 8; t++)
        threads.emplace_back([&] {
            for (int i = 0; i < 100; i++)
                if (budget.spend(1, 0))
                    successful++;
        });
    for (auto& thread : threads)
        thread.join();
    require(successful == 100, "Atomic budget cannot overspend");
    require(near(budget.refresh(1), 10), "Explicit budget refill");
    require(!budget.spend(-1, 2), "Negative costs rejected");
    auto root = std::filesystem::temp_directory_path() /
                std::filesystem::path("OUT-Native-" + g::hex(key.data(), 8));
    std::filesystem::create_directories(root);
    g::SaveStore store(root, key, "alice");
    store.saveVersioned("profile", input, 4);
    store.saveVersioned("profile", g::Bytes{9}, 5);
    int64_t revision = -1;
    require(store.loadVersioned("profile", 5, decoded, revision) == g::LoadStatus::Success &&
                decoded == g::Bytes{9} && revision == 5,
            "Versioned atomic save");
    require(store.loadVersioned("profile", 5, decoded, revision, true) ==
                g::LoadStatus::StaleRevision,
            "Trusted floor rejects old backup");
    auto path = store.slotPath("profile");
    std::string hash = g::hashFile(path);
    require(g::verifyFile(root, path.filename().u8string(), hash) == g::FileStatus::Valid,
            "File integrity hash");
    require(g::verifyFile(root, "../outside", hash) == g::FileStatus::InvalidPath,
            "Integrity root containment");
    store.erase("profile");
    std::filesystem::remove(root);
    std::vector<g::Box> walls{{{-50, -1, -50}, {50, 0, 50}}, {{2, 0, -2}, {2.02, 5, 2}}};
    g::AuthoritativeWorld world(walls);
    world.addActor("a", {0, .9, 0}, 0);
    auto s = world.snapshot("a", 0);
    for (int tick = 1; tick <= 120; tick++) {
        auto now = (tick - 1) / 60.;
        s = world.snapshot("a", now);
        require(world.submit("a", {s.nextInputSequence, s.tick, s.epoch, 1, 0, false, false},
                             now) == g::InputStatus::Queued,
                "Authoritative input queued");
        world.advance(tick / 60.);
    }
    s = world.snapshot("a", 2);
    require(s.position.x <= 1.650001, "Swept actor cannot tunnel through thin wall");
    require(world.submit(
                "a",
                {s.nextInputSequence, s.tick, s.epoch, std::numeric_limits<double>::quiet_NaN(), 0},
                2) == g::InputStatus::InvalidInput,
            "Nonfinite command rejected");
    g::AuthoritativeWorld combat({{{-20, -1, -20}, {20, 0, 20}}});
    combat.addActor("a", {0, .9, 0}, 0);
    combat.addActor("b", {4, .9, 0}, 0);
    auto shot = combat.fire("a", 1, {1, 0, 0}, 0);
    require(shot.status == g::ShotStatus::Hit && shot.damage == 25,
            "Server ray applies authoritative damage");
    require(combat.fire("a", 1, {1, 0, 0}, 0).status == g::ShotStatus::SequenceMismatch,
            "Shot replay rejected");
    require(combat.fire("a", 2, {1, 0, 0}, 0).status == g::ShotStatus::Cooldown,
            "Server fire cooldown");
}
void motorChecks() {
    auto bank = fixture();
    m::MotionCache cache(bank);
    for (int id = 0; id < 128; id++) {
        auto clip = cache.clip(id);
        require(clip.duration > 0, "Motion duration positive");
        for (int joint = 0; joint < m::Joints; joint++) {
            auto a = cache.normalized(id, 0, joint), b = cache.normalized(id, 1, joint);
            require(a.finite() && b.finite(), "Finite cached targets");
            if (clip.looping)
                require((a - b).squared() == 0, "Bit-exact loop seam");
        }
    }
    uint8_t nibble = 0xf8;
    require(m::DataBank::signedNibble(&nibble, 1, 0) == -8 &&
                m::DataBank::signedNibble(&nibble, 1, 1) == -1,
            "Signed int4 decode");
    require((bank->sampleField({.6f, -.3f, .2f}) - m::Vec{-.6f, .3f, -.2f}).length() < 1e-5f,
            "Trilinear 3D field reference");
    m::Ring<int> ring(2);
    int item = 0;
    require(ring.push(1) && ring.push(2) && !ring.push(3), "Bounded event ingress");
    require(ring.pop(item) && item == 1 && ring.push(3) && ring.pop(item) && item == 2 &&
                ring.pop(item) && item == 3 && !ring.pop(item),
            "Event ring FIFO wraps");
    m::FlowField flow(32, 1);
    flow.bake([](m::Vec centre, m::Vec) { return centre.x > 0 && centre.x < 2 && centre.z < 10; });
    flow.rebuild({10, 0, 0}, {-10, 0, 0});
    require(flow.blockedCount() > 0, "Host obstacle baking");
    require(flow.direction({-2, 0, 0}, 0).squared() > .5f, "Shared navigation routes toward goal");
    m::Settings settings;
    settings.workers = 1;
    m::World one(bank, 8, settings);
    settings.workers = 4;
    m::World four(bank, 8, settings);
    for (int a = 0; a < 8; a++) {
        one.locomotion(a, {0, 0, 1}, .55f);
        four.locomotion(a, {0, 0, 1}, .55f);
    }
    for (int t = 0; t < 240; t++) {
        one.tick();
        four.tick();
    }
    require(one.valid() && four.valid(), "Native full world finite after simulation");
    for (size_t i = 0; i < one.position.size(); i++)
        require((one.position[i] - four.position[i]).squared() == 0,
                "Native worker-count particle replay");
    require(one.queue(m::EventKind::Damage, 0, 2, {}, 20), "Damage ingress valid");
    one.spawn(0, {-3, 0, 0}, 0, 70, 0);
    one.tick();
    require(one.health[0] == 100, "Queued old-generation damage cannot hit respawn");
    one.sever(0, 11);
    require(one.supportLegLoss(0) && one.state[0] == uint8_t(m::State::Crawling),
            "Human leg loss chooses crawl");
    one.recover(0);
    require(one.state[0] == uint8_t(m::State::Crawling), "Recovery cannot regrow support leg");
    m::InjuryScheduler injuries(8);
    int losses = 0;
    injuries.limbLost = [&](int, int) { losses++; };
    for (int t = 0; t < 13; t++) {
        one.tick();
        injuries.tick(one);
    }
    require(losses == 1 && (injuries.flags(0) & 20) == 20, "Phased injury audit records limb once");
    m::Projectiles projectiles;
    m::RTS rts(one, projectiles);
    rts.enemyAI = false;
    require(rts.reserve(2) && rts.recruit(0, m::RTSRole::Worker), "RTS bounded reserve/recruit");
    require(rts.wood[0] == 45, "Recruit spends actual resources");
    rts.maximumDecisions = 2;
    for (int t = 0; t < 20; t++) {
        rts.tick(m::Step);
        require(rts.lastDecisions <= 2 && rts.lastProbes <= 64,
                "RTS decision/probe budgets bounded");
    }
    require(!rts.setMove(0, {std::numeric_limits<float>::infinity(), 0, 0}),
            "RTS nonfinite goals rejected");
    m::BloodPool blood(16);
    blood.emit({0, 1, 0}, {0, 1, 0}, 100, 13);
    require(blood.drops().size() == 16, "Blood fixed capacity");
    for (int t = 0; t < 100; t++)
        blood.tick(.05f);
    bool stain = false;
    for (auto& b : blood.drops())
        stain |= b.stain;
    require(stain, "Blood settles on ground");
    std::array<m::CapsuleInstance, 10> render;
    require(m::writeCapsules(one, render.data(), render.size()) == render.size(),
            "Renderer-neutral packet honors output capacity");
}

g::Key testKey();

g::Bytes temporalFixture();
void boundaryChecks() {
    g::Id context{};
    context.fill(0x22);
    auto original = temporalFixture();
    auto model = g::TemporalModel::load(original, context);
    g::TemporalRisk risk(model);
    original[100] ^= 1;
    require(rejects([&] { g::TemporalModel::load(original, context); }),
            "Corrupted temporal model rejected");
    require(model->exportBytes() != original, "Loaded model owns immutable copied bytes");
    auto wrong = context;
    wrong[0] ^= 1;
    require(risk.observe({}, wrong).status == g::TemporalStatus::ContextMismatch,
            "Wrong context abstains");
    require(risk.observe({0, .2, .1, 0, 0}, context).status == g::TemporalStatus::NeedsHistory,
            "Temporal tracker requires history");
    require(risk.observe({2, .2, .1, 0, 0}, context).status ==
                g::TemporalStatus::UnsupportedInterval,
            "Temporal unsupported gap abstains");
    require(risk.observe({2.05, .2, .1, 0, 0}, context).status ==
                g::TemporalStatus::UnsupportedInterval,
            "Gap cannot silently reset temporal trust");
    risk.reset();
    require(risk.observe({3, .2, .1, 0, 0}, context).status == g::TemporalStatus::NeedsHistory,
            "Trusted temporal reset explicit");
    g::MovementLimits limits;
    g::MovementTracker tracker(limits);
    limits.maximumSpeed = 1e6;
    tracker.observe(0, 0, 0, 0);
    require(!tracker.observe(.1, 10, 0, 0).accepted,
            "Caller limit mutation cannot relax trusted tracker");
    g::Codec codec(testKey());
    g::Bytes decoded;
    auto empty = codec.encode({}, "empty");
    require(codec.decode(empty, "empty", decoded) && decoded.empty(),
            "Empty payload interoperable padding");
    require(rejects([&] { codec.encode({}, ""); }), "Empty cryptographic context rejected");
    g::Binding arithmeticBinding("score");
    g::ProtectedLong maximum(INT64_MAX, arithmeticBinding);
    require(rejects([&] { maximum.add(1, &arithmeticBinding); }) &&
                maximum.read(arithmeticBinding) == INT64_MAX,
            "Protected arithmetic rejects signed overflow without modifying value");
    maximum.subtract(2, &arithmeticBinding);
    require(maximum.read(arithmeticBinding) == INT64_MAX - 2,
            "Protected arithmetic preserves field binding");
    g::ProtectedInt protectedValue(77);
    uint64_t before = nativeAllocations.load();
    int sum = 0;
    for (int i = 0; i < 10000; i++)
        sum += protectedValue.value();
    uint64_t allocationCount = nativeAllocations.load() - before;
    require(sum == 770000 && allocationCount == 0,
            "Protected native read loop has zero ordinary new/new[]");
    std::vector<g::Box> floor{{{-50, -1, -50}, {50, 0, 50}}};
    g::AuthoritativeWorld server(floor);
    server.addActor("a", {0, .9, 0}, 0);
    auto snap = server.snapshot("a", 0);
    for (int i = 1; i <= 3; i++)
        require(server.submit("a", {i, 0, snap.epoch, 1, 1}, 0) == g::InputStatus::Queued,
                "Three bounded pending commands");
    require(server.submit("a", {4, 0, snap.epoch, 1, 1}, 0) == g::InputStatus::QueueFull,
            "Input flood cannot add simulation time");
    require(server.snapshot("a", 0).tick == 0, "Client commands never advance trusted clock");
    server.advance(1. / 60);
    auto step = server.snapshot("a", 1. / 60);
    require(std::hypot(step.velocity.x, step.velocity.z) <= 1.00000001,
            "Diagonal acceleration bounded as vector");
    auto held = step.position;
    server.advance(10);
    auto stalled = server.snapshot("a", 10);
    require(stalled.resyncRequired && near(stalled.position.x, held.x) &&
                near(stalled.position.y, held.y),
            "Long server stall holds authoritative pose");
    auto resumed = server.resynchronize("a", 10);
    require(resumed.velocity.x == stalled.velocity.x && resumed.ammo == stalled.ammo,
            "Resync cannot stop velocity or reload");
    g::AuthoritativeWorld arena({{{-50, -1, -50}, {50, 0, 50}}, {{2, 0, -2}, {2.01, 4, 2}}});
    arena.addActor("a", {0, .9, 0}, 0);
    arena.addActor("b", {4, .9, 0}, 0);
    require(arena.fire("a", 1, {1, 0, 0}, 0).status == g::ShotStatus::Blocked &&
                arena.snapshot("b", 0).health == 100,
            "Static wall blocks authoritative damage");
    require(arena.fire("a", 2, {std::numeric_limits<double>::quiet_NaN(), 0, 0}, 0).status ==
                    g::ShotStatus::InvalidAim &&
                arena.snapshot("a", 0).ammo == 29,
            "Invalid aim cannot spend ammunition");
    require(rejects([&] { arena.setLagCompensation("a", 13); }), "Server rewind budget bounded");
    auto bank = fixture();
    m::Settings settings;
    settings.bodyCollisions = false;
    settings.autoRecovery = false;
    m::World world(bank, 2, settings);
    world.spawn(0, {-4, 0, 0}, 0, 70, 0);
    world.spawn(1, {4, 0, 0}, 0, 70, 1);
    auto originalBankPose = world.bank().pose(0, 0);
    bank->poses[0].y += 100;
    require(world.bank().pose(0, 0).y == originalBankPose.y,
            "Native world freezes a copy of supplied data");
    bank->poses[0].y -= 100;
    m::Projectiles shots;
    require(shots.fire(world, {0, 1.43f, 0}, {1, 0, 0}, 100, 0, .035f, 0, m::ProjectileKind::Bullet,
                       20),
            "Projectile accepted into fixed pool");
    shots.tick(world, .1f);
    require(shots.active == 0 && world.pendingEvents() == 1 && world.health[1] == 100,
            "Swept physical hit queues one event");
    world.tick();
    require(world.health[1] == 80, "Projectile damage committed through event queue");
    shots.obstacleFraction = [](m::Vec, m::Vec, float) { return .2f; };
    shots.fire(world, {0, 1.43f, 0}, {1, 0, 0}, 100, 0, .035f, 0, m::ProjectileKind::Bullet, 20);
    shots.tick(world, .1f);
    require(world.pendingEvents() == 0 && world.health[1] == 80,
            "Host obstacle callback blocks projectile damage");
    world.spawn(0, {0, 0, 0}, 0, 70, 0);
    world.spawn(1, {1, 0, 0}, 0, 70, 1);
    for (int j = 0; j < m::Joints; j++)
        world.position[m::Joints + j] = {1, 1.4f, 5};
    world.position[m::Joints] = {1, .95f, 0};
    world.position[m::Joints + 2] = {.5f, 1.4f, 0};
    world.position[9] = {0, 1.4f, 0};
    require(world.melee->begin(0, 1, 25, 0, m::Weapon::Unarmed), "Physical melee swing begins");
    world.melee->tick(.1f);
    world.melee->tick(.1f);
    require(world.melee->hits == 0 && world.pendingEvents() == 0,
            "Resting overlap/proximity is not melee damage");
    world.position[9] = {1, 1.4f, 0};
    world.melee->tick(.05f);
    require(world.melee->hits == 1 && world.pendingEvents() == 1,
            "Swept physical hand produces one melee hit");
    world.position[9] = {0, 1.4f, 0};
    world.melee->tick(.05f);
    require(world.melee->hits == 1 && world.pendingEvents() == 1,
            "A swing cannot apply duplicate hits");
    world.spawn(1, {5, 0, 0}, 0, 70, 1);
    world.tick();
    require(world.health[1] == 100, "Queued melee hit cannot damage respawn generation");
    m::AuthoredClip packed;
    packed.name = "Packed test";
    packed.family = m::Family::Hit;
    packed.duration = 1;
    packed.looping = false;
    packed.frames = 2;
    packed.packed.resize(2 * m::Joints * 3);
    packed.contacts = {1, 2};
    for (int j = 0; j < m::Joints; j++) {
        packed.packed[j * 3] = 100;
        packed.packed[(m::Joints + j) * 3] = 300;
    }
    packed.validate();
    require(std::abs(packed.sample(.5f, 0).x - .2f) < 1e-6f && packed.contactMask(.5f) == 1,
            "Packed int16 motion interpolates millimetres and rounds contact frames");
    packed.duration = 0;
    require(rejects([&] { packed.validate(); }),
            "Invalid authored clip rejected before installation");
    m::World stable(bank, 4, settings);
    for (int i = 0; i < 20; i++)
        stable.tick();
    before = nativeAllocations.load();
    for (int i = 0; i < 120; i++)
        stable.tick();
    allocationCount = nativeAllocations.load() - before;
    require(allocationCount == 0 && stable.valid(),
            "Warmed full Motor world has zero ordinary step allocations");
    m::Archetype archetype;
    archetype.health = 200;
    archetype.equipment = m::Equipment::Sword;
    archetype.apply(stable, 0);
    require(stable.health[0] == 200 && stable.visualArchetype[0] == 10 && stable.valid(),
            "Archetype tuning and weapon vocabulary applied");
    std::array<m::EquipmentInstance, 4> equipment;
    require(m::writeEquipment(stable, equipment.data(), equipment.size()) == 1 &&
                equipment[0].kind == m::Equipment::Sword,
            "Equipment transforms exported without Unity");
    std::array<m::Vec, 18> pose;
    std::array<m::Quaternion, 18> rotations{};
    for (int j = 0; j < 18; j++)
        pose[j] = stable.joint(0, j);
    m::RigBinding binding;
    binding.capture(pose, rotations, {}, stable.heading(0), stable.bank());
    auto rig = binding.apply(stable, 0, .016f);
    require(rig.rootPosition.length() < 1e-5f, "Retargeted rig root preserves captured hip offset");
    auto opposite = m::Quaternion::fromTo({1, 0, 0}, {-1, 0, 0}).rotate({1, 0, 0});
    require((opposite - m::Vec{-1, 0, 0}).length() < 1e-5f,
            "Retargeting handles opposite directions");
    {
        m::Crowd crowd(stable);
        crowd.ai = false;
        int old = stable.frame;
        crowd.advance(1);
        require(stable.frame - old == 6 && crowd.droppedSeconds > 0,
                "Crowd catch-up is bounded at six steps");
        crowd.volley({0, 3, -20});
        require(crowd.projectiles.active == 64, "Crowd volley uses fixed projectile pool");
    }

    stable.spawn(0, {0, 0, 0}, 0, 70, 0);
    m::AuthoredClip stumble;
    stumble.name = "Stumble";
    stumble.family = m::Family::Fall;
    stumble.duration = .6f;
    stumble.looping = false;
    stumble.frames = 2;
    stumble.poses.resize(2 * m::Joints);
    for (int frame = 0; frame < 2; frame++)
        for (int j = 0; j < m::Joints; j++)
            stumble.poses[size_t(frame) * m::Joints + j] = bank->pose(0, j);
    stable.setClip(stumble);
    stable.impulse(0, 2, {21, 0, 0});
    require(stable.stumbling(0), "Authored stumble reaction participates in native world");
    stable.locomotion(0, {1, 0, 0}, 1);
    stable.tick();
    require(stable.action[0] == uint8_t(m::Action::Idle),
            "Stumble suppresses locomotion/attack intent");
    m::Projectiles reservedShots;
    {
        m::RTS controller(stable, reservedShots);
        controller.enemyAI = false;
        controller.reserve(3);
        std::vector<m::CapsuleInstance> capsules(size_t(stable.count()) * 17);
        size_t written = m::writeCapsules(stable, capsules.data(), capsules.size());
        bool visible = false;
        for (size_t i = 0; i < written; i++)
            visible |= capsules[i].actor == 3;
        require(!visible, "Reserved RTS slots are absent from render packets");
    }
}
template <class... T> void row(const char* tag, T... values) {
    std::cout << tag;
    ((std::cout << '\t' << values), ...);
    std::cout << '\n';
}
g::Key testKey() {
    g::Key key{};
    for (int i = 0; i < 32; i++)
        key[i] = uint8_t(i);
    return key;
}
g::Bytes canonicalState() {
    std::string value = "OUT interoperability / Я / 日本 / ";
    g::Bytes state(value.begin(), value.end());
    state.push_back(0);
    return state;
}
g::Bytes temporalFixture() {
    g::Bytes b(g::TemporalModel::FileBytes);
    auto put = [&](size_t at, uint64_t value, int n) {
        for (int k = 0; k < n; k++)
            b[at + size_t(k)] = uint8_t(value >> (8 * k));
    };
    auto real = [&](size_t at, double d) {
        uint64_t bits;
        std::memcpy(&bits, &d, 8);
        put(at, bits, 8);
    };
    put(0, 0x5254474f, 4);
    put(4, 1, 2);
    put(6, 1, 2);
    std::fill_n(b.begin() + 8, 16, uint8_t(0x11));
    std::fill_n(b.begin() + 24, 16, uint8_t(0x22));
    real(40, .05);
    real(48, .5);
    real(56, .4);
    put(64, 45000, 2);
    b[66] = 2;
    put(68, 4096, 2);
    b[70] = 4;
    put(72, 10240, 4);
    for (int table = 0; table < 5; table++)
        for (int i = 0; i < 4096; i++) {
            int v = table == 4 ? (i % 13 == 0 ? 0 : 15) : ((i * 7 + table * 3) % 16);
            b[80 + table * 2048 + i / 2] |= uint8_t(v << ((i & 1) * 4));
        }
    auto digest = g::sha256(b.data(), 10320);
    std::copy(digest.begin(), digest.end(), b.begin() + 10320);
    return b;
}
void reference() {
    std::cout << std::setprecision(17);
    auto key = testKey();
    auto state = canonicalState();
    auto user = g::Codec::deriveUserKey(key, "alice", "app");
    row("K", g::hex(user.data(), user.size()));
    g::Challenge challenge{7, g::base64(key.data(), key.size()), 123.5};
    auto reply = g::createReply(key, challenge, state);
    row("H", g::hex(reply.authentication.data(), 32));
    auto hash = g::sha256(state.data(), state.size());
    auto signature = g::signatureRecord("release/test.txt", g::hex(hash.data(), 32), "native-test");
    row("F", g::hex(signature.data(), signature.size()));
    g::MovementTracker movement;
    for (int i = 0; i < 200; i++) {
        double time = i * .05, x = i * .1, y = 0, z = std::sin(i * .03) * .1;
        if (i % 17 == 7)
            x += 20;
        if (i % 23 == 6)
            y += 30;
        auto r = movement.observe(time, x, y, z, i == 120);
        row("M", r.accepted, r.hasEvidence, r.rebased, r.skipped, r.violations, int(r.signal),
            r.speedRatio, r.accelerationRatio, r.verticalRatio, r.predictionRatio, r.tableRisk);
    }
    g::Clock clock(1, .1, 3);
    clock.start(0);
    double ratio = 1;
    for (int i = 1; i <= 160; i++) {
        if (i == 45)
            clock.suspend(i * .1, ratio);
        if (i == 52)
            clock.resume(i * .1);
        bool found = clock.observe(i * .1, (i / 20) % 2 == 0 ? .2 : .05, ratio);
        row("C", found, ratio);
    }
    g::Id context{};
    context.fill(0x22);
    g::TemporalRisk temporal(g::TemporalModel::load(temporalFixture(), context));
    for (int i = 0; i < 120; i++) {
        auto a = temporal.observe({i * .05, (i % 19) / 9., (i % 7) / 4., .2, .1}, context);
        row("T", int(a.status), a.reasons, a.normal, a.lag, a.anomaly, a.uncertain, int(a.support),
            a.reviewSuggested);
    }
    g::AuthoritativeWorld world({{{-50, -1, -50}, {50, 0, 50}}, {{2, 0, -2}, {2.02, 5, 2}}});
    world.addActor("a", {-3, .9, 0}, 0);
    for (int i = 1; i <= 180; i++) {
        double t = (i - 1) / 60.;
        auto snapshot = world.snapshot("a", t);
        auto status = world.submit("a",
                                   {snapshot.nextInputSequence, snapshot.tick, snapshot.epoch, 1,
                                    i > 70 && i < 130 ? .4 : 0, i % 20 == 0, i % 30 == 0},
                                   t);
        snapshot = world.snapshot("a", i / 60.);
        row("S", int(status), snapshot.position.x, snapshot.position.y, snapshot.position.z,
            snapshot.velocity.x, snapshot.velocity.y, snapshot.velocity.z, snapshot.grounded,
            snapshot.epoch, snapshot.health);
    }
    auto bank = fixture();
    m::MotionCache cache(bank);
    for (int id = 0; id < 128; id++)
        for (int sample = 0; sample < 5; sample++)
            for (int joint : {0, 3, 6, 9, 11, 12, 13, 15, 16, 17}) {
                float t = (sample - 1) * .37f;
                auto p = cache.normalized(id, t, joint);
                row("P", p.x, p.y, p.z, int(cache.contacts(id, t)));
            }
    m::FootPlanner planner(1, bank);
    std::array<m::Vec, 18> goals{}, velocities{};
    std::array<uint8_t, 18> detached{};
    auto heading = m::Quaternion::yaw(30);
    for (int j = 0; j < 18; j++)
        goals[j] = heading.rotate(bank->pose(0, j));
    planner.reset(0, goals.data(), heading);
    for (int i = 0; i < 60; i++) {
        float t = i / 30.f;
        m::Vec anchor{i * .01f, 0, i * .02f};
        for (int j = 0; j < 18; j++)
            goals[j] = anchor + heading.rotate(cache.normalized(8, t, j));
        planner.apply(0, .05f, t, cache.contacts(8, t), cache.clip(8).duration, {.2f, 0, .4f},
                      heading, anchor, goals.data(), velocities.data(), detached.data(), false);
        for (int j : {11, 12, 13, 15, 16, 17})
            row("G", goals[j].x, goals[j].y, goals[j].z, velocities[j].x, velocities[j].y,
                velocities[j].z);
    }
}
void interop(const std::filesystem::path& directory) {
    auto read = [&](const char* name) {
        std::ifstream file(directory / name);
        std::string text;
        if (!(file >> text))
            throw std::runtime_error("Missing C# interoperability fixture");
        return g::unhex(text);
    };
    g::Codec codec(testKey());
    g::Bytes payload;
    require(codec.decode(read("csharp-envelope.hex"), "interop/slot", payload) &&
                payload == canonicalState(),
            "Native decodes original C# envelope");
    auto encoded = codec.encode(canonicalState(), "interop/slot");
    std::ofstream(directory / "native-envelope.hex") << g::hex(encoded.data(), encoded.size());
    auto digest = g::sha256(payload.data(), payload.size());
    require(g::verifySignedFile(directory, "release/test.txt", g::hex(digest.data(), 32),
                                "native-test", read("rsa-signature.hex"), read("rsa-modulus.hex"),
                                read("rsa-exponent.hex")) == g::FileStatus::Valid,
            "Native verifies original C# RSA manifest");
    auto bad = read("rsa-signature.hex");
    bad[0] ^= 1;
    require(g::verifySignedFile(directory, "release/test.txt", g::hex(digest.data(), 32),
                                "native-test", bad, read("rsa-modulus.hex"),
                                read("rsa-exponent.hex")) == g::FileStatus::UntrustedManifest,
            "Edited RSA signature rejected");
    std::cout << "PASS native decoded C# envelope and verified RSA manifest\n";
}
} // namespace
int main(int argc, char** argv) {
    try {
        if (argc > 1 && std::string(argv[1]) == "--reference") {
            reference();
            return 0;
        }
        if (argc > 2 && std::string(argv[1]) == "--interop") {
            interop(std::filesystem::u8path(argv[2]));
            return 0;
        }
        guardChecks();
        motorChecks();
        boundaryChecks();
        std::cout << "PASS " << checks << " native Guard/Motor assertions\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "FAIL after " << checks << " checks: " << e.what() << "\n";
        return 1;
    }
}
