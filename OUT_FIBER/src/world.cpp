// Copyright (c) 2026 Алексей Савельев (Alex Merqury / AlexMerq).
// SPDX-License-Identifier: LicenseRef-OUT-Source-Review-1.0
#include "fiber.hpp"
#include <algorithm>
#include <cmath>
#include <chrono>
#include <cstring>
#include <limits>
#include <stdexcept>

namespace fiber {
using Clock=std::chrono::steady_clock;
static double ms(Clock::time_point t){return std::chrono::duration<double,std::milli>(Clock::now()-t).count();}
static float length(Vec a){return std::sqrt(a.x*a.x+a.y*a.y);}
static Vec normalized(Vec a){float n=length(a);return n>.001f?Vec{a.x/n,a.y/n}:Vec{};}
static bool walk(uint8_t t){return t!=2&&t!=3&&t!=4;}
uint8_t Map::tile(int x,int y)const{return x<0||x>=MapW||y<0||y>=MapH?3:tiles[y*MapW+x];}
bool Map::clear(float x,float y,float r)const{
    if(!std::isfinite(x)||!std::isfinite(y)||x-r<Tile||y-r<Tile||x+r>=Width-Tile||y+r>=Height-Tile)return false;
    return walk(tile(int((x-r)/Tile),int((y-r)/Tile)))&&walk(tile(int((x+r)/Tile),int((y-r)/Tile)))&&walk(tile(int((x-r)/Tile),int((y+r)/Tile)))&&walk(tile(int((x+r)/Tile),int((y+r)/Tile)));
}
Map::Map(uint32_t seed){
    sites={{{{193,145},"BELL SQUARE",0},{{97,97},"HEATHER MARKET",1},{{281,89},"LAST LIGHT INN",2},{{301,181},"WATCH TOWER",0},{{249,237},"HEATHER WOODS",3},{{101,225},"SALT DOCKS",4},{{61,157},"RUINED ARCHIVE",3},{{193,49},"ASH CHAPEL",2}}};
    for(int y=0;y<MapH;y++)for(int x=0;x<MapW;x++){
        auto r=hash(seed,uint32_t(x),uint32_t(y));uint8_t t=0;
        if(x==0||y==0||x==MapW-1||y==MapH-1)t=3;
        else if(x>MapW-38&&r%8==0)t=2;
        else if(r%23==0)t=6;
        if(x>70&&x<75&&y>80)t=4;
        if(y>108&&y<116&&x>67&&x<78)t=5;
        tiles[y*MapW+x]=t;
    }
    // Roads join every destination to the square; static buildings have explicit doors.
    for(auto& s:sites){int sx=int(s.at.x/Tile),sy=int(s.at.y/Tile);for(int yy=std::min(sy,72);yy<=std::max(sy,72);yy++)for(int dx=-1;dx<=1;dx++)tiles[yy*MapW+sx+dx]=1;for(int xx=std::min(sx,96);xx<=std::max(sx,96);xx++)for(int dy=-1;dy<=1;dy++)tiles[(72+dy)*MapW+xx]=1;}
    for(int by=38;by<104;by+=25)for(int bx=38;bx<153;bx+=29){
        for(int yy=by;yy<by+9;yy++)for(int xx=bx;xx<bx+12;xx++)if(tile(xx,yy)!=1)tiles[yy*MapW+xx]=(yy==by||yy==by+8||xx==bx||xx==bx+11)?3:7;
        tiles[(by+8)*MapW+bx+5]=1;tiles[(by+8)*MapW+bx+6]=1;
    }
    for(auto& s:sites){int sx=int(s.at.x/Tile),sy=int(s.at.y/Tile);for(int dy=-3;dy<=3;dy++)for(int dx=-3;dx<=3;dx++)tiles[(sy+dy)*MapW+sx+dx]=1;}
    std::array<int,MapW*MapH> queue{};
    for(size_t site=0;site<sites.size();site++){
        auto& field=flow[site];field.fill(65535);int start=int(sites[site].at.y/Tile)*MapW+int(sites[site].at.x/Tile);size_t lo=0,hi=0;field[start]=0;queue[hi++]=start;
        while(lo<hi){int at=queue[lo++];int xx=at%MapW,yy=at/MapW;for(int delta:{-1,1,-MapW,MapW}){int to=at+delta;if(to<0||to>=MapW*MapH||std::abs(to%MapW-xx)+std::abs(to/MapW-yy)!=1||!walk(tiles[to])||field[to]!=65535)continue;field[to]=uint16_t(field[at]+1);queue[hi++]=to;}}
    }
}
bool Map::line(Vec a,Vec b)const{
    float dx=b.x-a.x,dy=b.y-a.y;int n=std::max(1,int(std::max(std::abs(dx),std::abs(dy))/.8f));
    for(int i=1;i<=n;i++){float t=float(i)/n;if(!clear(a.x+dx*t,a.y+dy*t,.05f))return false;}return true;
}
int Map::nearest(Vec a,int kind)const{int best=0;float d=1e30f;for(int i=0;i<8;i++){if(kind>=0&&sites[i].kind!=kind)continue;float dx=a.x-sites[i].at.x,dy=a.y-sites[i].at.y,dist=dx*dx+dy*dy;if(dist<d){d=dist;best=i;}}return best;}
Vec Map::direction(Vec a,int target)const{
    target=std::max(0,std::min(7,target));int xx=int(a.x/Tile),yy=int(a.y/Tile),at=yy*MapW+xx;if(at<0||at>=MapW*MapH)return {};
    auto& f=flow[target];uint16_t best=f[at];int chosen=at;
    for(int dy=-1;dy<=1;dy++)for(int dx=-1;dx<=1;dx++){int nx=xx+dx,ny=yy+dy;if(nx<1||nx>=MapW-1||ny<1||ny>=MapH-1)continue;int j=ny*MapW+nx;if(f[j]<best&&clear((nx+.5f)*Tile,(ny+.5f)*Tile)&&(!dx||!dy||(walk(tile(nx,yy))&&walk(tile(xx,ny))))){best=f[j];chosen=j;}}
    if(chosen==at)return normalized({sites[target].at.x-a.x,sites[target].at.y-a.y});
    return normalized({(chosen%MapW+.5f)*Tile-a.x,(chosen/MapW+.5f)*Tile-a.y});
}
int World::cell(float xx,float yy)const{return std::max(0,std::min(GridH-1,int(yy/8)))*GridW+std::max(0,std::min(GridW-1,int(xx/8)));}
World::World(size_t population,uint32_t worldSeed,unsigned workers):workers_(workers),map(worldSeed),seed(worldSeed){
    if(population<1||population>131072)throw std::invalid_argument("Population must be 1..131072");
    x.resize(population);y.resize(population);vx.resize(population);vy.resize(population);actors.resize(population);
    nextX_.resize(population);nextY_.resize(population);fearBefore_.resize(population);rumorBefore_.resize(population);intents_.resize(population);local_.resize(population);order_.resize(population);cursor_.resize(GridW*GridH);
    player.at=map.sites[0].at;
    for(size_t i=0;i<population;i++){
        auto& a=actors[i];uint32_t r=hash(seed,uint32_t(i));int k=int(i%20);a.role=k<3?Bandit:k<5?Wolf:k<9?Guard:k==9?Healer:k<12?Merchant:Villager;a.team=a.role==Bandit?1:a.role==Wolf?2:0;a.courage=.2f+random01(hash(r,1))*.8f;a.home=uint8_t(hash(r,3)%8);a.coins=uint16_t(5+r%50);a.hunger=random01(hash(r,5))*.6f;a.thirst=random01(hash(r,6))*.6f;a.fatigue=random01(hash(r,7))*.5f;a.mind.phase=uint8_t(r%16);
        float xx=0,yy=0;uint32_t attempt=0;
        do{uint32_t v=hash(r,attempt++);xx=4+random01(v)*(Width-8);yy=4+random01(hash(v,9))*(Height-8);}while(!map.clear(xx,yy));x[i]=xx;y[i]=yy;a.goal=map.sites[a.home].at;
    }
    grid();
}
void World::grid(){
    offsets_.fill(0);for(size_t i=0;i<actors.size();i++)if(actors[i].hp>0)offsets_[cell(x[i],y[i])+1]++;
    for(size_t i=1;i<offsets_.size();i++)offsets_[i]+=offsets_[i-1];std::copy(offsets_.begin(),offsets_.end()-1,cursor_.begin());
    for(size_t i=0;i<actors.size();i++)if(actors[i].hp>0)order_[cursor_[cell(x[i],y[i])]++]=uint32_t(i);
}
void World::think(size_t begin,size_t end){
    for(size_t i=begin;i<end;i++){
        auto& a=actors[i];auto& m=local_[i];if(a.hp<=0||(!fullAI&&(tick+i)%6!=0))continue;
        auto remember=[&](uint32_t id){if(!id||std::find(a.recentEvents.begin(),a.recentEvents.end(),id)!=a.recentEvents.end())return;a.recentEvents[a.eventHead++%4]=id;a.rumor=id;m.rumors++;};
        Observation o;o.health=a.hp/100;o.need=std::max(a.hunger,std::max(a.thirst,a.fatigue));o.critical=a.hp<34;
        int cx=int(x[i]/8),cy=int(y[i]/8);float enemyDist=24*24,allyDist=20*20,alarm=0;int social=0,probes=0;
        // Rotate the starting bucket for fairness without changing replay or thread ordering.
        int shift=int(hash(seed,uint32_t(i),tick/6)%25);
        for(int search=0;search<25;search++){
            int s=(search+shift)%25,xx=cx+s%5-2,yy=cy+s/5-2;if(xx<0||yy<0||xx>=GridW||yy>=GridH)continue;
            int c=yy*GridW+xx;uint32_t lo=offsets_[c],hi=offsets_[c+1];if(lo==hi)continue;
            uint32_t offset=hash(seed,uint32_t(i),tick/6,uint32_t(c))%(hi-lo);
            for(uint32_t n=0;n<hi-lo;n++){
                uint32_t j=order_[lo+(n+offset)%(hi-lo)];if(j==i)continue;
                if(++probes>192){m.capped++;break;}
                float dx=x[j]-x[i],dy=y[j]-y[i],d=dx*dx+dy*dy;
                if(actors[j].team!=a.team&&d<enemyDist){o.enemy=int(j);enemyDist=d;}
                if(actors[j].team==a.team){if(actors[j].hp<60&&d<allyDist){o.ally=int(j);allyDist=d;}if(d<100){alarm+=fearBefore_[j];social++;remember(rumorBefore_[j]);}}
            }
            if(probes>192)break;
        }
        m.probes+=std::min(probes,192);
        float pdx=player.at.x-x[i],pdy=player.at.y-y[i],pd=pdx*pdx+pdy*pdy;
        if((a.team>0||(a.role==Guard&&wanted>0))&&player.hp>0&&pd<enemyDist){o.enemy=int(actors.size());enemyDist=pd;}
        Vec enemy{};
        if(o.enemy>=0){enemy=o.enemy==int(actors.size())?player.at:Vec{x[o.enemy],y[o.enemy]};o.distance=std::sqrt(enemyDist);o.visible=map.line({x[i],y[i]},enemy)&&(!player.sneaking||o.enemy!=int(actors.size())||o.distance<10);}
        if(o.visible){o.danger=clamp(1-o.distance/26);a.clue=enemy;a.lastSeen=tick;o.cue=2;}
        else if(tick-a.lastSeen<300&&a.lastSeen){o.danger=.24f;o.cue=1;o.clue=a.clue;}
        for(auto& event:events)if(event.id&&event.until>tick){float dx=x[i]-event.at.x,dy=y[i]-event.at.y,d=dx*dx+dy*dy;float radius=event.strength*28;if(d<radius*radius){float threat=event.strength*(1-std::sqrt(d)/radius)*.55f;if(threat>o.danger){o.danger=threat;o.cue=1;a.clue=event.at;a.lastSeen=tick;}remember(event.id);}if(event.crime&&a.role==Guard&&std::find(a.recentEvents.begin(),a.recentEvents.end(),event.id)!=a.recentEvents.end())a.mind.eventId=event.id;}
        a.mind.fear=clamp(fearBefore_[i]*.94f+o.danger*.05f+(social?alarm/social*.008f:0));o.danger=std::max(o.danger,a.mind.fear*.7f);o.social=social?float(social)/12:0;
        if(o.ally>=0){o.aid=(1-std::sqrt(allyDist)/20)*(1-actors[o.ally].hp/100);if(o.aid>.18f&&!o.visible)o.cue=3;}
        if(o.visible&&a.team==0){
            // Friendly body gate for local attacks, not a global O(N) ray test.
            int c=cell(x[i],y[i]);for(uint32_t k=offsets_[c];k<offsets_[c+1]&&k<offsets_[c]+48;k++){auto j=order_[k];if(j==i||actors[j].team!=a.team)continue;Vec da{x[j]-x[i],y[j]-y[i]},db{enemy.x-x[i],enemy.y-y[i]};float len=db.x*db.x+db.y*db.y;float t=len>0?(da.x*db.x+da.y*db.y)/len:0;float ex=da.x-db.x*t,ey=da.y-db.y*t;if(t>0&&t<1&&ex*ex+ey*ey<.36f)o.blockedShot=true;}
        }
        int old=a.mind.action;auto decision=bank.decide(a.mind,o,Role(a.role),a.courage,a.affinity,seed,uint32_t(i),tick,mode);
        m.decisions++;m.ambiguous+=decision.ambiguous;m.safetyBlocks+=decision.blocked;m.held+=decision.held;m.switches+=old!=decision.action;
        a.task=0;
        switch(decision.action){
        case Routine:{int destination=a.home;float h=hour();if(a.thirst>.63f){destination=5;a.task=2;}else if(a.hunger>.65f){destination=1;a.task=1;}else if(a.fatigue>.72f||h>21){destination=2;a.task=3;}else if(a.role==Merchant){destination=1;a.task=4;}else if(a.role==Guard)destination=3;else if(a.role==Healer)destination=7;else destination=int(hash(seed,uint32_t(i),tick/900)%8);a.goal=map.sites[destination].at;break;}
        case Investigate:a.goal=a.clue;break;
        case Engage:if(o.enemy>=0)a.goal=enemy;break;
        case Aid:if(o.ally>=0)a.goal={x[o.ally],y[o.ally]};break;
        case Flee:{Vec d=normalized({x[i]-enemy.x,y[i]-enemy.y});if(o.enemy<0)d=normalized({x[i]-a.clue.x,y[i]-a.clue.y});a.goal={clamp(x[i]+d.x*22,4,Width-4),clamp(y[i]+d.y*22,4,Height-4)};break;}
        case Report:a.goal=map.sites[a.team?6:3].at;break;
        }
        // Applying interactions after the parallel phases keeps results independent of worker count.
        if(decision.action==Engage&&o.enemy>=0&&o.distance<2.8f&&(tick+uint32_t(i))%30==0){intents_[i]={o.enemy,7.f,0};}
        if(decision.action==Aid&&o.ally>=0&&allyDist<9&&(tick+uint32_t(i))%12==0)intents_[i]={o.ally,0,4.f};
    }
}
void World::move(size_t begin,size_t end){
    for(size_t i=begin;i<end;i++){
        auto& a=actors[i];nextX_[i]=x[i];nextY_[i]=y[i];if(a.hp<=0){vx[i]=vy[i]=0;continue;}
        a.hunger=clamp(a.hunger+.0012f*Dt);a.thirst=clamp(a.thirst+.0018f*Dt);a.fatigue=clamp(a.fatigue+.0008f*Dt);
        Vec delta{a.goal.x-x[i],a.goal.y-y[i]};float dist=length(delta);Vec direction{};int site=-1;
        for(int k=0;k<8;k++)if(a.goal.x==map.sites[k].at.x&&a.goal.y==map.sites[k].at.y){site=k;break;}
        if(dist>.7f){direction=site>=0?map.direction({x[i],y[i]},site):normalized(delta);}
        int c=cell(x[i],y[i]),cx=c%GridW,cy=c/GridW,probes=0;Vec avoid{};float crowd=0;
        for(int yy=std::max(0,cy-1);yy<=std::min(GridH-1,cy+1);yy++)for(int xx=std::max(0,cx-1);xx<=std::min(GridW-1,cx+1);xx++){
            int bucket=yy*GridW+xx;
            for(uint32_t k=offsets_[bucket];k<offsets_[bucket+1];k++){
                auto j=order_[k];if(j==i)continue;if(++probes>96)break;
                float dx=x[i]-x[j],dy=y[i]-y[j],d=dx*dx+dy*dy;if(d<4)crowd+=.04f;
                if(d<.9f&&d>.0001f){float weight=(.95f-std::sqrt(d))/(d+.02f);avoid.x+=dx*weight;avoid.y+=dy*weight;}
            }
        }
        auto motor=bank.motion(Action(a.mind.action),crowd,a.fatigue,a.mind.confidence);float speed=a.mind.action==Flee?5.8f:a.role==Wolf?3.8f:2.7f;
        speed*=float(motor.speed)/110*(.7f+a.mind.intensity*.5f);float av=float(motor.avoidance)/127;
        Vec target{direction.x*speed+clamp(avoid.x,-2,2)*av,direction.y*speed+clamp(avoid.y,-2,2)*av};float acceleration=(2+(motor.response+motor.steer*a.mind.correction)/24.f)*Dt;
        vx[i]+=(target.x-vx[i])*std::min(1.f,acceleration);vy[i]+=(target.y-vy[i])*std::min(1.f,acceleration);
        float nx=x[i]+vx[i]*Dt,ny=y[i]+vy[i]*Dt;
        // Axis-separated swept substeps: maximum travel below the wall tile width.
        if(map.clear(nx,y[i]))nextX_[i]=nx;else{vx[i]=0;local_[i].collisions++;}
        if(map.clear(nextX_[i],ny))nextY_[i]=ny;else{vy[i]=0;local_[i].collisions++;}
        if(site>=0&&dist<3&&a.mind.action==Routine){
            if(a.task==1){a.hunger=std::max(0.f,a.hunger-.1f*Dt);if(tick%120==i%120&&a.coins)a.coins--;}
            if(a.task==2)a.thirst=std::max(0.f,a.thirst-.15f*Dt);
            if(a.task==3){a.fatigue=std::max(0.f,a.fatigue-.1f*Dt);a.hp=std::min(100.f,a.hp+.5f*Dt);}
            if(a.task==4&&tick%300==i%300&&a.coins<1000){a.coins++;local_[i].arrivals++;}
        }
        if(a.hunger>.98f||a.thirst>.98f)a.hp=std::max(0.f,a.hp-.5f*Dt);
    }
}
void World::step(){
    auto total=Clock::now();metrics={};std::fill(local_.begin(),local_.end(),Metrics{});std::fill(intents_.begin(),intents_.end(),Intent{});
    for(size_t i=0;i<actors.size();i++){fearBefore_[i]=actors[i].mind.fear;rumorBefore_[i]=actors[i].rumor;}
    auto start=Clock::now();grid();metrics.gridMs=ms(start);
    start=Clock::now();workers_.run(actors.size(),[this](size_t a,size_t b){think(a,b);});metrics.thinkMs=ms(start);
    start=Clock::now();workers_.run(actors.size(),[this](size_t a,size_t b){move(a,b);});metrics.moveMs=ms(start);
    start=Clock::now();x.swap(nextX_);y.swap(nextY_);
    for(size_t i=0;i<actors.size();i++){
        auto& intent=intents_[i];if(intent.target>=0){if(intent.target==int(actors.size())){player.hp=std::max(0.f,player.hp-intent.damage);metrics.hits+=intent.damage>0;}
            else if(intent.target<int(actors.size())){auto& to=actors[intent.target];if(to.hp>0){to.hp=clamp(to.hp-intent.damage+intent.heal,0,100);metrics.hits+=intent.damage>0;metrics.heals+=intent.heal>0;}}}
        auto& m=local_[i];metrics.decisions+=m.decisions;metrics.ambiguous+=m.ambiguous;metrics.safetyBlocks+=m.safetyBlocks;metrics.held+=m.held;metrics.switches+=m.switches;metrics.probes+=m.probes;metrics.capped+=m.capped;metrics.collisions+=m.collisions;metrics.rumors+=m.rumors;metrics.arrivals+=m.arrivals;
        if(actors[i].role==Guard&&actors[i].mind.eventId)for(auto& event:events)if(event.id==actors[i].mind.eventId&&event.crime&&!event.reported){event.reported=true;wanted=std::min(100.f,wanted+15);}
    }
    if(player.hp>0){
        Vec d=normalized({clamp(input.x,-1,1),clamp(input.y,-1,1)});bool running=input.sprint&&player.stamina>1;float speed=input.sneak?1.7f:running?7.f:4.f;player.sneaking=input.sneak;
        float nx=player.at.x+d.x*speed*Dt,ny=player.at.y+d.y*speed*Dt;if(map.clear(nx,player.at.y))player.at.x=nx;if(map.clear(player.at.x,ny))player.at.y=ny;
        player.stamina=clamp(player.stamina+((running&&length(d)>.1f)?-18.f:12.f)*Dt,0,100);
        player.hunger=clamp(player.hunger+.002f*Dt);player.thirst=clamp(player.thirst+.003f*Dt);player.cooldown=std::max(0.f,player.cooldown-Dt);
        player.warmth=clamp(player.warmth+((hour()>20||hour()<6)?-.003f:.004f)*Dt);
        if(player.hunger>.95f||player.thirst>.95f||player.warmth<.05f)player.hp=std::max(0.f,player.hp-2*Dt);
    }
    wanted=std::max(0.f,wanted-.002f*Dt);tick++;metrics.commitMs=ms(start);metrics.totalMs=ms(total);
}
void World::signal(Vec at,float strength,bool crime){auto& e=events[nextEvent%events.size()];e={at,nextEvent++,tick+360,clamp(strength,.1f,3),crime,false};}
int World::nearestActor(Vec p,float distance)const{int best=-1;float d=distance*distance;for(size_t i=0;i<actors.size();i++)if(actors[i].hp>0){float dx=x[i]-p.x,dy=y[i]-p.y,q=dx*dx+dy*dy;if(q<d){d=q;best=int(i);}}return best;}
std::string World::consume(bool water){if(player.hp<=0)return "You have fallen. F9 starts a fresh world.";int& n=water?player.water:player.food;if(n<=0)return water?"No water. Visit the salt docks.":"No food. Visit Heather Market.";n--;if(water)player.thirst=std::max(0.f,player.thirst-.4f);else player.hunger=std::max(0.f,player.hunger-.35f);return water?"You drink water.":"You eat rye bread.";}
std::string World::rest(){if(player.hp<=0)return "You have fallen. F9 restarts.";if(player.cooldown>0)return "Wait before resting again.";if(wanted>0)return "The watch is looking for you. Rest is unsafe.";int site=map.nearest(player.at,2);if(length({player.at.x-map.sites[site].at.x,player.at.y-map.sites[site].at.y})>8)return "Find the inn or chapel before resting.";player.hp=std::min(100.f,player.hp+20);player.stamina=100;player.warmth=1;player.cooldown=2;return "A short rest at the hearth restores health and warmth.";}
std::string World::talk(int i){if(i<0||i>=int(actors.size())||actors[i].hp<=0)return "No one is close enough.";auto& a=actors[i];if(length({x[i]-player.at.x,y[i]-player.at.y})>8)return "Move closer to talk.";if(a.team)return "The hostile creature refuses to talk.";a.affinity=clamp(a.affinity+.12f,-1,1);if(player.quest==0){player.quest=1;return "Find heather in the eastern woods, then bring three herbs to the chapel.";}if(a.mind.fear>.4f)return "Someone raised the alarm. The watch is following the rumors.";return a.role==Merchant?"A loaf is 3 gold. Supplies are sold at Heather Market.":a.role==Guard?"Witnesses remember. Keep your blade sheathed in town.":"The last light burns at the inn. The forest grows restless.";}
std::string World::interact(){
    if(player.hp<=0)return "You have fallen. F9 restarts.";int site=map.nearest(player.at);float d=length({player.at.x-map.sites[site].at.x,player.at.y-map.sites[site].at.y});
    if(d<7){
        if(site==0){signal(player.at,2);return "The bell carries a local alarm. Citizens remember and report it.";}
        if(site==1){if(player.gold<3)return "You need 3 gold for bread.";player.gold-=3;player.food++;return "Bought one rye loaf for 3 gold.";}
        if(site==5){player.water=std::min(20,player.water+3);return "Filled three waterskins.";}
        if(site==4){if(player.cooldown>0)return "Gathering: wait a moment.";player.cooldown=1;player.herbs++;return "Gathered heather. Three herbs complete the chapel task.";}
        if(site==7&&player.quest==1&&player.herbs>=3){player.herbs-=3;player.gold+=25;player.quest=2;return "Delivered heather to the chapel. Earned 25 gold and trust.";}
    }
    int i=nearestActor(player.at,6);if(i>=0&&actors[i].team==0&&actors[i].hp<80&&player.herbs>0){actors[i].hp=std::min(100.f,actors[i].hp+30);player.herbs--;actors[i].affinity=clamp(actors[i].affinity+.3f,-1,1);return "You treat the wounded. They remember your help.";}
    return talk(i);
}
std::string World::attack(){if(player.hp<=0||player.cooldown>0||player.stamina<10)return "Cannot attack yet.";int i=nearestActor(player.at,4);player.cooldown=.4f;player.stamina-=10;if(i<0||!map.line(player.at,{x[i],y[i]}))return "Your blade finds no reachable target.";actors[i].hp=std::max(0.f,actors[i].hp-24);actors[i].clue=player.at;actors[i].lastSeen=tick;actors[i].affinity=-1;signal(player.at,1,actors[i].team==0);return actors[i].team?"You strike a hostile target.":"Assault: nearby witnesses can spread the report to the watch.";}
bool World::valid()const{if(!std::isfinite(player.hp)||!map.clear(player.at.x,player.at.y))return false;for(size_t i=0;i<x.size();i++)if(!std::isfinite(x[i])||!std::isfinite(y[i])||!std::isfinite(vx[i])||!std::isfinite(vy[i])||!std::isfinite(actors[i].hp)||!map.clear(x[i],y[i]))return false;return true;}
uint64_t World::digest()const{
    uint64_t h=1469598103934665603ull;auto add=[&](uint32_t u){for(int k=0;k<4;k++)h=(h^uint8_t(u>>(k*8)))*1099511628211ull;};auto number=[&](float v){uint32_t u;std::memcpy(&u,&v,4);add(u);};
    add(tick);add(seed);add(uint32_t(mode));add(fullAI);number(wanted);add(nextEvent);
    for(size_t i=0;i<x.size();i++){
        number(x[i]);number(y[i]);number(vx[i]);number(vy[i]);auto& a=actors[i];
        number(a.hp);number(a.hunger);number(a.thirst);number(a.fatigue);number(a.courage);number(a.affinity);number(a.goal.x);number(a.goal.y);number(a.clue.x);number(a.clue.y);
        add(a.lastSeen);add(a.rumor);for(auto e:a.recentEvents)add(e);add(a.eventHead);add(a.coins);add(a.food);add(a.role);add(a.team);add(a.home);add(a.task);
        auto& m=a.mind;for(auto f:m.inhibition)number(f);number(m.fear);number(m.confidence);number(m.intensity);number(m.correction);add(m.holdUntil);add(m.eventId);add(m.branch);add(m.action);add(m.cue);add(m.phase);add(m.flags);for(auto b:m.previous)add(b);add(m.focus);
    }
    for(auto& e:events){number(e.at.x);number(e.at.y);add(e.id);add(e.until);number(e.strength);add(e.crime);add(e.reported);}
    number(player.hp);number(player.at.x);number(player.at.y);number(player.stamina);number(player.hunger);number(player.thirst);number(player.warmth);number(player.cooldown);add(player.food);add(player.water);add(player.gold);add(player.herbs);add(player.quest);add(player.sneaking);return h;
}
size_t World::bytes()const{return sizeof(*this)+actors.capacity()*sizeof(Actor)+(x.capacity()+y.capacity()+vx.capacity()+vy.capacity()+nextX_.capacity()+nextY_.capacity()+fearBefore_.capacity())*sizeof(float)+(order_.capacity()+cursor_.capacity()+rumorBefore_.capacity())*sizeof(uint32_t)+intents_.capacity()*sizeof(Intent)+local_.capacity()*sizeof(Metrics);}
}
