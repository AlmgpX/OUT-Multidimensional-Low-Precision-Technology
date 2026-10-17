// SPDX-License-Identifier: LicenseRef-OUT-Source-Review-1.0
// Copyright (c) 2026 Алексей Савельев (Alex Merqury / AlexMerq).
#include "fiber.hpp"
#include <cmath>
#include <iostream>
#include <memory>
#include <filesystem>
#include <fstream>
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
    a.input={};a.player.at=a.map.sites[4].at;a.player.quests[3]=1;a.player.quests[1]=1;for(int i=0;i<3;i++){a.player.cooldown=0;a.inspectSite();}require(a.player.herbs==3&&a.itemCount(Trace)==1,"Harvest and original trail evidence require resource proximity");
    auto findCharacter=[&](const char* id){for(size_t i=0;i<characters().size();i++)if(std::string(characters()[i].id)==id)return int(i);return -1;};
    int doctor=findCharacter("emery"),mira=findCharacter("mira"),ilia=findCharacter("ilia"),varen=findCharacter("varen"),nell=findCharacter("nell"),letta=findCharacter("letta");
    auto approach=[&](int i){require(i>=0,"Named character exists");a.actors[i].hp=100;a.player.at={a.x[i],a.y[i]};a.openConversation(i);require(a.dialogue.open,"Lore dialogue opens only in range");};
    approach(doctor);int gold=a.player.gold;a.choose(107);require(a.player.quests[3]==3&&a.player.gold==gold+21&&a.player.herbs==0,"Original Emery quest consumes herbs");
    approach(mira);gold=a.player.gold;int food=a.player.food;a.trade(Bread);require(a.player.gold<gold&&a.player.food==food+1,"Trade accounting");
    a.choose(100);require(a.player.quests[0]==1&&a.itemCount(Letter)==1,"Mira letter quest starts");approach(ilia);gold=a.player.gold;a.choose(101);require(a.player.quests[0]==3&&a.itemCount(Letter)==0&&a.player.gold==gold+19,"Letter delivered to Ilia once");a.choose(101);require(a.player.gold==gold+19,"Completed quest cannot pay twice");
    approach(varen);a.choose(103);require(a.player.quests[1]==3&&a.itemCount(Trace)==0,"Trail evidence delivered to Varen");
    approach(nell);a.choose(104);require(a.player.quests[2]==1,"Nell introduces moral choice");approach(varen);a.choose(108);require(a.player.quests[2]==3&&a.player.reputation[2]<0,"Guild job can be reported to watch");
    require(letta>=0,"Original purse owner retained");
    require(bank.chooseLife(.99f,.1f,.1f,12,Villager,.1f,0,1)==Eat,"Hunger has priority over work");
    require(bank.chooseLife(.1f,.99f,.1f,12,Villager,.1f,0,1)==Drink,"Thirst has priority over work");
    require(bank.chooseLife(.1f,.1f,.99f,12,Villager,.1f,0,1)==Sleep,"Exhaustion has priority");
    require(bank.chooseLife(.1f,.1f,.4f,23,Villager,.1f,0,1)==Sleep,"Night changes daily-life policy");
    for(auto& house:a.map.buildings)require(a.map.clear(house.bed.x,house.bed.y),"Every home has accessible interior floor");
    auto bedroom=std::make_unique<World>(1,91,1);auto& resident=bedroom->actors[0];resident.role=Villager;resident.hunger=resident.thirst=.1f;resident.fatigue=.8f;bedroom->timeOffset=15*1800;bedroom->lodEnabled=false;bedroom->x[0]=bedroom->map.buildings[resident.residence].door.x;bedroom->y[0]=bedroom->map.buildings[resident.residence].door.y;float initialFatigue=resident.fatigue;
    for(int i=0;i<1800;i++)bedroom->step();require(resident.life==Sleep&&bedroom->map.inside({bedroom->x[0],bedroom->y[0]},resident.residence),"Resident enters home through doorway to sleep");require(resident.fatigue<initialFatigue,"Sleep restores fatigue");
    auto far=std::make_unique<World>(256,31,1);far->step();require(far->metrics.farActors>0&&far->metrics.nearActors<256,"Distance tiers genuinely distribute population");
    uint64_t adaptive=0,uniform=0;for(int t=0;t<240;t++){far->step();adaptive+=far->metrics.decisions;}far->lodEnabled=false;for(int t=0;t<240;t++){far->step();uniform+=far->metrics.decisions;}require(uniform>adaptive,"Far cognition is actually less frequent");
    auto homestead=std::make_unique<World>(1,77,1);auto& home=*homestead;
    auto& woodNode=home.resources[0];require(woodNode.kind==Wood&&woodNode.stock==4,"Near arrival has deterministic gatherable wood");home.player.at=woodNode.at;home.gather();require(home.itemCount(Wood)==1&&woodNode.stock==3,"Gather consumes finite local stock");
    for(int k=0;k<3;k++){home.player.cooldown=0;home.gather();}home.player.cooldown=0;home.gather();require(woodNode.stock==0&&home.itemCount(Wood)==4,"Depleted node cannot duplicate its last unit");
    for(int k=0;k<4;k++)home.give(Item(Wood+k),80);home.player.cooldown=0;home.craft(0);require(home.itemCount(Axe)==1,"Tool recipe consumes materials and yields tool");int woodBefore=home.itemCount(Wood);home.player.cooldown=0;home.craft(0);require(home.itemCount(Wood)==woodBefore&&home.itemCount(Axe)==1,"Existing tool does not spend materials again");
    Vec ground{};bool foundGround=false;for(int yy=12;yy<60&&!foundGround;yy++)for(int xx=12;xx<60&&!foundGround;xx++){bool clear=true;for(int offY=-3;offY<=3;offY++)for(int offX=-3;offX<=3;offX++)if(home.map.tile(xx+offX,yy+offY)!=0)clear=false;if(clear){ground={(xx+.5f)*Tile,(yy+.5f)*Tile};foundGround=true;}}
    require(foundGround,"Construction fixture has free ground");home.player.at={ground.x-3,ground.y};home.input={};home.player.cooldown=0;home.build(Campfire);require(home.projects[0].active&&home.projects[0].kind==Campfire,"Campfire placed on free ground");
    woodBefore=home.itemCount(Wood);home.player.cooldown=0;home.build(Shelter);require(home.itemCount(Wood)==woodBefore&&!home.projects[1].active,"Overlapping build fails without spending materials");
    home.player.at={ground.x+5,ground.y};home.player.cooldown=0;home.build(Garden);require(home.projects[1].active&&home.projects[1].kind==Garden,"Player plants a garden");home.player.at=home.projects[1].at;home.player.cooldown=0;int waterBefore=home.player.water;home.useProject();require(home.player.water==waterBefore-1&&home.projects[1].watered,"Watering spends inventory water");
    home.waitHours(4);require(home.projects[1].growth==4,"Crop growth responds to elapsed world hours");home.player.cooldown=0;int berryBefore=home.itemCount(Berry);home.useProject();require(home.itemCount(Berry)==berryBefore+4&&home.projects[1].growth==0&&!home.projects[1].watered,"Harvest grants crop once and resets watering");home.player.cooldown=0;home.useProject();require(home.itemCount(Berry)==berryBefore+4,"Second interaction cannot duplicate harvest");
    home.projects[2]={home.player.at,0,Storage,1,0};home.projects[1].active=0;home.player.cooldown=0;woodBefore=home.itemCount(Wood);home.useProject();require(home.itemCount(Wood)==0&&home.stockpile[0]==woodBefore,"Chest transfers resources from inventory");home.withdrawStockpile();require(home.stockpile[0]==0&&home.itemCount(Wood)==woodBefore,"Chest withdrawal conserves resources");
    a.dialogue.open=false;a.input={};a.projects=home.projects;a.resources=home.resources;a.stockpile=home.stockpile;auto savePath=(std::filesystem::temp_directory_path()/"OUT-FIBER-selftest-v02.save").string();require(a.save(savePath),"World save succeeds");auto restored=World::load(savePath,4);require(restored&&restored->digest()==a.digest(),"Inventory, actors, quests, camp, resources, time and memory survive save/load");
    {std::fstream file(savePath,std::ios::in|std::ios::out|std::ios::binary);file.seekp(-1,std::ios::end);char c=123;file.write(&c,1);}require(!World::load(savePath,1),"Corrupt save is rejected");std::filesystem::remove(savePath);
    a.bleed(a.player.at,20,1);int drops=0;for(auto& drop:a.blood)drops+=drop.life>0;require(drops>0&&drops<=1024,"Blood uses bounded data pool");
    auto peers=std::make_unique<World>(2,66,1);peers->fullAI=true;
    for(int k=0;k<2;k++){auto& person=peers->actors[k];person.team=0;person.role=Villager;person.life=Socialize;person.activityUntil=100000;person.loneliness=.9f;peers->x[k]=peers->map.sites[2].at.x+float(k);peers->y[k]=peers->map.sites[2].at.y;}
    for(int k=0;k<30;k++)peers->step();require(peers->actors[0].partner==1&&peers->actors[1].partner==0,"NPCs actually choose local conversation partners");
    bool bond=false;for(auto& peerBond:peers->actors[0].bonds)bond|=peerBond.peer==1&&peerBond.meetings>0;require(bond,"Peer interaction leaves bounded relationship memory");
    auto troubled=std::make_unique<World>(1,67,1);troubled->fullAI=true;troubled->tick=600;troubled->actors[0].sanity=.15f;troubled->actors[0].stress=.9f;troubled->step();require(troubled->actors[0].mind.cue==1,"Fictional mental stress changes observed cues, not just labels");
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
