// SPDX-License-Identifier: LicenseRef-OUT-Source-Review-1.0
// Copyright (c) 2026 Алексей Савельев (Alex Merqury / AlexMerq).
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
    tiles.resize(MapW*MapH);
    sites={{{{Width*.5f,Height*.52f},"BELL SQUARE",0},{{Width*.23f,Height*.2f},"HEATHER MARKET",1},{{Width*.76f,Height*.2f},"LAST LIGHT INN",2},{{Width*.85f,Height*.54f},"WATCH TOWER",0},{{Width*.67f,Height*.83f},"HEATHER WOODS",3},{{Width*.32f,Height*.83f},"SALT DOCKS",4},{{Width*.14f,Height*.55f},"RUINED ARCHIVE",3},{{Width*.5f,Height*.13f},"ASH CHAPEL",2}}};
    for(int yy=0;yy<MapH;yy++)for(int xx=0;xx<MapW;xx++){
        auto r=hash(seed,uint32_t(xx),uint32_t(yy));uint8_t t=0;
        if(xx==0||yy==0||xx==MapW-1||yy==MapH-1)t=3;
        else if((xx>MapW*.56f||yy>MapH*.7f)&&r%14==0)t=2;
        else if(r%29==0)t=6;
        int river=int(MapW*.36f)+int(std::sin(yy*.032)*5);if(xx>river&&xx<river+6&&yy>MapH*.34f)t=4;
        tiles[yy*MapW+xx]=t;
    }
    auto road=[&](int x0,int y0,int x1,int y1){
        for(int yy=std::min(y0,y1)-1;yy<=std::max(y0,y1)+1;yy++)for(int xx=x0-1;xx<=x0+1;xx++)if(xx>0&&yy>0&&xx<MapW-1&&yy<MapH-1)tiles[yy*MapW+xx]=tiles[yy*MapW+xx]==4?5:1;
        for(int xx=std::min(x0,x1)-1;xx<=std::max(x0,x1)+1;xx++)for(int yy=y1-1;yy<=y1+1;yy++)if(xx>0&&yy>0&&xx<MapW-1&&yy<MapH-1)tiles[yy*MapW+xx]=tiles[yy*MapW+xx]==4?5:1;
    };
    int centerX=int(sites[0].at.x/Tile),centerY=int(sites[0].at.y/Tile);
    for(auto& site:sites){int sx=int(site.at.x/Tile),sy=int(site.at.y/Tile);road(sx,sy,centerX,centerY);}
    buildings.reserve(128);
    for(int z=0;z<8;z++){
        int sx=int(sites[z].at.x/Tile),sy=int(sites[z].at.y/Tile);
        for(int row:{-2,-1,1,2})for(int col:{-2,-1,1,2}){
            Building house;house.x=sx+col*19-6;house.y=sy+row*15-4;house.zone=z;
            house.door={(house.x+6.5f)*Tile,(house.y+house.h+1.5f)*Tile};house.bed={(house.x+6.5f)*Tile,(house.y+3.5f)*Tile};
            house.navX=house.x-58;house.navY=house.y-58;
            for(int yy=house.y-1;yy<house.y+house.h+2;yy++)for(int xx=house.x-1;xx<house.x+house.w+1;xx++)if(xx>0&&yy>0&&xx<MapW-1&&yy<MapH-1)tiles[yy*MapW+xx]=0;
            for(int yy=house.y;yy<house.y+house.h;yy++)for(int xx=house.x;xx<house.x+house.w;xx++)tiles[yy*MapW+xx]=(xx==house.x||xx==house.x+house.w-1||yy==house.y||yy==house.y+house.h-1)?3:7;
            tiles[(house.y+house.h-1)*MapW+house.x+6]=1;tiles[(house.y+house.h-1)*MapW+house.x+7]=1;
            road(sx,sy,sx,house.y+house.h+1);road(sx,house.y+house.h+1,house.x+6,house.y+house.h+1);
            buildings.push_back(std::move(house));
        }
        for(int dy=-3;dy<=3;dy++)for(int dx=-3;dx<=3;dx++)tiles[(sy+dy)*MapW+sx+dx]=1;
    }
    std::vector<int> queue(MapW*MapH);
    for(size_t site=0;site<sites.size();site++){
        auto& field=flow[site];field.assign(MapW*MapH,65535);int start=int(sites[site].at.y/Tile)*MapW+int(sites[site].at.x/Tile);size_t lo=0,hi=0;field[start]=0;queue[hi++]=start;
        while(lo<hi){int at=queue[lo++];int xx=at%MapW,yy=at/MapW;for(int delta:{-1,1,-MapW,MapW}){int to=at+delta;if(to<0||to>=MapW*MapH||std::abs(to%MapW-xx)+std::abs(to/MapW-yy)!=1||!walk(tiles[to])||field[to]!=65535)continue;field[to]=uint16_t(field[at]+1);queue[hi++]=to;}}
    }
    // Local home fields route through actual doorways into individual residences.
    for(auto& house:buildings){
        house.flow.assign(128*128,65535);int tx=int(house.bed.x/Tile)-house.navX,ty=int(house.bed.y/Tile)-house.navY,start=ty*128+tx;size_t lo=0,hi=0;house.flow[start]=0;queue[hi++]=start;
        while(lo<hi){int at=queue[lo++],xx=at%128,yy=at/128;for(int delta:{-1,1,-128,128}){int to=at+delta;if(to<0||to>=128*128||std::abs(to%128-xx)+std::abs(to/128-yy)!=1||house.flow[to]!=65535)continue;int gx=house.navX+to%128,gy=house.navY+to/128;if(!walk(tile(gx,gy)))continue;house.flow[to]=uint16_t(house.flow[at]+1);queue[hi++]=to;}}
    }
}
bool Map::inside(Vec p,int home)const{if(home<0||home>=int(buildings.size()))return false;auto& b=buildings[home];return p.x>(b.x+.5f)*Tile&&p.x<(b.x+b.w-.5f)*Tile&&p.y>(b.y+.5f)*Tile&&p.y<(b.y+b.h-.5f)*Tile;}
Vec Map::homeDirection(Vec p,int home)const{
    if(home<0||home>=int(buildings.size()))return {};auto& b=buildings[home];int x=int(p.x/Tile)-b.navX,y=int(p.y/Tile)-b.navY;
    if(x<1||y<1||x>=127||y>=127||b.flow[y*128+x]==65535)return direction(p,b.zone);
    int chosen=y*128+x;uint16_t best=b.flow[chosen];
    for(int dy=-1;dy<=1;dy++)for(int dx=-1;dx<=1;dx++){
        int xx=x+dx,yy=y+dy;if(xx<0||yy<0||xx>=128||yy>=128)continue;int at=yy*128+xx;
        float wx=(xx+b.navX+.5f)*Tile,wy=(yy+b.navY+.5f)*Tile;
        if(b.flow[at]<best&&clear(wx,wy)&&(!dx||!dy||(walk(tile(xx+b.navX,y+b.navY))&&walk(tile(x+b.navX,yy+b.navY))))){chosen=at;best=b.flow[at];}
    }
    return normalized({(chosen%128+b.navX+.5f)*Tile-p.x,(chosen/128+b.navY+.5f)*Tile-p.y});
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
    const auto& cast=characters();
    for(size_t i=0;i<population;i++){
        auto& a=actors[i];uint32_t r=hash(seed,uint32_t(i));int k=int(i%40);
        a.role=k==0?Bandit:k==1?Wolf:k<6?Guard:k==6?Healer:k<10?Merchant:k<13?Hunter:k<18?Farmer:Villager;
        a.team=a.role==Bandit?1:a.role==Wolf?2:0;a.courage=.2f+random01(hash(r,1))*.8f;a.home=uint8_t(hash(r,3)%8);a.workSite=a.home;a.socialSite=2;
        a.coins=uint16_t(5+r%50);a.hunger=random01(hash(r,5))*.55f;a.thirst=random01(hash(r,6))*.5f;a.fatigue=random01(hash(r,7))*.4f;a.mind.phase=uint8_t(r%16);
        a.sanity=(i%67==31)?.24f:.8f+random01(hash(r,11))*.2f;a.drunk=(i%43==22)?.55f:0;
        if(i<cast.size()){
            const auto& d=cast[i];a.character=int16_t(i);a.role=d.role;a.team=0;a.home=d.home;a.workSite=d.work;a.socialSite=d.social;a.courage=d.courage;a.affinity=d.opinion;a.coins=d.coins;a.pocket=d.pocket;a.pocketCount=d.pocketCount;
        }
        a.residence=uint16_t(int(a.home)*16+int(hash(r,12)%16));a.homeAt=map.buildings[a.residence].bed;
        float bedShift=(int(hash(r,13)%3)-1)*1.4f;if(map.clear(a.homeAt.x+bedShift,a.homeAt.y))a.homeAt.x+=bedShift;
        Vec center=map.sites[a.workSite].at;
        if(a.role==Bandit||a.role==Wolf)center={Width*.75f,Height*.64f};
        float xx=0,yy=0;uint32_t attempt=0;
        do{uint32_t v=hash(r,attempt++);float spread=a.team?65.f:22.f;xx=clamp(center.x+(random01(v)*2-1)*spread,4,Width-4);yy=clamp(center.y+(random01(hash(v,9))*2-1)*spread,4,Height-4);}while(!map.clear(xx,yy));
        x[i]=xx;y[i]=yy;a.goal=center;if(a.role==Wolf)a.homeAt={xx,yy};
    }
    // A quiet, inhabited arrival instead of spawning an army around the player.
    player.at={map.sites[0].at.x+3,map.sites[0].at.y+5};
    for(size_t i=0;i<std::min<size_t>(6,population);i++){
        static constexpr int castOrder[]={8,16,1,0,5,3};int named=castOrder[i];
        if(named<int(population)){float xx=player.at.x+3+float(i%3)*2,yy=player.at.y+float(i/3)*3;if(map.clear(xx,yy)){x[named]=xx;y[named]=yy;}}
    }
    initSandbox();grid();
}
void World::grid(){
    offsets_.fill(0);for(size_t i=0;i<actors.size();i++)if(actors[i].hp>0)offsets_[cell(x[i],y[i])+1]++;
    for(size_t i=1;i<offsets_.size();i++)offsets_[i]+=offsets_[i-1];std::copy(offsets_.begin(),offsets_.end()-1,cursor_.begin());
    for(size_t i=0;i<actors.size();i++)if(actors[i].hp>0)order_[cursor_[cell(x[i],y[i])]++]=uint32_t(i);
}
void World::think(size_t begin,size_t end){
    for(size_t i=begin;i<end;i++){
        auto& a=actors[i];auto& m=local_[i];if(a.hp<=0)continue;
        float playerDistance2=(x[i]-player.at.x)*(x[i]-player.at.x)+(y[i]-player.at.y)*(y[i]-player.at.y);
        a.lod=uint8_t(!lodEnabled||fullAI||playerDistance2<80*80?0:playerDistance2<260*260?1:2);
        bool sleeping=a.life==Sleep&&std::abs(x[i]-a.homeAt.x)+std::abs(y[i]-a.homeAt.y)<2;
        uint32_t interval=fullAI?1:sleeping?120:a.lod==0?6:a.lod==1?30:240;
        if((tick+uint32_t(i))%interval!=0)continue;
        auto remember=[&](uint32_t id){if(!id||std::find(a.recentEvents.begin(),a.recentEvents.end(),id)!=a.recentEvents.end())return;a.recentEvents[a.eventHead++%4]=id;a.rumor=id;m.rumors++;};
        Observation o;o.health=a.hp/100;o.need=std::max(a.hunger,std::max(a.thirst,a.fatigue));o.critical=a.hp<34;
        int cx=int(x[i]/8),cy=int(y[i]/8);float enemyDist=24*24,allyDist=20*20,alarm=0;int social=0,probes=0,friendId=-1;
        // Rotate the starting bucket for fairness without changing replay or thread ordering.
        int shift=int(hash(seed,uint32_t(i),tick/6)%25);
        for(int search=0;search<25;search++){
            int s=(search+shift)%25,xx=cx+s%5-2,yy=cy+s/5-2;if(xx<0||yy<0||xx>=GridW||yy>=GridH)continue;
            int c=yy*GridW+xx;uint32_t lo=offsets_[c],hi=offsets_[c+1];if(lo==hi)continue;
            uint32_t offset=hash(seed,uint32_t(i),tick/6,uint32_t(c))%(hi-lo);
            for(uint32_t n=0;n<hi-lo;n++){
                uint32_t j=order_[lo+(n+offset)%(hi-lo)];if(j==i)continue;
                if(++probes>(a.lod==2?16:a.lod==1?64:192)){m.capped++;break;}
                float dx=x[j]-x[i],dy=y[j]-y[i],d=dx*dx+dy*dy;
                if(actors[j].team!=a.team&&d<enemyDist){o.enemy=int(j);enemyDist=d;}
                if(actors[j].team==a.team){if(actors[j].hp<60&&d<allyDist){o.ally=int(j);allyDist=d;}if(d<100){alarm+=fearBefore_[j];social++;remember(rumorBefore_[j]);if(d<16&&a.role!=Wolf&&actors[j].role!=Wolf)friendId=int(j);}}
            }
            if(probes>(a.lod==2?16:a.lod==1?64:192))break;
        }
        m.probes+=std::min(probes,a.lod==2?16:a.lod==1?64:192);
        float pdx=player.at.x-x[i],pdy=player.at.y-y[i],pd=pdx*pdx+pdy*pdy;
        if((a.team>0||(a.role==Guard&&wanted>0))&&player.hp>0&&pd<enemyDist){o.enemy=int(actors.size());enemyDist=pd;}
        Vec enemy{};
        if(o.enemy>=0){enemy=o.enemy==int(actors.size())?player.at:Vec{x[o.enemy],y[o.enemy]};o.distance=std::sqrt(enemyDist);o.visible=map.line({x[i],y[i]},enemy)&&(!player.sneaking||o.enemy!=int(actors.size())||o.distance<10);}
        if(o.visible){o.danger=clamp(1-o.distance/26);a.clue=enemy;a.lastSeen=tick;o.cue=2;}
        else if(tick-a.lastSeen<300&&a.lastSeen){o.danger=.24f;o.cue=1;o.clue=a.clue;}
        for(auto& event:events)if(event.id){if(event.until>tick){float dx=x[i]-event.at.x,dy=y[i]-event.at.y,d=dx*dx+dy*dy;float radius=event.strength*28;if(d<radius*radius){float threat=event.strength*(1-std::sqrt(d)/radius)*.55f;if(threat>o.danger){o.danger=threat;o.cue=1;a.clue=event.at;a.lastSeen=tick;}remember(event.id);}}if(event.crime&&a.role==Guard&&std::find(a.recentEvents.begin(),a.recentEvents.end(),event.id)!=a.recentEvents.end())a.mind.eventId=event.id;}
        a.mind.fear=clamp(fearBefore_[i]*.94f+o.danger*.05f+(social?alarm/social*.008f:0));o.danger=std::max(o.danger,a.mind.fear*.7f);o.social=social?float(social)/12:0;
        if(!o.visible&&a.sanity<.3f&&(tick/600+uint32_t(i))%3!=0){o.cue=1;o.danger=std::max(o.danger,.28f+a.stress*.12f);uint32_t key=hash(seed,uint32_t(i),tick/900);a.clue={clamp(x[i]+(random01(key)-.5f)*18,4,Width-4),clamp(y[i]+(random01(hash(key,1))-.5f)*18,4,Height-4)};}
        if(o.ally>=0){o.aid=(1-std::sqrt(allyDist)/20)*(1-actors[o.ally].hp/100);if(o.aid>.18f&&!o.visible)o.cue=3;}
        if(o.visible&&a.team==0){
            // Friendly body gate for local attacks, not a global O(N) ray test.
            int c=cell(x[i],y[i]);for(uint32_t k=offsets_[c];k<offsets_[c+1]&&k<offsets_[c]+48;k++){auto j=order_[k];if(j==i||actors[j].team!=a.team)continue;Vec da{x[j]-x[i],y[j]-y[i]},db{enemy.x-x[i],enemy.y-y[i]};float len=db.x*db.x+db.y*db.y;float t=len>0?(da.x*db.x+da.y*db.y)/len:0;float ex=da.x-db.x*t,ey=da.y-db.y*t;if(t>0&&t<1&&ex*ex+ey*ey<.36f)o.blockedShot=true;}
        }
        int old=a.mind.action;auto decision=bank.decide(a.mind,o,Role(a.role),clamp(a.courage+a.drunk*.1f-a.stress*.2f),a.affinity,seed,uint32_t(i),tick,mode);
        m.decisions++;m.ambiguous+=decision.ambiguous;m.safetyBlocks+=decision.blocked;m.held+=decision.held;m.switches+=old!=decision.action;
        if(a.life==Socialize&&friendId>=0&&tick>=a.nextSocial){a.partner=friendId;a.nextSocial=tick+300;a.loneliness=std::max(0.f,a.loneliness-.08f);auto& bond=a.bonds[hash(uint32_t(friendId))%4];if(bond.peer!=friendId)bond={friendId,0,0};bond.trust=int16_t(std::min(100,int(bond.trust)+1));bond.meetings=uint16_t(std::min(65000,int(bond.meetings)+1));m.socials++;}
        switch(decision.action){
        case Routine:planLife(i);break;
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
        bool sleeping=a.life==Sleep&&std::abs(x[i]-a.homeAt.x)+std::abs(y[i]-a.homeAt.y)<2;
        uint32_t cadence=fullAI||!lodEnabled?1:sleeping?60:a.lod==0?1:a.lod==1?4:30;
        if((tick+uint32_t(i))%cadence!=0)continue;
        float dt=Dt*cadence;
        a.hunger=clamp(a.hunger+.0013f*dt);a.thirst=clamp(a.thirst+.0018f*dt);a.fatigue=clamp(a.fatigue+.00065f*dt);a.loneliness=clamp(a.loneliness+.0007f*dt);a.drunk=std::max(0.f,a.drunk-.001f*dt);
        a.stress=clamp(a.stress+(a.mind.fear*.004f-.002f)*dt);a.sanity=clamp(a.sanity+(.0002f-a.stress*.00025f)*dt,.1f,1);a.grief=std::max(0.f,a.grief-.0004f*dt);
        Vec delta{a.goal.x-x[i],a.goal.y-y[i]};float dist=length(delta);Vec direction{};int site=-1;
        for(int k=0;k<8;k++)if(std::abs(a.goal.x-map.sites[k].at.x)+std::abs(a.goal.y-map.sites[k].at.y)<12){site=k;break;}
        bool homebound=a.role!=Wolf&&a.mind.action==Routine&&std::abs(a.goal.x-a.homeAt.x)+std::abs(a.goal.y-a.homeAt.y)<.1f;
        if(dist>.7f){direction=homebound&&dist>4?map.homeDirection({x[i],y[i]},a.residence):site>=0&&dist>12?map.direction({x[i],y[i]},site):normalized(delta);}
        if(!homebound&&map.inside({x[i],y[i]},a.residence))direction=map.direction({x[i],y[i]},a.workSite);
        if(dialogue.open&&dialogue.actor==int(i)&&a.mind.fear<.7f)direction={};
        if(a.life==Sleep&&homebound&&dist<1.5f){direction={};vx[i]=vy[i]=0;}
        if(a.drunk>.1f&&dist>2){float sway=std::sin(tick*.045f+float(i))*.15f*a.drunk;float dx=direction.x;direction.x-=direction.y*sway;direction.y+=dx*sway;}
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
        Vec target{direction.x*speed+clamp(avoid.x,-2,2)*av,direction.y*speed+clamp(avoid.y,-2,2)*av};float acceleration=(2+(motor.response+motor.steer*a.mind.correction)/24.f)*dt;
        if((a.mind.action==Routine&&dist<1.2f)||(dialogue.open&&dialogue.actor==int(i)&&a.mind.fear<.7f)){target={};vx[i]=vy[i]=0;}
        vx[i]+=(target.x-vx[i])*std::min(1.f,acceleration);vy[i]+=(target.y-vy[i])*std::min(1.f,acceleration);
        float nx=x[i]+vx[i]*dt,ny=y[i]+vy[i]*dt;
        int substeps=std::max(1,int(std::ceil(std::max(std::abs(nx-x[i]),std::abs(ny-y[i]))/.4f)));
        float sx=x[i],sy=y[i];
        for(int part=0;part<substeps;part++){
            float tx=sx+vx[i]*dt/substeps,ty=sy+vy[i]*dt/substeps;
            if(map.clear(tx,sy))sx=tx;else{vx[i]=0;local_[i].collisions++;}
            if(map.clear(sx,ty))sy=ty;else{vy[i]=0;local_[i].collisions++;}
        }
        nextX_[i]=sx;nextY_[i]=sy;
        if(a.mind.action==Routine&&dist<3){
            switch(a.life){
            case Eat:if(a.food||a.coins){a.hunger=std::max(0.f,a.hunger-.12f*dt);if(tick%180<uint32_t(cadence)){if(a.food)a.food--;else a.coins--;}}break;
            case Drink:a.thirst=std::max(0.f,a.thirst-.16f*dt);break;
            case Sleep:a.fatigue=std::max(0.f,a.fatigue-.06f*dt);a.hp=std::min(100.f,a.hp+.3f*dt);a.stress=std::max(0.f,a.stress-.01f*dt);break;
            case Recover:a.hp=std::min(100.f,a.hp+.7f*dt);break;
            case Socialize:if(a.partner>=0&&a.nextSocial>tick){a.loneliness=std::max(0.f,a.loneliness-.025f*dt);a.stress=std::max(0.f,a.stress-.01f*dt);if(a.socialSite==2&&a.coins>6&&(tick+uint32_t(i))%1800<cadence){a.coins--;a.drunk=clamp(a.drunk+.2f);}}break;
            case Work:case Forage:if((tick+uint32_t(i))%600<uint32_t(cadence)&&a.coins<1000){a.coins++;local_[i].arrivals++;}break;
            default:break;
            }
            vx[i]*=.6f;vy[i]*=.6f;
        }
        if(a.hunger>.98f||a.thirst>.98f)a.hp=std::max(0.f,a.hp-.5f*dt);
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
        auto& intent=intents_[i];if(intent.target>=0){if(intent.target==int(actors.size())){player.hp=std::max(0.f,player.hp-intent.damage);metrics.hits+=intent.damage>0;if(intent.damage>0)bleed(player.at,intent.damage,hash(seed,tick,uint32_t(i)));}
            else if(intent.target<int(actors.size())){auto& to=actors[intent.target];if(to.hp>0){to.hp=clamp(to.hp-intent.damage+intent.heal,0,100);metrics.hits+=intent.damage>0;metrics.heals+=intent.heal>0;if(intent.damage>0){bleed({x[intent.target],y[intent.target]},intent.damage,hash(seed,tick,uint32_t(i)));to.stress=clamp(to.stress+.02f);}if(to.hp<=0){int c=cell(x[intent.target],y[intent.target]);for(uint32_t k=offsets_[c];k<offsets_[c+1]&&k<offsets_[c]+64;k++){auto j=order_[k];if(actors[j].hp<=0||actors[j].team!=to.team)continue;bool friendLost=false;for(auto& bond:actors[j].bonds)if(bond.peer==intent.target&&bond.trust>0)friendLost=true;actors[j].stress=clamp(actors[j].stress+.08f);if(friendLost){actors[j].grief=clamp(actors[j].grief+.6f);actors[j].sanity=clamp(actors[j].sanity-.06f,.1f,1);}}}}}}
        auto& m=local_[i];metrics.decisions+=m.decisions;metrics.ambiguous+=m.ambiguous;metrics.safetyBlocks+=m.safetyBlocks;metrics.held+=m.held;metrics.switches+=m.switches;metrics.probes+=m.probes;metrics.capped+=m.capped;metrics.collisions+=m.collisions;metrics.rumors+=m.rumors;metrics.arrivals+=m.arrivals;
        metrics.socials+=m.socials;if(actors[i].hp>0){if(actors[i].lod==0)metrics.nearActors++;else if(actors[i].lod==1)metrics.midActors++;else metrics.farActors++;if(actors[i].life==Sleep&&std::abs(x[i]-actors[i].homeAt.x)+std::abs(y[i]-actors[i].homeAt.y)<2)metrics.sleepers++;}
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
    updateEffects();if(tick%60==0)updateSandbox(1.f/30);wanted=std::max(0.f,wanted-.002f*Dt);tick++;metrics.commitMs=ms(start);metrics.totalMs=ms(total);
}
void World::signal(Vec at,float strength,bool crime){auto& e=events[nextEvent%events.size()];e={at,nextEvent++,tick+360,clamp(strength,.1f,3),crime,false};}
int World::nearestActor(Vec p,float distance)const{int best=-1;float d=distance*distance;for(size_t i=0;i<actors.size();i++)if(actors[i].hp>0){float dx=x[i]-p.x,dy=y[i]-p.y,q=dx*dx+dy*dy;if(q<d){d=q;best=int(i);}}return best;}
bool World::valid()const{if(!std::isfinite(player.hp)||!map.clear(player.at.x,player.at.y))return false;for(size_t i=0;i<x.size();i++)if(!std::isfinite(x[i])||!std::isfinite(y[i])||!std::isfinite(vx[i])||!std::isfinite(vy[i])||!std::isfinite(actors[i].hp)||!map.clear(x[i],y[i]))return false;return true;}
uint64_t World::digest()const{
    uint64_t h=1469598103934665603ull;auto add=[&](uint32_t u){for(int k=0;k<4;k++)h=(h^uint8_t(u>>(k*8)))*1099511628211ull;};auto number=[&](float v){uint32_t u;std::memcpy(&u,&v,4);add(u);};
    add(tick);add(seed);add(uint32_t(mode));add(fullAI);add(lodEnabled);add(uint32_t(timeOffset));number(wanted);add(nextEvent);
    for(size_t i=0;i<x.size();i++){
        number(x[i]);number(y[i]);number(vx[i]);number(vy[i]);auto& a=actors[i];
        number(a.hp);number(a.hunger);number(a.thirst);number(a.fatigue);number(a.courage);number(a.affinity);number(a.goal.x);number(a.goal.y);number(a.clue.x);number(a.clue.y);
        add(a.lastSeen);add(a.rumor);for(auto e:a.recentEvents)add(e);add(a.eventHead);add(a.coins);add(a.food);add(a.role);add(a.team);add(a.home);add(a.task);
        add(a.residence);add(uint32_t(a.character));add(a.workSite);add(a.socialSite);add(a.life);add(a.lod);add(a.pocketCount);for(auto item:a.pocket)add(item);number(a.loneliness);number(a.stress);number(a.sanity);number(a.grief);number(a.drunk);add(a.activityUntil);add(a.nextSocial);add(uint32_t(a.grudge));add(uint32_t(a.partner));for(auto& bond:a.bonds){add(uint32_t(bond.peer));add(uint32_t(bond.trust));add(bond.meetings);}number(a.homeAt.x);number(a.homeAt.y);
        auto& m=a.mind;for(auto f:m.inhibition)number(f);number(m.fear);number(m.confidence);number(m.intensity);number(m.correction);add(m.holdUntil);add(m.eventId);add(m.branch);add(m.action);add(m.cue);add(m.phase);add(m.flags);for(auto b:m.previous)add(b);add(m.focus);
    }
    for(auto& e:events){number(e.at.x);number(e.at.y);add(e.id);add(e.until);number(e.strength);add(e.crime);add(e.reported);}
    for(auto& r:resources){number(r.at.x);number(r.at.y);add(r.renewAt);add(r.kind);add(r.stock);}
    for(auto& p:projects){number(p.at.x);number(p.at.y);number(p.growth);add(p.kind);add(p.active);add(p.watered);}for(auto n:stockpile)add(uint32_t(n));
    number(player.hp);number(player.at.x);number(player.at.y);number(player.stamina);number(player.hunger);number(player.thirst);number(player.warmth);number(player.cooldown);add(player.food);add(player.water);add(player.gold);add(player.herbs);add(player.quest);add(player.sneaking);for(auto n:player.bag)add(uint32_t(n));for(auto n:player.quests)add(n);for(auto n:player.reputation)add(uint32_t(n));add(player.renown);return h;
}
size_t World::bytes()const{size_t navigation=map.tiles.capacity()+map.buildings.capacity()*sizeof(Building);for(auto& f:map.flow)navigation+=f.capacity()*sizeof(uint16_t);for(auto& h:map.buildings)navigation+=h.flow.capacity()*sizeof(uint16_t);return navigation+sizeof(*this)+actors.capacity()*sizeof(Actor)+(x.capacity()+y.capacity()+vx.capacity()+vy.capacity()+nextX_.capacity()+nextY_.capacity()+fearBefore_.capacity())*sizeof(float)+(order_.capacity()+cursor_.capacity()+rumorBefore_.capacity())*sizeof(uint32_t)+intents_.capacity()*sizeof(Intent)+local_.capacity()*sizeof(Metrics);}
}
