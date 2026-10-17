// SPDX-License-Identifier: LicenseRef-OUT-Source-Review-1.0
// Copyright (c) 2026 Алексей Савельев (Alex Merqury / AlexMerq).
#include "fiber.hpp"
#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <new>
#include <sstream>

std::atomic<uint64_t> fiberAllocations{0};
void* operator new(std::size_t n){if(void* p=std::malloc(n?n:1)){fiberAllocations.fetch_add(1,std::memory_order_relaxed);return p;}throw std::bad_alloc();}
void* operator new[](std::size_t n){return ::operator new(n);}
void operator delete(void* p)noexcept{std::free(p);}
void operator delete[](void* p)noexcept{std::free(p);}
void operator delete(void* p,std::size_t)noexcept{std::free(p);}
void operator delete[](void* p,std::size_t)noexcept{std::free(p);}
namespace fiber {
static double percentile(std::vector<double> values,double q){std::sort(values.begin(),values.end());return values[size_t(q*double(values.size()-1))];}
int benchmark(const BenchmarkOptions& o){
    if(o.ticks<1||o.ticks>100000||o.warmup<0)throw std::invalid_argument("Invalid benchmark tick count");
    World world(o.agents,o.seed,o.workers);world.fullAI=o.fullAI;world.lodEnabled=o.lodEnabled;world.mode=o.mode;
    world.signal(world.map.sites[0].at,2,true);
    for(int i=0;i<o.warmup;i++)world.step();
    std::vector<double> times(size_t(o.ticks),0);Metrics sum{};uint64_t allocationCount=0;
    auto wall=std::chrono::steady_clock::now();
    for(int i=0;i<o.ticks;i++){
        if(i%180==0)world.signal(world.map.sites[(i/180)%8].at,1.5f);
        auto before=fiberAllocations.load();world.step();allocationCount+=fiberAllocations.load()-before;
        times[size_t(i)]=world.metrics.totalMs;auto& m=world.metrics;
        sum.totalMs+=m.totalMs;sum.gridMs+=m.gridMs;sum.thinkMs+=m.thinkMs;sum.moveMs+=m.moveMs;sum.commitMs+=m.commitMs;
        sum.decisions+=m.decisions;sum.probes+=m.probes;sum.capped+=m.capped;sum.hits+=m.hits;sum.heals+=m.heals;sum.rumors+=m.rumors;sum.arrivals+=m.arrivals;sum.safetyBlocks+=m.safetyBlocks;sum.ambiguous+=m.ambiguous;sum.switches+=m.switches;sum.socials+=m.socials;
    }
    double seconds=std::chrono::duration<double>(std::chrono::steady_clock::now()-wall).count();
    size_t alive=0;std::array<uint64_t,Actions> distribution{};for(auto& a:world.actors)if(a.hp>0){alive++;distribution[a.mind.action]++;}
    bool valid=world.valid();std::ostringstream out;out<<std::fixed<<std::setprecision(6);
    out<<"{\n  \"format\": \"OUT-FIBER-NATIVE-REPORT-2\",\n  \"moduleVersion\": \"0.2\",\n";
    auto field=[&](const char* name,auto value){out<<"  \""<<name<<"\": "<<value<<",\n";};
    field("agents",o.agents);field("aliveAtEnd",alive);field("seed",o.seed);field("workers",world.workers());
    out<<"  \"mode\": \""<<modeName(o.mode)<<"\",\n";
    field("fullAI",o.fullAI?"true":"false");field("distanceLOD",o.lodEnabled?"true":"false");field("simulationHz",60);
    field("nearCognitionHz",o.fullAI?60:10);field("midCognitionHz",o.fullAI?60:o.lodEnabled?2:10);field("farCognitionHz",o.fullAI?60.f:o.lodEnabled?.25f:10.f);
    field("worldWidthMeters",Width);field("worldHeightMeters",Height);field("homes",world.map.buildings.size());
    field("warmupTicks",o.warmup);field("measuredTicks",o.ticks);field("wallSeconds",seconds);
    out<<"  \"tickMs\": {\"p50\": "<<percentile(times,.5)<<", \"p95\": "<<percentile(times,.95)<<", \"p99\": "<<percentile(times,.99)<<", \"max\": "<<*std::max_element(times.begin(),times.end())<<"},\n";
    out<<"  \"meanStagesMs\": {\"grid\": "<<sum.gridMs/o.ticks<<", \"decision\": "<<sum.thinkMs/o.ticks<<", \"movement\": "<<sum.moveMs/o.ticks<<", \"commit\": "<<sum.commitMs/o.ticks<<"},\n";
    field("actualDecisions",sum.decisions);field("decisionsPerWallSecond",sum.decisions/seconds);field("neighborProbes",sum.probes);field("cappedPerceptions",sum.capped);
    field("safetyFilteredDecisions",sum.safetyBlocks);field("ambiguousDecisions",sum.ambiguous);field("actionSwitches",sum.switches);field("hits",sum.hits);field("heals",sum.heals);field("rumorReceptions",sum.rumors);field("workIncomeEvents",sum.arrivals);field("socialMeetings",sum.socials);
    field("nearActorsAtEnd",world.metrics.nearActors);field("midActorsAtEnd",world.metrics.midActors);field("farActorsAtEnd",world.metrics.farActors);field("sleepersAtEnd",world.metrics.sleepers);
    field("bankBytes",world.bank.bytes());field("mindBytes",sizeof(Mind));field("actorBytes",sizeof(Actor));field("ownedSimulationBytes",world.bytes());field("trackedHeapAllocationsDuringSteps",allocationCount);field("valid",valid?"true":"false");
    out<<"  \"bankChecksum\": \""<<world.bank.checksum()<<"\",\n  \"replayDigest\": \""<<world.digest()<<"\",\n  \"aliveActionCounts\": [";
    for(int i=0;i<Actions;i++)out<<(i?", ":"")<<distribution[i];
    std::array<uint64_t,LifeCount> life{};for(auto& a:world.actors)if(a.hp>0)life[a.life]++;
    out<<"],\n  \"aliveLifeCounts\": [";for(int i=0;i<LifeCount;i++)out<<(i?", ":"")<<life[i];
    out<<"],\n  \"scope\": \"Tales 0.2 headless CPU simulation, no rendering. New larger calmer world: not directly comparable to v0.1. Distance tiers and sleep reduce update cadence unless fullAI is enabled. No paid asset data. Allocation counter covers ordinary C++ new/new[]; owned memory excludes stacks, allocator, OS and GUI.\"\n}\n";
    if(!o.output.empty()){std::ofstream file(o.output,std::ios::binary);if(!file)throw std::runtime_error("Cannot write report");file<<out.str();}
    std::cout<<out.str();return valid&&allocationCount==0?0:2;
}
}
