// Copyright (c) 2026 Алексей Савельев (Alex Merqury / AlexMerq).
// SPDX-License-Identifier: LicenseRef-OUT-Source-Review-1.0
#include "fiber.hpp"
#include <cmath>
#include <iostream>
#include <memory>
#include <set>
#include <stdexcept>
extern std::atomic<uint64_t> fiberAllocations;
namespace fiber {
int selftest(){
    int checks=0;auto require=[&](bool p,const char* label){if(!p)throw std::runtime_error(label);checks++;};
    Bank bank;auto pin=bank.checksum();require(sizeof(Mind)<=64,"Compact memory");require(bank.bytes()<65536,"Shared bank budget");
    std::set<int> addresses;
    for(int a=0;a<4;a++)for(int b=0;b<4;b++)for(int c=0;c<4;c++)for(int d=0;d<4;d++)for(int e=0;e<4;e++)addresses.insert(Bank::address5(a,b,c,d,e));
    require(addresses.size()==1024&&*addresses.begin()==0&&*addresses.rbegin()==1023,"Real five-dimensional addressing");
    // Dangerous alternatives must be rejected in both strict and ambiguity modes.
    for(Mode mode:{Mode::Strict,Mode::Overlap,Mode::Exploratory})for(uint32_t seed=0;seed<512;seed++){
        Mind m;Observation o;o.danger=.98f;o.health=.15f;o.critical=true;o.visible=true;o.enemy=0;o.distance=2;o.ally=1;o.aid=.8f;o.cue=2;
        auto d=bank.decide(m,o,Guard,.9f,0,seed,1,0,mode);require((d.action==Flee||d.action==Report)&&!d.ambiguous,"No unsafe ambiguity in imminent danger");
    }
    Observation friendly;friendly.danger=.7f;friendly.visible=true;friendly.enemy=1;friendly.distance=3;friendly.blockedShot=true;friendly.cue=2;
    for(int i=0;i<100;i++){Mind m;auto d=bank.decide(m,friendly,Guard,.8f,0,1,i,0,Mode::Exploratory);require(d.action!=Engage,"Friendly-body attack gate");}
    bool diversity=false;
    for(uint32_t caseId=0;caseId<1200&&!diversity;caseId++){
        Observation o;o.danger=random01(hash(61,caseId))*.65f;o.need=random01(hash(62,caseId));o.health=.5f+random01(hash(63,caseId))*.5f;o.cue=uint8_t(caseId%4);o.visible=o.cue==2;o.enemy=o.visible?0:-1;o.ally=o.cue==3?1:-1;o.aid=.5f;o.distance=15;
        std::set<int> choices;
        for(uint32_t seed=0;seed<16;seed++){Mind ma,mb;auto da=bank.decide(ma,o,Guard,.7f,0,seed,1,120,Mode::Exploratory),db=bank.decide(mb,o,Guard,.7f,0,seed,1,120,Mode::Exploratory);require(da.action==db.action&&da.branch==db.branch,"Seeded decision replay");require(Bank::allowed(da.action,o,Guard),"Safe ambiguity candidate");choices.insert(da.action);}
        diversity=choices.size()>1;
    }
    require(diversity,"Harmless ambiguity produces more than one seeded choice");
    auto pa=std::make_unique<World>(256,77,1),pb=std::make_unique<World>(256,77,4);auto& a=*pa;auto& b=*pb;a.fullAI=b.fullAI=true;a.signal(a.map.sites[0].at,2,true);b.signal(b.map.sites[0].at,2,true);
    auto before=fiberAllocations.load();
    for(int t=0;t<360;t++){a.step();b.step();}
    auto allocated=fiberAllocations.load()-before;
    require(a.digest()==b.digest(),"Replay independent of worker count");require(allocated==0,"No ordinary C++ new during warmed steps");require(a.valid()&&b.valid(),"Finite states and wall exclusion");require(a.bank.checksum()==pin,"No runtime bank training/mutation");
    // Flow must route around buildings and river, not teleport agents to destinations.
    int reachable=0;for(auto& field:a.map.flow)for(auto n:field)reachable+=n!=65535;
    require(reachable>MapW*MapH*5,"Connected destination flow fields");
    auto pos=a.player.at;a.input={1,1,true,false};for(int i=0;i<120;i++)a.step();float dx=a.player.at.x-pos.x,dy=a.player.at.y-pos.y;
    require(dx*dx+dy*dy<=14.1f*14.1f,"Diagonal/sprint speed bound");require(a.player.stamina<100,"Sprint consumes stamina");
    a.player.thirst=.9f;int water=a.player.water;a.consume(true);require(a.player.water==water-1&&a.player.thirst<.6f,"Water inventory and survival");
    a.player.at=a.map.sites[4].at;a.player.quest=1;for(int i=0;i<3;i++){a.player.cooldown=0;a.interact();}require(a.player.herbs==3,"Harvest requires actual resource proximity");
    a.player.at=a.map.sites[7].at;int gold=a.player.gold;a.interact();require(a.player.quest==2&&a.player.gold==gold+25&&a.player.herbs==0,"Quest turn-in consumes resources");
    a.player.at=a.map.sites[1].at;gold=a.player.gold;int food=a.player.food;a.interact();require(a.player.gold==gold-3&&a.player.food==food+1,"Trade accounting");
    // Different seeds must change a valid world; a benchmark cannot just return one cached scene.
    auto pc=std::make_unique<World>(256,78,1);require(pc->digest()!=b.digest(),"Seed changes simulation");
    uint64_t rumors=0,decisions=0,hits=0;auto ps=std::make_unique<World>(1024,19,4);auto& social=*ps;social.signal(social.map.sites[0].at,3,true);
    for(int i=0;i<240;i++){social.step();rumors+=social.metrics.rumors;decisions+=social.metrics.decisions;hits+=social.metrics.hits;}
    require(rumors>0&&decisions>1000,"Observed events and active cognition");require(social.valid(),"Integrated larger-world invariants");
    auto pw=std::make_unique<World>(4,41,1);auto& witness=*pw;witness.fullAI=true;
    for(size_t i=0;i<4;i++){witness.actors[i].team=0;witness.actors[i].role=Villager;witness.x[i]=witness.player.at.x+float(i+1);witness.y[i]=witness.player.at.y;}
    witness.signal(witness.player.at,1,true);witness.step();require(witness.wanted==0,"Crime is not magically known without a guard report");
    witness.actors[3].role=Guard;witness.step();require(witness.wanted>0,"Guard reports witnessed or relayed crime");
    float wanted=witness.wanted;for(int i=0;i<60;i++)witness.step();require(witness.wanted<=wanted,"Repeated memory does not duplicate an accusation");
    // Independent held-out procedural queries; approximation quality is reported, not renamed intelligence.
    int correct=0,n=4000;for(int i=0;i<n;i++){Observation o;o.danger=random01(hash(991,i));o.need=random01(hash(992,i));o.health=random01(hash(993,i));o.cue=uint8_t(i%4);o.visible=o.cue==2;o.enemy=o.visible?0:-1;o.ally=o.cue==3?1:-1;o.aid=o.ally>=0?.7f:0;o.distance=10;o.critical=o.health<.34f;Mind m;auto d=bank.decide(m,o,Guard,.7f,0,1,i,0,Mode::Strict);auto desired=Bank::teacher(o.danger,o.need,o.health,o.cue);if(!Bank::allowed(desired,o,Guard))continue;if(d.action==desired)correct++;}
    std::cout<<"PASS "<<checks<<" assertions; procedural held-out matches="<<correct<<"/"<<n<<" (includes safety-ineligible teacher cases in denominator; diagnostic only); interactions="<<hits<<"; replay="<<b.digest()<<"; bank="<<pin<<"\n";return 0;
}
}
