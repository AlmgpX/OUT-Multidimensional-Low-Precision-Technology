// SPDX-License-Identifier: LicenseRef-OUT-Source-Review-1.0
// Copyright (c) 2026 Алексей Савельев (Alex Merqury / AlexMerq).
#pragma once
#include <array>
#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <functional>
#include <mutex>
#include <memory>
#include <string>
#include <thread>
#include <vector>

namespace fiber {
constexpr int Actions=6, Branches=3, MapW=512, MapH=384;
constexpr float Tile=2.f, Width=MapW*Tile, Height=MapH*Tile, Dt=1.f/60;
enum Action : uint8_t { Routine, Investigate, Engage, Aid, Flee, Report };
enum Role : uint8_t { Villager, Guard, Merchant, Healer, Bandit, Wolf, Hunter, Farmer, Innkeeper, Scholar };
enum Life : uint8_t { Travel, Work, Sleep, Eat, Drink, Socialize, Recover, Patrol, Forage, Loiter, Rob, Pray, LifeCount };
enum Item : uint8_t { Bread,Water,Herb,Berry,Wine,Charm,Ring,MapScrap,Letter,Trace,Purse,AshKey,Shard,Wood,Stone,Fiber,Seed,Axe,Pick,ItemCount };
const char* lifeName(int);const char* itemName(int);int itemValue(int);
enum ProjectKind : uint8_t { Campfire,Bedroll,Workbench,Garden,Storage,Shelter,ProjectCount };
const char* projectName(int);
enum class Mode { Strict, Overlap, Exploratory, Utility };
const char* actionName(int); const char* roleName(int); const char* modeName(Mode);
uint32_t hash(uint32_t a,uint32_t b=0,uint32_t c=0,uint32_t d=0);
float random01(uint32_t); float clamp(float,float=0,float=1);
struct Vec { float x=0,y=0; };
struct ResourceNode {Vec at{};uint32_t renewAt=0;uint8_t kind=Wood,stock=0;};
struct Project {Vec at{};float growth=0;uint8_t kind=Campfire,active=0,watered=0;};
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
    std::array<uint8_t,4*4*4*4*4*6> life_{};
    std::array<uint8_t,4*4*4*11*4*2> social_{};
    uint64_t rejectedSamples_=0;
public:
    Bank();
    Decision decide(Mind&,const Observation&,Role,float courage,float affinity,
                    uint32_t seed,uint32_t id,uint32_t tick,Mode) const;
    MotionCell motion(Action,float crowd,float fatigue,float confidence) const;
    float context(int action,float need,float fear,float courage,float affinity,float phase) const;
    Life chooseLife(float hunger,float thirst,float fatigue,float hour,Role,float loneliness,float stress,uint32_t identity)const;
    int socialStance(float relationship,float alarm,float stakes,int cue,float bravery,float greed)const;
    static int address3(float,float,float);
    static int address4(int,int,int,int);
    static int address5(int,int,int,int,int);
    static Action teacher(float danger,float need,float health,uint8_t cue);
    static bool allowed(Action,const Observation&,Role);
    size_t bytes() const { return sizeof(branches_)+sizeof(motion_)+sizeof(context_)+sizeof(life_)+sizeof(social_); }
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
struct Building {int x=0,y=0,w=12,h=9,zone=0;Vec door{},bed{};int navX=0,navY=0;std::vector<uint16_t> flow;};
struct Map {
    std::vector<uint8_t> tiles;
    // Genuine [destination][y][x] 3D flow field shared by all actors.
    std::array<std::vector<uint16_t>,8> flow;
    std::array<Site,8> sites{};
    std::vector<Building> buildings;
    explicit Map(uint32_t seed);
    bool clear(float x,float y,float radius=.28f) const;
    bool line(Vec,Vec) const;
    Vec direction(Vec,int destination) const;
    Vec homeDirection(Vec,int home)const;
    bool inside(Vec,int home)const;
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
    uint16_t residence=0;
    int16_t character=-1;
    uint8_t workSite=0,socialSite=2,life=Loiter,lod=0,pocketCount=0;
    std::array<uint8_t,4> pocket{};
    float loneliness=.2f,stress=0,sanity=1,grief=0,drunk=0;
    uint32_t activityUntil=0,nextSocial=0;int32_t grudge=-1;
    int32_t partner=-1;
    struct Bond {int32_t peer=-1;int16_t trust=0;uint16_t meetings=0;};
    std::array<Bond,4> bonds{};
    Vec homeAt{};
};
struct Player { Vec at{192,144}; float hp=100,stamina=100,hunger=.12f,thirst=.12f,warmth=1; int food=3,water=3,gold=32,herbs=0,quest=0; float cooldown=0; bool sneaking=false;
    std::array<int,ItemCount> bag{};std::array<uint8_t,4> quests{};std::array<int,5> reputation{};int renown=0;float stealth=.48f;
};
struct Input { float x=0,y=0; bool sprint=false,sneak=false; };
struct Event { Vec at; uint32_t id=0,until=0; float strength=0; bool crime=false,reported=false; };
struct Metrics {
    double gridMs=0,thinkMs=0,moveMs=0,commitMs=0,totalMs=0;
    uint64_t decisions=0,ambiguous=0,safetyBlocks=0,held=0,switches=0,probes=0,capped=0;
    uint64_t collisions=0,hits=0,heals=0,rumors=0,arrivals=0;
    uint64_t nearActors=0,midActors=0,farActors=0,sleepers=0,socials=0;
};
struct CharacterDef {const char* id;const char* name;const char* roleText;const char* bio;const char* secret;std::array<const char*,3> intro;const char* vow;uint8_t faction,work,home,social,role;float courage,perception,greed,heart,opinion;uint16_t coins;std::array<uint8_t,4> pocket;uint8_t pocketCount;};
const std::vector<CharacterDef>& characters();const char* zoneLore(int);const char* zoneName(int);
struct Choice {int code;std::string label;};
struct Conversation {int actor=-1;std::string title,text;std::vector<Choice> choices;bool open=false;};
struct Blood {Vec at{},velocity{};float life=0,radius=0;uint8_t stain=0;};
struct Notice {uint32_t tick=0;std::array<char,256> text{};};
class World {
    static constexpr int GridW=MapW/4, GridH=MapH/4;
    Workers workers_;
    std::array<uint32_t,GridW*GridH+1> offsets_{};
    std::vector<uint32_t> order_,cursor_;
    std::vector<float> nextX_,nextY_,fearBefore_;
    std::vector<uint32_t> rumorBefore_;
    struct Intent { int target=-1; float damage=0,heal=0; };
    std::vector<Intent> intents_;
    std::vector<Metrics> local_;
    void grid(); void think(size_t,size_t); void move(size_t,size_t);void planLife(size_t);void updateEffects();void updateSandbox(float hours);
    int cell(float,float) const;
public:
    Bank bank; Map map;
    std::vector<float> x,y,vx,vy;
    std::vector<Actor> actors;
    Player player; Input input; Metrics metrics;
    std::array<Event,64> events{};
    std::array<Blood,1024> blood{};uint32_t bloodCursor=0;
    std::array<Notice,32> notices{};uint32_t noticeCursor=0;
    Conversation dialogue;
    std::array<ResourceNode,384> resources{};
    std::array<Project,64> projects{};
    std::array<int,4> stockpile{};
    uint32_t tick=0,seed=41917,nextEvent=1; Mode mode=Mode::Exploratory;
    bool fullAI=false,lodEnabled=true; float wanted=0;int32_t timeOffset=0;
    explicit World(size_t population=1024,uint32_t seed=41917,unsigned workers=1);
    void step(); void signal(Vec,float strength=1,bool crime=false);
    std::string interact(); std::string attack(); std::string consume(bool water);
    std::string rest(); std::string talk(int);
    void choose(int);void openConversation(int);int itemCount(Item)const;bool take(Item,int=1);void give(Item,int=1);
    std::string trade(Item,bool sell=false);std::string inspectSite();std::string waitHours(int);
    void initSandbox();std::string gather();std::string craft(int);std::string build(int);std::string useProject();std::string withdrawStockpile();
    int nearbyProject(Vec,int kind,float radius)const;
    std::string actorName(int)const;void note(const std::string&);void bleed(Vec,float,uint32_t);
    bool save(const std::string&)const;static std::unique_ptr<World> load(const std::string&,unsigned);
    int nearestActor(Vec,float distance=8) const;
    bool valid() const; uint64_t digest() const; size_t bytes() const;
    unsigned workers() const {return workers_.count();}
    float hour() const {return float((int64_t(tick%43200)+timeOffset+14400+4320000)%43200)/1800.f;}
};
struct BenchmarkOptions { size_t agents=4096; unsigned workers=1; uint32_t seed=41917; int ticks=600,warmup=120; bool fullAI=false,lodEnabled=true; Mode mode=Mode::Exploratory; std::string output; };
int benchmark(const BenchmarkOptions&);
int selftest();
}
