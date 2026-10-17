// Copyright (c) 2026 Алексей Савельев (Alex Merqury / AlexMerq).
// SPDX-License-Identifier: LicenseRef-OUT-Source-Review-1.0
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
    World world(o.agents,o.seed,o.workers);world.fullAI=o.fullAI;world.mode=o.mode;
    world.signal(world.map.sites[0].at,2,true);
    for(int i=0;i<o.warmup;i++)world.step();
    std::vector<double> times(size_t(o.ticks),0);Metrics sum{};uint64_t allocationCount=0;
    auto wall=std::chrono::steady_clock::now();
    for(int i=0;i<o.ticks;i++){
        if(i%180==0)world.signal(world.map.sites[(i/180)%8].at,1.5f);
        auto before=fiberAllocations.load();world.step();allocationCount+=fiberAllocations.load()-before;
        times[size_t(i)]=world.metrics.totalMs;auto& m=world.metrics;
        sum.totalMs+=m.totalMs;sum.gridMs+=m.gridMs;sum.thinkMs+=m.thinkMs;sum.moveMs+=m.moveMs;sum.commitMs+=m.commitMs;
        sum.decisions+=m.decisions;sum.probes+=m.probes;sum.capped+=m.capped;sum.hits+=m.hits;sum.heals+=m.heals;sum.rumors+=m.rumors;sum.arrivals+=m.arrivals;sum.safetyBlocks+=m.safetyBlocks;sum.ambiguous+=m.ambiguous;sum.switches+=m.switches;
    }
    double seconds=std::chrono::duration<double>(std::chrono::steady_clock::now()-wall).count();
    size_t alive=0;std::array<uint64_t,Actions> distribution{};for(auto& a:world.actors)if(a.hp>0){alive++;distribution[a.mind.action]++;}
    bool valid=world.valid();std::ostringstream out;out<<std::fixed<<std::setprecision(6);
    out<<"{\n  \"format\": \"OUT-FIBER-NATIVE-REPORT-1\",\n  \"agents\": "<<o.agents<<",\n  \"aliveAtEnd\": "<<alive<<",\n  \"seed\": "<<o.seed<<",\n  \"workers\": "<<world.workers()<<",\n  \"mode\": \""<<modeName(o.mode)<<"\",\n  \"fullAI\": "<<(o.fullAI?"true":"false")<<",\n  \"simulationHz\": 60,\n  \"nominalCognitionHz\": "<<(o.fullAI?60:10)<<",\n  \"warmupTicks\": "<<o.warmup<<",\n  \"measuredTicks\": "<<o.ticks<<",\n  \"wallSeconds\": "<<seconds<<",\n  \"tickMs\": {\"p50\": "<<percentile(times,.5)<<", \"p95\": "<<percentile(times,.95)<<", \"p99\": "<<percentile(times,.99)<<", \"max\": "<<*std::max_element(times.begin(),times.end())<<"},\n  \"meanStagesMs\": {\"grid\": "<<sum.gridMs/o.ticks<<", \"decision\": "<<sum.thinkMs/o.ticks<<", \"movement\": "<<sum.moveMs/o.ticks<<", \"commit\": "<<sum.commitMs/o.ticks<<"},\n  \"actualDecisions\": "<<sum.decisions<<",\n  \"decisionsPerWallSecond\": "<<sum.decisions/seconds<<",\n  \"neighborProbes\": "<<sum.probes<<",\n  \"cappedPerceptions\": "<<sum.capped<<",\n  \"safetyFilteredDecisions\": "<<sum.safetyBlocks<<",\n  \"ambiguousDecisions\": "<<sum.ambiguous<<",\n  \"actionSwitches\": "<<sum.switches<<",\n  \"hits\": "<<sum.hits<<",\n  \"heals\": "<<sum.heals<<",\n  \"rumorReceptions\": "<<sum.rumors<<",\n  \"trades\": "<<sum.arrivals<<",\n  \"bankBytes\": "<<world.bank.bytes()<<",\n  \"mindBytes\": "<<sizeof(Mind)<<",\n  \"ownedSimulationBytes\": "<<world.bytes()<<",\n  \"trackedHeapAllocationsDuringSteps\": "<<allocationCount<<",\n  \"bankChecksum\": \""<<world.bank.checksum()<<"\",\n  \"replayDigest\": \""<<world.digest()<<"\",\n  \"valid\": "<<(valid?"true":"false")<<",\n  \"aliveActionCounts\": [";
    for(int i=0;i<Actions;i++)out<<(i?", ":"")<<distribution[i];
    out<<"],\n  \"scope\": \"Headless native CPU simulation, no rendering. Procedural benchmark policy, not paid asset data. Timings are not FPS or proof of general AI quality. Allocation counter covers ordinary C++ new/new[] only; memory excludes allocator, thread stacks, OS and GUI.\"\n}\n";
    if(!o.output.empty()){std::ofstream file(o.output,std::ios::binary);if(!file)throw std::runtime_error("Cannot write report");file<<out.str();}
    std::cout<<out.str();return valid&&allocationCount==0?0:2;
}
}
