// Copyright (c) 2026 Алексей Савельев (Alex Merqury / AlexMerq).
// SPDX-License-Identifier: LicenseRef-OUT-Source-Review-1.0
#include "fiber.hpp"
#include <algorithm>
#include <cmath>
#include <limits>

namespace fiber {
float clamp(float x,float a,float b){return std::isfinite(x)?std::max(a,std::min(b,x)):a;}
uint32_t hash(uint32_t a,uint32_t b,uint32_t c,uint32_t d){
    uint32_t h=a^0x9e3779b9u;for(auto x:{b,c,d}){h^=x+0x9e3779b9u+(h<<6)+(h>>2);h^=h>>16;h*=0x7feb352du;}return h^(h>>15);
}
float random01(uint32_t h){return float(h>>8)*(1.f/16777216.f);}
const char* actionName(int a){static const char* n[]={"ROUTINE","INVESTIGATE","ENGAGE","AID","FLEE","REPORT"};return n[std::max(0,std::min(5,a))];}
const char* roleName(int a){static const char* n[]={"Citizen","Watch","Trader","Healer","Bandit","Wolf"};return n[std::max(0,std::min(5,a))];}
const char* modeName(Mode m){switch(m){case Mode::Strict:return "STRICT";case Mode::Overlap:return "OVERLAP+MEMORY";case Mode::Exploratory:return "EXPLORATORY";default:return "UTILITY";}}
static int bin(float v){return std::min(3,int(clamp(v)*4));}
int Bank::address3(float a,float b,float c){return bin(a)+4*bin(b)+16*bin(c);}
int Bank::address4(int a,int b,int c,int d){return (a&3)+4*(b&3)+16*(c&3)+64*(d&3);}
int Bank::address5(int a,int b,int c,int d,int e){return address4(a,b,c,d)+256*(e&3);}
Action Bank::teacher(float danger,float need,float health,uint8_t cue){
    if(danger>.32f && health<.34f)return Flee;
    if(cue==2 && danger>.22f)return danger>.86f&&health<.6f?Flee:Engage;
    if(cue==3 && danger<.48f && need<.75f)return Aid;
    if(danger>.46f)return Report;
    if(cue==1 && danger>.1f)return Investigate;
    return Routine;
}
static std::array<float,2> targets(float danger,float need,Action a){
    float u=a==Flee?.97f:a==Engage?.83f:a==Aid?.64f:a==Investigate?.58f:.28f+.38f*need;
    return {clamp(u+danger*.04f),clamp(.25f+danger*.5f)};
}
Bank::Bank(){
    // A new procedural benchmark teacher, never an OUT Guard/Motor shipped dataset.
    // Training scratch is temporary; inference only reads quantized branch arrays.
    std::array<std::array<float,8>,64*Branches> weights{};
    std::array<std::array<float,16>,64*Branches> gates{};
    auto learn=[&](float d,float n,float h,uint8_t cue){
        auto a=teacher(d,n,h,cue);int cell=address3(d,n,h),slot=-1;
        for(int k=0;k<Branches;k++){int j=cell*Branches+k;if(branches_[j].support&&branches_[j].action==a){slot=j;break;}}
        if(slot<0)for(int k=0;k<Branches;k++){int j=cell*Branches+k;if(!branches_[j].support){slot=j;branches_[j].action=a;break;}}
        if(slot<0){rejectedSamples_++;return;}
        auto& b=branches_[slot];auto count=b.support;b.support=uint16_t(std::min(65535,int(count)+1));b.cues[cue]=uint16_t(std::min(65535,int(b.cues[cue])+1));
        float f[3]={d,n,h};uint16_t proto=0;
        for(int j=0;j<3;j++){float old=float((b.prototype>>(j*4))&15);float v=count?old+(f[j]*15-old)/float(std::min(64,int(count)+1)):f[j]*15;proto|=uint16_t(std::lround(clamp(v,0,15)))<<(j*4);}b.prototype=proto;
        float feature[4]={1,d*4-bin(d)-.5f,n*4-bin(n)-.5f,h*4-bin(h)-.5f};auto target=targets(d,n,a);
        float lr=.16f/std::sqrt(1+count*.01f);
        for(int out=0;out<2;out++){float p=0;for(int j=0;j<4;j++)p+=weights[slot][out*4+j]*feature[j];for(int j=0;j<4;j++)weights[slot][out*4+j]=clamp(weights[slot][out*4+j]+lr*(target[out]-p)*feature[j],-1,1);}
        int row=cue*4;
        for(int k=0;k<Branches;k++){int other=cell*Branches+k;if(other==slot||!branches_[other].support)continue;float delta=0;for(int j=0;j<4;j++)delta+=(gates[slot][row+j]-gates[other][row+j])*feature[j];if(delta>.46f)continue;
            for(int j=0;j<4;j++){gates[slot][row+j]=clamp(gates[slot][row+j]+lr*feature[j],-2.5f,2.5f);gates[other][row+j]=clamp(gates[other][row+j]-lr*.72f*feature[j],-2.5f,2.5f);}}
    };
    for(uint32_t i=0;i<24000;i++){
        uint32_t v=hash(0xF1B3,i);float d=random01(v),n=random01(hash(v,1)),h=random01(hash(v,2));
        if(i<2048){int cell=int(i/32);d=(float(cell%4)+random01(v))/4;n=(float((cell/4)%4)+random01(hash(v,1)))/4;h=(float(cell/16)+random01(hash(v,2)))/4;}
        learn(d,n,h,uint8_t(i%4));
    }
    for(size_t i=0;i<branches_.size();i++){for(int j=0;j<8;j++)branches_[i].residual[j]=int8_t(std::lround(weights[i][j]*127));for(int j=0;j<16;j++)branches_[i].gate[j]=int8_t(std::lround(gates[i][j]/2.5f*127));}
    for(int a=0;a<4;a++)for(int c=0;c<4;c++)for(int f=0;f<4;f++)for(int q=0;q<4;q++){
        auto& v=motion_[address4(a,c,f,q)];v.speed=int8_t(110-f*15-c*5);v.steer=int8_t(50+q*20);v.avoidance=int8_t(25+c*27);v.response=int8_t(45+q*20);
    }
    for(int n=0;n<4;n++)for(int fear=0;fear<4;fear++)for(int courage=0;courage<4;courage++)for(int relation=0;relation<4;relation++)for(int phase=0;phase<4;phase++){
        int index=address5(n,fear,courage,relation,phase);
        for(int a=0;a<Actions;a++){
            int value=7;
            if(a==Routine)value+=n*2+(phase==0?2:0);
            if(a==Aid)value+=relation*2-n;
            if(a==Flee)value+=fear*2-courage;
            if(a==Engage)value+=courage*2-fear;
            if(a==Report)value+=fear+(phase==2?1:0);
            if(a==Investigate)value+=courage-n;
            int off=index*3+a/2;context_[off]|=uint8_t(std::max(0,std::min(15,value)))<<((a%2)*4);
        }
    }
}
float Bank::context(int action,float n,float f,float c,float a,float phase)const{
    int i=address5(bin(n),bin(f),bin(c),bin((a+1)*.5f),bin(phase));return float((context_[i*3+action/2]>>((action%2)*4))&15)/15;
}
MotionCell Bank::motion(Action a,float crowd,float fatigue,float confidence)const{return motion_[address4(std::min(3,int(a)),bin(crowd),bin(fatigue),bin(confidence))];}
bool Bank::allowed(Action a,const Observation& o,Role role){
    if(a==Engage)return role!=Villager&&role!=Merchant&&o.visible&&o.enemy>=0&&!o.blockedShot&&!o.critical;
    if(a==Aid)return o.ally>=0&&o.aid>.05f&&o.danger<.72f&&!o.critical;
    if((o.critical&&o.danger>.2f)||o.danger>.92f)return a==Flee||a==Report;
    if(a==Routine&&o.visible&&o.distance<7)return false;
    return true;
}
Decision Bank::decide(Mind& mind,const Observation& o,Role role,float courage,float affinity,uint32_t seed,uint32_t id,uint32_t tick,Mode mode)const{
    float f[3]={clamp(o.danger),clamp(o.need),clamp(o.health)};int q[3]={bin(f[0]),bin(f[1]),bin(f[2])};int cells[4]={address3(f[0],f[1],f[2]),0,0,0};int count=1;
    if(mode!=Mode::Strict)for(int d=0;d<3;d++){float frac=f[d]*4-q[d];int other=frac<.28f&&q[d]>0?q[d]-1:frac>.72f&&q[d]<3?q[d]+1:-1;if(other>=0){int qq[3]={q[0],q[1],q[2]};qq[d]=other;cells[count++]=qq[0]+4*qq[1]+16*qq[2];}}
    float scores[Actions],intensity[Actions],correction[Actions];uint16_t branch[Actions];for(int a=0;a<Actions;a++){scores[a]=-1e9f;intensity[a]=correction[a]=.5f;branch[a]=65535;}
    // Bounded attentional competition; no transformer or per-agent model copy.
    float channels[4]={o.danger,o.aid,o.need,o.cue!=mind.cue?.65f:.05f};int focus=0;
    for(int k=1;k<4;k++)if(channels[k]-mind.inhibition[k]*.27f>channels[focus]-mind.inhibition[focus]*.27f)focus=k;
    if(channels[mind.focus]>=channels[focus]-.13f)focus=mind.focus;
    mind.focus=uint8_t(focus);
    uint8_t cue=uint8_t(std::min(3,int(o.cue)));bool blocked=false;
    for(int c=0;c<count;c++)for(int k=0;k<Branches;k++){
        int j=cells[c]*Branches+k;auto& b=branches_[j];if(!b.support)continue;int a=b.action;
        if(!allowed(Action(a),o,role)){blocked=true;continue;}
        float distance=0;for(int d=0;d<3;d++){float delta=float((b.prototype>>(d*4))&15)/15-f[d];distance+=delta*delta;}
        float res[4]={1,f[0]*4-q[0]-.5f,f[1]*4-q[1]-.5f,f[2]*4-q[2]-.5f};float gate=0,pred=0,control=0;
        for(int d=0;d<4;d++){gate+=b.gate[cue*4+d]/127.f*res[d];pred+=b.residual[d]/127.f*res[d];control+=b.residual[4+d]/127.f*res[d];}
        float score=.32f+.15f*std::log2(1.f+b.support)-1.6f*distance+.10f*std::log((b.cues[cue]+1.f)/(b.support+4.f))+gate*8-(c?.16f:0);
        score+=context(a,o.need,mind.fear,courage,affinity,float(mind.phase)/16)*.2f;
        int purpose=a==Aid?1:a==Routine?2:a==Investigate?3:0;
        score+=purpose==focus?.08f:-.03f;
        score-=mind.inhibition[a%4]*.055f;
        if(a==mind.action)score+=.19f;
        if(score>scores[a]){scores[a]=score;intensity[a]=clamp(pred);correction[a]=clamp(control);branch[a]=uint16_t(j);}
    }
    if(mode==Mode::Utility){Action a=teacher(f[0],f[1],f[2],cue);if(!allowed(a,o,role))a=o.danger>.2f?Flee:Routine;for(int k=0;k<Actions;k++)scores[k]=k==a?1.f:-1e9f;}
    int best=0,second=-1;for(int a=1;a<Actions;a++)if(scores[a]>scores[best])best=a;
    if(scores[best]<-1e8f){best=o.danger>.2f?Flee:Routine;scores[best]=0;}
    for(int a=0;a<Actions;a++)if(a!=best&&(second<0||scores[a]>scores[second]))second=a;
    float margin=second<0?1:scores[best]-scores[second];bool urgent=o.critical||o.danger>.7f||(o.visible&&o.distance<8);
    bool stable=true;for(int d=0;d<3;d++)if(std::abs(int(mind.previous[d])-int(f[d]*255))>9)stable=false;
    bool held=false,ambiguous=false;
    if(!urgent&&allowed(Action(mind.action),o,role)&&mind.cue==cue&&(stable||tick<mind.holdUntil)&&scores[mind.action]>=scores[best]-.5f){best=mind.action;held=true;}
    else if(mode==Mode::Exploratory&&!urgent&&margin<.19f){
        float maximum=-1e9f;for(int a=0;a<Actions;a++)if(scores[a]>=scores[best]-.19f&&allowed(Action(a),o,role)){
            float u=std::max(.000001f,random01(hash(seed,id,tick/84,uint32_t(a))));float g=scores[a]/.09f-std::log(-std::log(u));if(g>maximum){maximum=g;second=a;}ambiguous=true;}best=second;
    }
    if(best!=mind.action)mind.holdUntil=tick+90+hash(seed,id)%19;
    for(int d=0;d<3;d++)mind.previous[d]=uint8_t(f[d]*255);
    for(auto& x:mind.inhibition)x*=.91f;
    mind.inhibition[best%4]=clamp(mind.inhibition[best%4]+.1f);
    mind.action=uint8_t(best);mind.cue=cue;mind.branch=branch[best];mind.confidence=clamp(margin/.6f);mind.intensity=intensity[best];mind.correction=correction[best];
    return {Action(best),intensity[best],margin,branch[best],uint8_t(count*Branches),ambiguous,blocked,held};
}
uint64_t Bank::checksum()const{
    uint64_t h=1469598103934665603ull;
    auto add=[&](uint8_t b){h=(h^b)*1099511628211ull;};
    // Hash members explicitly; padding bytes are not deterministic data.
    for(auto& b:branches_){add(b.action);add(uint8_t(b.prototype));add(uint8_t(b.prototype>>8));add(uint8_t(b.support));add(uint8_t(b.support>>8));for(auto v:b.cues){add(uint8_t(v));add(uint8_t(v>>8));}for(auto v:b.gate)add(uint8_t(v));for(auto v:b.residual)add(uint8_t(v));}
    for(auto& m:motion_){add(uint8_t(m.speed));add(uint8_t(m.steer));add(uint8_t(m.avoidance));add(uint8_t(m.response));}for(auto v:context_)add(v);return h;
}
Workers::Workers(unsigned n){
    n=std::max(1u,std::min(32u,n));
    for(unsigned id=1;id<n;id++)threads_.emplace_back([this,id,n]{size_t seen=0;for(;;){std::unique_lock<std::mutex> lock(mutex_);wake_.wait(lock,[&]{return stop_||epoch_!=seen;});if(stop_)return;seen=epoch_;auto start=count_*id/n,end=count_*(id+1)/n;lock.unlock();job_(start,end);lock.lock();if(--remaining_==0)done_.notify_one();}});
}
Workers::~Workers(){{std::lock_guard<std::mutex> lock(mutex_);stop_=true;}wake_.notify_all();for(auto& t:threads_)t.join();}
void Workers::run(size_t n,const std::function<void(size_t,size_t)>& fn){
    if(threads_.empty()){fn(0,n);return;}
    {std::lock_guard<std::mutex> lock(mutex_);count_=n;job_=fn;remaining_=threads_.size();++epoch_;}wake_.notify_all();fn(0,n/(threads_.size()+1));std::unique_lock<std::mutex> lock(mutex_);done_.wait(lock,[&]{return remaining_==0;});
}
}
