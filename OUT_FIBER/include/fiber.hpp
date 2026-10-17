// Copyright (c) 2026 Алексей Савельев (Alex Merqury / AlexMerq).
// SPDX-License-Identifier: LicenseRef-OUT-Source-Review-1.0
#pragma once
#include <array>
#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <functional>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace fiber {
constexpr int Actions=6, Branches=3, MapW=192, MapH=144;
constexpr float Tile=2.f, Width=MapW*Tile, Height=MapH*Tile, Dt=1.f/60;
enum Action : uint8_t { Routine, Investigate, Engage, Aid, Flee, Report };
enum Role : uint8_t { Villager, Guard, Merchant, Healer, Bandit, Wolf };
enum class Mode { Strict, Overlap, Exploratory, Utility };
const char* actionName(int); const char* roleName(int); const char* modeName(Mode);
uint32_t hash(uint32_t a,uint32_t b=0,uint32_t c=0,uint32_t d=0);
float random01(uint32_t); float clamp(float,float=0,float=1);
struct Vec { float x=0,y=0; };
struct Observation {
    float danger=0, need=0, health=1, aid=0, social=0, distance=1000;
    uint8_t cue=0; bool visible=false, blockedShot=false, critical=false;
    int enemy=-1, ally=-1; Vec clue{};
};
// Compact policy memory, separate from physical state and long-lived social context.
struct Mind {
    std::array<float,4> inhibition{};
    float fear=0, confidence=0, intensity=.5f, correction=.5f;
    uint32_t holdUntil=0, eventId=0;
    uint16_t branch=65535;
    uint8_t action=Routine, cue=0, phase=0, flags=0;
    std::array<uint8_t,3> previous{};
    uint8_t focus=0;
};
static_assert(sizeof(Mind)<=64,"Policy memory budget exceeded");
struct Decision { Action action=Routine; float intensity=.5f, margin=0; uint16_t branch=65535; uint8_t candidates=0; bool ambiguous=false, blocked=false, held=false; };
struct Branch {
    uint16_t prototype=0, support=0;
    std::array<uint16_t,4> cues{};
    std::array<int8_t,8> residual{};
    std::array<int8_t,16> gate{};
    uint8_t action=0;
};
struct MotionCell { int8_t speed=0, steer=0, avoidance=0, response=0; };
class Bank {
    std::array<Branch,64*Branches> branches_{};
    std::array<MotionCell,4*4*4*4> motion_{};
    // Five *address* axes; each cell has six packed int4 action components.
    std::array<uint8_t,4*4*4*4*4*3> context_{};
    uint64_t rejectedSamples_=0;
public:
    Bank();
    Decision decide(Mind&,const Observation&,Role,float courage,float affinity,
                    uint32_t seed,uint32_t id,uint32_t tick,Mode) const;
    MotionCell motion(Action,float crowd,float fatigue,float confidence) const;
    float context(int action,float need,float fear,float courage,float affinity,float phase) const;
    static int address3(float,float,float);
    static int address4(int,int,int,int);
    static int address5(int,int,int,int,int);
    static Action teacher(float danger,float need,float health,uint8_t cue);
    static bool allowed(Action,const Observation&,Role);
    size_t bytes() const { return sizeof(branches_)+sizeof(motion_)+sizeof(context_); }
    uint64_t checksum() const;
    uint64_t rejectedSamples() const {return rejectedSamples_;}
};
class Workers {
    std::vector<std::thread> threads_;
    std::mutex mutex_; std::condition_variable wake_,done_;
    std::function<void(size_t,size_t)> job_;
    size_t count_=0,epoch_=0,remaining_=0; bool stop_=false;
public:
    explicit Workers(unsigned count); ~Workers();
    Workers(const Workers&)=delete;
    void run(size_t,const std::function<void(size_t,size_t)>&);
    unsigned count() const { return unsigned(threads_.size()+1); }
};
struct Site { Vec at; const char* name; uint8_t kind; };
struct Map {
    std::array<uint8_t,MapW*MapH> tiles{};
    // Genuine [destination][y][x] 3D flow field shared by all actors.
    std::array<std::array<uint16_t,MapW*MapH>,8> flow{};
    std::array<Site,8> sites{};
    explicit Map(uint32_t seed);
    bool clear(float x,float y,float radius=.28f) const;
    bool line(Vec,Vec) const;
    Vec direction(Vec,int destination) const;
    int nearest(Vec,int kind=-1) const;
    uint8_t tile(int x,int y) const;
};
struct Actor {
    Mind mind;
    float hp=100,hunger=.2f,thirst=.2f,fatigue=.1f,courage=.5f,affinity=0;
    Vec goal{},clue{}; uint32_t lastSeen=0,rumor=0;
    std::array<uint32_t,4> recentEvents{};
    uint8_t eventHead=0;
    uint16_t coins=12,food=1; uint8_t role=Villager,team=0,home=0,task=0;
};
struct Player { Vec at{192,144}; float hp=100,stamina=100,hunger=.12f,thirst=.12f,warmth=1; int food=3,water=3,gold=32,herbs=0,quest=0; float cooldown=0; bool sneaking=false; };
struct Input { float x=0,y=0; bool sprint=false,sneak=false; };
struct Event { Vec at; uint32_t id=0,until=0; float strength=0; bool crime=false,reported=false; };
struct Metrics {
    double gridMs=0,thinkMs=0,moveMs=0,commitMs=0,totalMs=0;
    uint64_t decisions=0,ambiguous=0,safetyBlocks=0,held=0,switches=0,probes=0,capped=0;
    uint64_t collisions=0,hits=0,heals=0,rumors=0,arrivals=0;
};
class World {
    static constexpr int GridW=48, GridH=36;
    Workers workers_;
    std::array<uint32_t,GridW*GridH+1> offsets_{};
    std::vector<uint32_t> order_,cursor_;
    std::vector<float> nextX_,nextY_,fearBefore_;
    std::vector<uint32_t> rumorBefore_;
    struct Intent { int target=-1; float damage=0,heal=0; };
    std::vector<Intent> intents_;
    std::vector<Metrics> local_;
    void grid(); void think(size_t,size_t); void move(size_t,size_t);
    int cell(float,float) const;
public:
    Bank bank; Map map;
    std::vector<float> x,y,vx,vy;
    std::vector<Actor> actors;
    Player player; Input input; Metrics metrics;
    std::array<Event,64> events{};
    uint32_t tick=0,seed=41917,nextEvent=1; Mode mode=Mode::Exploratory;
    bool fullAI=false; float wanted=0;
    explicit World(size_t population=1024,uint32_t seed=41917,unsigned workers=1);
    void step(); void signal(Vec,float strength=1,bool crime=false);
    std::string interact(); std::string attack(); std::string consume(bool water);
    std::string rest(); std::string talk(int);
    int nearestActor(Vec,float distance=8) const;
    bool valid() const; uint64_t digest() const; size_t bytes() const;
    unsigned workers() const {return workers_.count();}
    float hour() const {return float((tick%43200+14400)%43200)/1800.f;}
};
struct BenchmarkOptions { size_t agents=4096; unsigned workers=1; uint32_t seed=41917; int ticks=600,warmup=120; bool fullAI=false; Mode mode=Mode::Exploratory; std::string output; };
int benchmark(const BenchmarkOptions&);
int selftest();
}
