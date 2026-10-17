// SPDX-License-Identifier: LicenseRef-OUT-Source-Review-1.0
// Copyright (c) 2026 Алексей Савельев (Alex Merqury). See ../../LICENSE.
#define NOMINMAX
#include <windows.h>
#include <windowsx.h>
#include <gdiplus.h>
#include "fiber.hpp"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <memory>

namespace {
using namespace fiber;
std::unique_ptr<World> world;
HWND windowHandle=nullptr;HDC backDC=nullptr;HBITMAP bitmap=nullptr,oldBitmap=nullptr;
HFONT regular=nullptr,fontSmall=nullptr,title=nullptr,heading=nullptr;
uint32_t* surface=nullptr;bool mapClip=false;
int width=1440,height=940,selected=-1,panel=0;bool paused=false,atlas=false,debug=false,labels=true,bloodVisible=true;
float zoom=8.5f;Vec camera{},waypoint{};bool hasWaypoint=false;double renderMs=0,dropped=0;
std::string message="Вересковый предел. Колокол молчит, но у каждого здесь есть своя история.";
struct Button {RECT bounds;int action;};std::vector<Button> buttons;
COLORREF ink=RGB(232,227,209),muted=RGB(157,170,155),gold=RGB(218,183,124),lime=RGB(169,215,144);
int mapRight(){return width-330;}int mapBottom(){return height-96;}
float scale(){return atlas?std::min((mapRight()-34)/Width,(mapBottom()-113)/Height):zoom;}
int screenX(float x){return 17+(mapRight()-34)/2+int((x-camera.x)*scale());}
int screenY(float y){return 107+(mapBottom()-112)/2+int((y-camera.y)*scale());}
std::wstring wide(const std::string& s){if(s.empty())return {};int n=MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,s.data(),int(s.size()),nullptr,0);if(n<1)return L"[UTF-8]";std::wstring w(size_t(n),0);MultiByteToWideChar(CP_UTF8,0,s.data(),int(s.size()),w.data(),n);return w;}
std::string number(double value,int precision=1){char b[64];std::snprintf(b,sizeof(b),"%.*f",precision,value);return b;}
void box(int x,int y,int w,int h,COLORREF color){
    int l=std::max(mapClip?13:0,x),r=std::min(mapClip?mapRight()-13:width,x+w),t=std::max(mapClip?102:0,y),b=std::min(mapClip?mapBottom()-1:height,y+h);
    if(l>=r||t>=b||!surface)return;uint32_t pixel=((color&255)<<16)|(color&0xff00)|((color>>16)&255);
    for(int row=t;row<b;row++)std::fill(surface+row*width+l,surface+row*width+r,pixel);
}
void disk(int x,int y,int radius,COLORREF color){radius=std::min(120,std::max(1,radius));for(int yy=-radius;yy<=radius;yy++){int n=int(std::sqrt(float(radius*radius-yy*yy)));box(x-n,y+yy,2*n+1,1,color);}}
void text(int x,int y,const std::string& value,COLORREF color=RGB(232,227,209),HFONT font=nullptr){auto w=wide(value);SelectObject(backDC,font?font:regular);SetTextColor(backDC,color);SetBkMode(backDC,TRANSPARENT);TextOutW(backDC,x,y,w.data(),int(w.size()));}
void wrapped(int x,int y,int w,int h,const std::string& value,COLORREF color,HFONT font=nullptr){auto s=wide(value);SelectObject(backDC,font?font:regular);SetTextColor(backDC,color);SetBkMode(backDC,TRANSPARENT);RECT r{x,y,x+w,y+h};DrawTextW(backDC,s.data(),int(s.size()),&r,DT_WORDBREAK|DT_NOPREFIX|DT_END_ELLIPSIS);}
void button(int x,int y,int w,int h,const std::string& label,int action,COLORREF accent=RGB(218,183,124)){
    box(x,y,w,h,RGB(34,47,45));box(x,y,3,h,accent);text(x+12,y+(h-19)/2,label,ink,fontSmall);buttons.push_back({{x,y,x+w,y+h},action});
}
void meter(int x,int y,const char* label,float value,COLORREF color){text(x,y,label,muted,fontSmall);box(x+82,y+4,196,7,RGB(38,50,45));box(x+82,y+4,int(196*clamp(value)),7,color);}
void resize(int w,int h){
    width=std::max(1100,w);height=std::max(780,h);if(bitmap){SelectObject(backDC,oldBitmap);DeleteObject(bitmap);}if(!backDC){HDC dc=GetDC(windowHandle);backDC=CreateCompatibleDC(dc);ReleaseDC(windowHandle,dc);}
    BITMAPINFO info{};info.bmiHeader.biSize=sizeof(BITMAPINFOHEADER);info.bmiHeader.biWidth=width;info.bmiHeader.biHeight=-height;info.bmiHeader.biPlanes=1;info.bmiHeader.biBitCount=32;info.bmiHeader.biCompression=BI_RGB;void* p=nullptr;bitmap=CreateDIBSection(backDC,&info,DIB_RGB_COLORS,&p,nullptr,0);surface=static_cast<uint32_t*>(p);oldBitmap=(HBITMAP)SelectObject(backDC,bitmap);
}
COLORREF roleColor(const Actor& a){switch(a.role){case Guard:return RGB(109,167,197);case Merchant:return RGB(183,136,72);case Healer:return RGB(155,190,139);case Bandit:return RGB(146,64,58);case Wolf:return RGB(133,139,145);case Hunter:return RGB(85,123,91);case Innkeeper:return RGB(166,115,106);case Scholar:return RGB(140,122,164);default:return RGB(151,145,104);}}
COLORREF lit(int r,int g,int b,float light){return RGB(int(r*light),int(g*light),int(b*light));}
void sprite(Vec at,COLORREF clothes,int role,bool dead,bool sleeping,float velocity,uint32_t identity,bool player=false){
    int x=screenX(at.x),y=screenY(at.y);float s=scale();if(atlas||s<3){box(x-1,y-1,3,3,dead?RGB(96,60,54):clothes);return;}
    float size=std::max(1.f,s/5);int unit=std::max(1,int(size));int stride=velocity>.12f?int(std::sin(world->tick*.18f+identity)*unit):0;
    disk(x+2,y+2,std::max(2,int(s*.5f)),RGB(22,26,23));
    if(dead){box(x-6*unit,y-2*unit,11*unit,3*unit,clothes);box(x+5*unit,y-2*unit,3*unit,3*unit,RGB(161,134,108));return;}
    if(sleeping){box(x-5*unit,y-2*unit,10*unit,3*unit,clothes);box(x+4*unit,y-3*unit,3*unit,3*unit,RGB(197,160,125));text(x+5,y-27,"z",muted,fontSmall);return;}
    if(role==Wolf){box(x-5*unit,y-5*unit,10*unit,4*unit,clothes);box(x+4*unit,y-7*unit,4*unit,4*unit,clothes);box(x+6*unit,y-5*unit,unit,unit,RGB(234,197,113));box(x-4*unit,y,2*unit,3*unit+stride,RGB(63,65,69));box(x+3*unit,y,2*unit,3*unit-stride,RGB(63,65,69));return;}
    box(x-3*unit,y-2*unit,2*unit,5*unit+stride,RGB(65,62,57));box(x+unit,y-2*unit,2*unit,5*unit-stride,RGB(65,62,57));
    box(x-4*unit,y-10*unit,8*unit,8*unit,clothes);box(x-5*unit,y-8*unit,unit,5*unit+stride,clothes);box(x+4*unit,y-8*unit,unit,5*unit-stride,clothes);
    box(x-3*unit,y-15*unit,6*unit,5*unit,RGB(201,166,127));box(x-3*unit,y-16*unit,6*unit,2*unit,role==Guard?RGB(161,173,172):RGB(67,56,45));
    if(role==Merchant||role==Scholar||role==Hunter){box(x-4*unit,y-17*unit,8*unit,2*unit,clothes);box(x-2*unit,y-20*unit,4*unit,3*unit,clothes);}
    if(role==Guard||role==Bandit||player){box(x+5*unit,y-12*unit,unit,12*unit,RGB(192,195,178));box(x+3*unit,y-3*unit,5*unit,unit,RGB(151,116,65));}
}
void drawWorld(){
    auto& w=*world;float s=scale();float daylight=clamp(std::sin((w.hour()-6)/24*6.283185f)*.65f+.45f,.17f,1);
    HRGN region=CreateRectRgn(13,102,mapRight()-13,mapBottom()-1);SelectClipRgn(backDC,region);mapClip=true;
    box(13,102,mapRight()-26,mapBottom()-103,RGB(19,31,30));
    int left=std::max(0,int((camera.x-(mapRight()-34)/s/2)/Tile)-2),right=std::min(MapW-1,int((camera.x+(mapRight()-34)/s/2)/Tile)+2);
    int top=std::max(0,int((camera.y-(mapBottom()-112)/s/2)/Tile)-2),bottom=std::min(MapH-1,int((camera.y+(mapBottom()-112)/s/2)/Tile)+2);int size=std::max(1,int(Tile*s)+1);
    for(int yy=top;yy<=bottom;yy++)for(int xx=left;xx<=right;xx++){
        int x=screenX(xx*Tile),y=screenY(yy*Tile),t=w.map.tiles[yy*MapW+xx];uint32_t noise=hash(w.seed,xx,yy);float v=float(noise%13)/100;COLORREF c;
        float ambient=daylight;if(!atlas&&ambient<.7f){float dx=(xx+.5f)*Tile-w.player.at.x,dy=(yy+.5f)*Tile-w.player.at.y,d2=dx*dx+dy*dy;if(d2<18*18)daylight=std::max(ambient,.35f+.5f*(1-std::sqrt(d2)/18));}
        switch(t){case 1:c=lit(108,99,74,daylight+v);break;case 2:c=lit(43,85,57,daylight+v);break;case 3:c=lit(110,108,96,daylight+v);break;case 4:c=lit(42,83,101,daylight+v);break;case 5:c=lit(138,105,65,daylight+v);break;case 6:c=lit(76,91,60,daylight+v);break;case 7:c=lit(111,83,57,std::max(.55f,daylight));break;default:c=lit(66,84,52,daylight+v);break;}
        box(x,y,size,size,c);
        if(!atlas&&t==0&&noise%4==0)box(x+size/3,y+size/2,2,2,lit(100,118,68,daylight));
        if(!atlas&&t==1&&noise%3==0){box(x+2,y+size/2,size-4,1,lit(82,77,58,daylight));}
        if(!atlas&&t==4)box(x+2,y+size/2,int(size*.7f),1,lit(104,150,158,daylight));
        if(!atlas&&t==2){disk(x+size/2+3,y+size/2+4,size/2,RGB(23,37,29));box(x+size/2-1,y+size/2,3,size/2,lit(92,66,42,daylight));disk(x+size/2,y+size/3,size/2,lit(40,81,53,daylight+.1f));disk(x+size/3,y+size/5,size/3,lit(58,103,63,daylight+.1f));}
        if(!atlas&&t==6&&noise%3==0){box(x+size/2,y+size/3,2,5,lit(130,111,147,daylight));box(x+size/2+3,y+size/3+3,2,4,lit(150,119,151,daylight));}
        daylight=ambient;
    }
    std::array<int,128> sleepers{};for(size_t i=0;i<w.actors.size();i++){auto& a=w.actors[i];if(a.hp>0&&a.life==fiber::Sleep&&a.residence<128&&std::abs(w.x[i]-a.homeAt.x)+std::abs(w.y[i]-a.homeAt.y)<2)sleepers[a.residence]++;}
    for(size_t k=0;k<w.map.buildings.size();k++){
        auto& b=w.map.buildings[k];int x=screenX(b.x*Tile),y=screenY(b.y*Tile),bw=int(b.w*Tile*s),bh=int(b.h*Tile*s);if(x+bw<13||x>mapRight()-13||y+bh<102||y>mapBottom())continue;
        bool interior=w.map.inside(w.player.at,int(k))||debug;
        if(!atlas&&!interior){box(x+5,y+7,bw,bh,RGB(24,27,24));box(x-2,y-6,bw+4,bh+5,lit(107,64,48,daylight+.1f));box(x,y-4,bw,bh/2,lit(140,84,55,daylight+.1f));box(x,y+bh/2,bw,bh/2,lit(95,58,44,daylight+.1f));for(int yy=2;yy<bh;yy+=8)box(x,y+yy,bw,1,lit(68,46,38,daylight+.1f));box(x,y+bh/2,bw,2,lit(166,105,65,daylight+.1f));box(screenX(b.door.x)-int(s),y+bh-2,int(2*s),6,RGB(49,38,28));box(x+bw-18,y-7,10,16,lit(122,111,93,daylight));
            if(daylight<.6f){box(x+10,y+bh-8,8,5,RGB(225,165,77));box(x+bw-22,y+bh-8,8,5,RGB(225,165,77));}
            if(sleepers[k])text(x+5,y+6,"z · "+std::to_string(sleepers[k]),RGB(214,204,185),fontSmall);
        }else if(!atlas){int bx=screenX(b.bed.x),by=screenY(b.bed.y);box(bx-int(2*s),by-int(s),int(4*s),int(2*s),RGB(79,53,34));box(bx-int(1.8f*s),by-int(.8f*s),int(3.6f*s),int(1.6f*s),RGB(126,137,123));box(bx-int(1.8f*s),by-int(.8f*s),int(.8f*s),int(1.6f*s),RGB(207,192,152));box(bx+int(3*s),by+int(4*s),int(3*s),int(2*s),RGB(100,68,38));}
    }
    if(bloodVisible&&!atlas)for(auto& b:w.blood)if(b.life>0){int radius=std::max(1,int(b.radius*s));disk(screenX(b.at.x),screenY(b.at.y),radius,b.stain?RGB(91,35,29):RGB(153,42,35));}
    for(auto& r:w.resources)if(r.stock){int x=screenX(r.at.x),y=screenY(r.at.y);if(x<15||x>mapRight()-15||y<105||y>mapBottom()-5)continue;
        COLORREF c=r.kind==Wood?RGB(133,97,56):r.kind==Stone?RGB(147,152,146):r.kind==Fiber?RGB(142,163,89):RGB(206,174,100);
        if(atlas){if(debug)box(x,y,2,2,c);continue;}
        if(r.kind==Stone){disk(x,y,4,c);box(x-3,y+3,7,2,RGB(81,91,86));}else if(r.kind==Wood){box(x-5,y-2,10,4,c);box(x-3,y-5,9,3,c);}else{box(x,y-8,2,9,c);box(x-4,y-5,8,2,c);}
    }
    for(auto& p:w.projects)if(p.active){int x=screenX(p.at.x),y=screenY(p.at.y);if(x<15||x>mapRight()-15||y<105||y>mapBottom()-5)continue;int u=std::max(1,int(s/4));
        if(atlas){disk(x,y,3,lime);continue;}
        switch(p.kind){case Campfire:disk(x,y,5*u,RGB(97,91,77));box(x-4*u,y-2*u,8*u,3*u,RGB(121,77,40));disk(x,y-4*u,3*u,RGB(206,108,42));disk(x,y-6*u,2*u,RGB(238,183,78));break;
        case Bedroll:box(x-5*u,y-3*u,10*u,5*u,RGB(123,144,112));box(x-5*u,y-3*u,3*u,5*u,RGB(201,186,149));break;
        case Workbench:box(x-6*u,y-6*u,12*u,5*u,RGB(142,100,57));box(x-5*u,y-u,2*u,5*u,RGB(80,56,37));box(x+3*u,y-u,2*u,5*u,RGB(80,56,37));break;
        case Garden:box(x-6*u,y-5*u,12*u,10*u,p.watered?RGB(69,53,36):RGB(98,74,47));for(int k=-1;k<=1;k++){box(x+k*4*u,y-3*u,u,6*u,RGB(91,135,71));if(p.growth>=4)disk(x+k*4*u,y-2*u,u,RGB(172,69,73));}break;
        case Storage:box(x-5*u,y-5*u,10*u,7*u,RGB(151,101,52));box(x-5*u,y-2*u,10*u,u,RGB(65,60,48));box(x-u,y-3*u,2*u,3*u,gold);break;
        case Shelter:box(x-7*u,y-3*u,u,7*u,RGB(97,76,51));box(x+6*u,y-3*u,u,7*u,RGB(97,76,51));box(x-8*u,y-8*u,16*u,7*u,RGB(120,143,103));break;}
        if(labels)text(x-35,y+9,projectName(p.kind),gold,fontSmall);
    }
    std::array<RECT,24> labelBounds{};int labelCount=0,bubbles=0;
    for(size_t i=0;i<w.actors.size();i++){
        auto& a=w.actors[i];int x=screenX(w.x[i]),y=screenY(w.y[i]);if(x<7||x>mapRight()-7||y<90||y>mapBottom()+25)continue;
        bool indoors=w.map.inside({w.x[i],w.y[i]},a.residence);if(!atlas&&!debug&&indoors&&!w.map.inside(w.player.at,a.residence))continue;
        bool sleeping=a.life==fiber::Sleep&&std::abs(w.x[i]-a.homeAt.x)+std::abs(w.y[i]-a.homeAt.y)<2;
        COLORREF color=debug&&atlas?(a.lod==0?RGB(173,224,135):a.lod==1?RGB(214,177,92):RGB(103,129,151)):roleColor(a);
        sprite({w.x[i],w.y[i]},color,a.role,a.hp<=0,sleeping,std::abs(w.vx[i])+std::abs(w.vy[i]),uint32_t(i));
        if(!atlas&&a.hp>0&&labels&&labelCount<20&&(a.character>=0||int(i)==selected||s>10)){
            std::string label=w.actorName(int(i))+" · "+(a.mind.action==Routine?lifeName(a.life):actionName(a.mind.action));auto ws=wide(label);SIZE measured;SelectObject(backDC,fontSmall);GetTextExtentPoint32W(backDC,ws.data(),int(ws.size()),&measured);
            RECT bounds{x-measured.cx/2-4,y-int(s*4.3f)-17,x+measured.cx/2+4,y-int(s*4.3f)+1};bool overlap=bounds.left<15||bounds.right>mapRight()-15;RECT intersection;
            for(int k=0;k<labelCount;k++)if(IntersectRect(&intersection,&bounds,&labelBounds[k]))overlap=true;
            if(!overlap){labelBounds[labelCount++]=bounds;box(bounds.left,bounds.top,bounds.right-bounds.left,18,RGB(24,31,28));text(bounds.left+4,bounds.top+1,label,int(i)==selected?gold:ink,fontSmall);}
        }
        if(!atlas&&a.hp>0&&bubbles<2&&((a.partner>=0&&a.nextSocial>w.tick&&w.tick%300<100)||(a.sanity<.3f&&w.tick%600<110))){
            std::string line=a.sanity<.3f?"Слышишь колокол под землёй?":a.rumor?"Говорят, дозор ищет свидетелей...":"У каждого дома есть своя история.";
            int bx=std::max(20,std::min(mapRight()-290,x-130));box(bx,y-90,275,24,RGB(216,204,171));text(bx+8,y-87,line,RGB(46,44,33),fontSmall);bubbles++;
        }
        if(int(i)==selected&&!atlas){box(x-10,y+5,20,2,gold);if(a.hp<100){box(x-12,y-35,24,3,RGB(71,48,37));box(x-12,y-35,int(24*clamp(a.hp/100)),3,RGB(156,192,117));}}
    }
    for(int k=0;k<8;k++){auto& site=w.map.sites[k];int x=screenX(site.at.x),y=screenY(site.at.y);if(x<13||x>mapRight()-13||y<102||y>mapBottom())continue;
        disk(x,y,atlas?4:std::max(3,int(s*.5f)),gold);if(atlas)text(x+7,y-14,zoneName(k),gold,fontSmall);
        else if(k==0){box(x-int(s*.4f),y-int(s*4),int(s*.8f),int(s*4),RGB(100,79,48));box(x-int(s*1.2f),y-int(s*4.2f),int(s*2.4f),int(s*.6f),RGB(180,146,87));}
    }
    sprite(w.player.at,RGB(133,176,129),Guard,w.player.hp<=0,false,std::abs(w.input.x)+std::abs(w.input.y),0,true);
    if(!atlas){int px=screenX(w.player.at.x),py=screenY(w.player.at.y);box(px-3,py-int(s*4.8f),7,3,gold);text(px-20,py+9,w.player.hp>0?"Путник":"Погиб",gold,fontSmall);}
    if(hasWaypoint){int x=screenX(waypoint.x),y=screenY(waypoint.y);box(x-5,y-5,10,2,gold);box(x-5,y-5,2,10,gold);text(24,114,"Цель: "+number(std::hypot(waypoint.x-w.player.at.x,waypoint.y-w.player.at.y),0)+" м",gold,fontSmall);}
    SelectClipRgn(backDC,nullptr);DeleteObject(region);mapClip=false;
}
void sidebar(){
    auto& w=*world;int sx=width-312,y=114;auto row=[&](const std::string& label,const std::string& value){text(sx,y,label,muted,fontSmall);text(sx+147,y,value,ink,fontSmall);y+=22;};
    text(sx,y,debug?"ЖИВОЙ МИР / ОТЛАДКА":"ПУТНИК",gold,heading);y+=35;
    meter(sx,y,"Здоровье",w.player.hp/100,RGB(158,191,126));y+=24;meter(sx,y,"Силы",w.player.stamina/100,RGB(136,176,181));y+=24;
    meter(sx,y,"Сытость",1-w.player.hunger,RGB(207,168,104));y+=24;meter(sx,y,"Вода",1-w.player.thirst,RGB(123,169,198));y+=24;meter(sx,y,"Тепло",w.player.warmth,RGB(204,139,91));y+=28;
    row("Монеты / розыск",std::to_string(w.player.gold)+" / "+number(w.wanted,0));
    row("Хлеб / вода / вереск",std::to_string(w.player.food)+" / "+std::to_string(w.player.water)+" / "+std::to_string(w.player.herbs));
    button(sx,y,135,34,"I · Сумка",2001);button(sx+145,y,135,34,"J · Задания",2002);y+=47;
    if(selected>=0&&selected<int(w.actors.size())){
        auto& a=w.actors[selected];text(sx,y,w.actorName(selected),gold,heading);y+=29;row(roleName(a.role),a.mind.action==Routine?lifeName(a.life):actionName(a.mind.action));
        row("Здоровье / усталость",number(a.hp,0)+" / "+number(a.fatigue*100,0));row("Голод / жажда",number(a.hunger*100,0)+" / "+number(a.thirst*100,0));row("Стресс / рассудок",number(a.stress*100,0)+" / "+number(a.sanity*100,0));row("Отношение / слух",number(a.affinity*100,0)+" / "+std::to_string(a.rumor));
        row("Дом / занятие",std::to_string(a.residence+1)+" / "+std::to_string(a.life));button(sx,y,280,33,"T · Поговорить рядом",2003);y+=44;
    }else{int zone=w.map.nearest(w.player.at);text(sx,y,zoneName(zone),gold,heading);y+=30;wrapped(sx,y,280,110,zoneLore(zone),muted,fontSmall);y+=118;}
    if(debug){
        row("Жители / тики",std::to_string(w.actors.size())+" / "+std::to_string(w.tick));row("Близко / средне / далеко",std::to_string(w.metrics.nearActors)+" / "+std::to_string(w.metrics.midActors)+" / "+std::to_string(w.metrics.farActors));
        row("Спят / решили",std::to_string(w.metrics.sleepers)+" / "+std::to_string(w.metrics.decisions));row("CPU мир / рендер",number(w.metrics.totalMs,2)+" / "+number(renderMs,2));row("Лимит соседей",std::to_string(w.metrics.capped));row("Банк / память NPC",number(w.bank.bytes()/1024.,1)+"K / "+std::to_string(sizeof(Mind)));row("Отставание",number(dropped)+" с");
    }else{
        text(sx,y,"ПОСЛЕДНИЕ СОБЫТИЯ",gold);y+=27;
        for(int k=0;k<3&&uint32_t(k)<w.noticeCursor;k++){auto& n=w.notices[(w.noticeCursor-1-uint32_t(k))%w.notices.size()];wrapped(sx,y,280,49,n.text.data(),muted,fontSmall);y+=55;}
    }
    button(sx,height-149,135,32,"M · Карта",2004);button(sx+145,height-149,135,32,"F1 · Отладка",2005);
}
void overlay(){
    auto& w=*world;if(!panel&&!w.dialogue.open)return;int bw=std::min(width-100,980),bh=std::min(height-150,690),x=(width-bw)/2,y=(height-bh)/2;
    box(x-3,y-3,bw+6,bh+6,RGB(94,85,63));box(x,y,bw,bh,RGB(22,33,32));box(x,y,bw,4,gold);
    if(w.dialogue.open){
        text(x+28,y+23,w.dialogue.title,gold,heading);wrapped(x+28,y+69,bw-56,255,w.dialogue.text,ink);
        int top=y+350,buttonW=(bw-74)/2;
        for(size_t k=0;k<w.dialogue.choices.size();k++){auto& choice=w.dialogue.choices[k];int xx=x+28+int(k%2)*(buttonW+18),yy=top+int(k/2)*53;button(xx,yy,buttonW,43,std::to_string(k+1)+". "+choice.label,4000+choice.code);}
        text(x+28,y+bh-28,"Разговор не останавливает жизнь остальных. Esc — закончить.",muted,fontSmall);return;
    }
    if(panel==1){
        text(x+28,y+24,"СУМКА ПУТНИКА",gold,heading);text(x+bw-230,y+30,"Монеты: "+std::to_string(w.player.gold),gold);
        int shown=0;for(int k=0;k<ItemCount;k++){int count=w.itemCount(Item(k));if(!count)continue;int col=shown/10,yy=y+80+(shown%10)*39,xx=x+25+col*(bw/2),cw=bw/2-38;box(xx,yy,cw,34,RGB(31,44,39));text(xx+10,yy+5,itemName(k),ink,fontSmall);text(xx+cw-68,yy+5,"× "+std::to_string(count),gold,fontSmall);if(k==Bread||k==Water||k==Herb||k==Berry||k==Wine)button(xx+cw-150,yy+17,80,17,"Применить",5000+k);shown++;}
        wrapped(x+28,y+bh-92,bw-56,58,"Письмо, улика, кошелёк и ключи связаны с заданиями. Продать случайно их нельзя. Хлеб и вода восстанавливают потребности; трава лечит.",muted,fontSmall);
    }else if(panel==3){
        text(x+28,y+24,"СВОЁ МЕСТО В ЭТОМ МИРЕ",gold,heading);
        text(x+28,y+67,"Дерево: "+std::to_string(w.itemCount(Wood))+"   Камень: "+std::to_string(w.itemCount(Stone))+"   Волокно: "+std::to_string(w.itemCount(Fiber))+"   Семена: "+std::to_string(w.itemCount(Seed)),ink);
        wrapped(x+28,y+101,bw-56,63,"Q — собрать ресурс рядом. Строим на свободной земле в трёх метрах по направлению движения, по умолчанию справа. E — использовать, полить или собрать урожай. Постройки сохраняются вместе с миром.",muted,fontSmall);
        static const char* recipes[]={"Костёр · 3 дерева, 2 камня","Спальное место · 2 дерева, 3 волокна","Верстак · 6 дерева, 2 камня, 1 волокно","Грядка · 2 дерева, 1 волокно, 1 семя","Сундук у верстака · 4 дерева, 1 волокно","Навес · 8 дерева, 2 камня, 5 волокна"};
        int cw=(bw-74)/2;for(int k=0;k<ProjectCount;k++)button(x+28+(k%2)*(cw+18),y+190+(k/2)*53,cw,43,recipes[k],6000+k);
        button(x+28,y+358,cw,43,"Топор · 2 дерева, 1 камень, 1 волокно",6100);button(x+46+cw,y+358,cw,43,"Кирка · 3 дерева, 2 камня, 1 волокно",6101);
        button(x+28,y+411,cw,43,"2 лепёшки у огня · 2 ягоды, 1 вода",6102);button(x+46+cw,y+411,cw,43,"Забрать материалы из сундука",6200);
        int built=0;for(auto& p:w.projects)built+=p.active!=0;
        wrapped(x+28,y+483,bw-56,120,"Построено: "+std::to_string(built)+" / 64. Запас лагеря (дерево / камень / волокно / семена): "+std::to_string(w.stockpile[0])+" / "+std::to_string(w.stockpile[1])+" / "+std::to_string(w.stockpile[2])+" / "+std::to_string(w.stockpile[3])+".\nУрожай созревает за четыре игровых часа после полива. R — отдых у огня, постели или навеса. Это небольшой лагерь с проходной мебелью; земля и дома жителей пока не перестраиваются.",muted,fontSmall);
    }else{
        text(x+28,y+24,"ДЕЛА ВЕРЕСКОВОГО ПРЕДЕЛА",gold,heading);
        static const char* names[]={"Письмо через пепел","Масляные следы","Цена молчания","Тёплый настой"};
        static const char* descriptions[]={"Мира на рынке → запечатанное письмо → Илия в часовне. Не вскрывай печать.","Капитан Варрен → осмотреть вересковую тропу → принести масляный след дозору.","Нель в трактире предлагает украсть кошелёк Летты. Можно выполнить дело или раскрыть замысел Варрену.","Лекарь Эмери просит три пучка вереска. Собери их в чаще и передай лекарю, а не случайному прохожему."};
        int yy=y+80;for(int k=0;k<4;k++){auto state=w.player.quests[k];text(x+30,yy,names[k],state==3?lime:gold,heading);text(x+bw-180,yy+3,state==0?"Не начато":state==3?"Завершено":"В работе",muted,fontSmall);wrapped(x+30,yy+32,bw-60,62,descriptions[k],ink);yy+=120;}
    }
    button(x+bw-150,y+bh-47,120,31,"Закрыть",2000);
}
void render(){
    auto started=std::chrono::steady_clock::now();GdiFlush();buttons.clear();auto& w=*world;camera=atlas?Vec{Width/2,Height/2}:w.player.at;
    box(0,0,width,height,RGB(12,21,23));box(0,0,width,83,RGB(23,34,34));box(0,0,width,3,gold);
    text(25,17,"OUT / FIBER",gold,title);text(266,19,"ВЕРЕСКОВЫЙ ПРЕДЕЛ",ink,title);
    int hour=int(w.hour()),minute=int((w.hour()-hour)*60);char clock[64];std::snprintf(clock,sizeof(clock),"%02d:%02d",hour,minute);
    text(width-312,18,clock,gold,title);text(width-215,25,paused?"Пауза":atlas?"Карта мира":hour>=22||hour<6?"Ночь":hour<12?"Утро":hour<18?"День":"Вечер",muted);
    text(27,61,"Alex Merqury  ·  живая C++-песочница  ·  жизнь, память и многомерные матрицы",muted,fontSmall);
    int zone=w.map.nearest(w.player.at);text(26,86,atlas?"КАРТА · щелчок ставит путевую отметку; M возвращает к герою":zoneName(zone),muted,fontSmall);
    drawWorld();sidebar();
    box(12,height-87,width-24,74,RGB(25,38,37));wrapped(26,height-79,width-52,35,message,gold,fontSmall);
    text(26,height-41,"WASD · идти   Shift · бег   Ctrl · скрытность   E/T · общение   F · удар   Q · сбор   C · обустройство   I · сумка   J · задания   M · карта",muted,fontSmall);
    text(26,height-23,"5/6 · еда/вода   R · отдых   N · ждать час   L · осмотреться   F5/F6 · сохранить/загрузить   Space · пауза   F7 · нагрузка   Esc · закрыть",muted,fontSmall);
    overlay();renderMs=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-started).count();
    HDC dc=GetDC(windowHandle);BitBlt(dc,0,0,width,height,backDC,0,0,SRCCOPY);ReleaseDC(windowHandle,dc);
}
std::string savePath(){char* app=nullptr;size_t size=0;_dupenv_s(&app,&size,"LOCALAPPDATA");std::filesystem::path p=app?app:".";std::free(app);p/="OUT_FIBER";std::filesystem::create_directories(p);return (p/"HeatherReach-v02.save").string();}
void dispatch(int action){
    if(action>=6000){if(action==6200)message=world->withdrawStockpile();else if(action>=6100)message=world->craft(action-6100);else message=world->build(action-6000);panel=0;return;}
    if(action>=4000&&action<5000){world->choose(action-4000);return;}
    if(action>=5000){Item item=Item(action-5000);if(item==Bread||item==Water)message=world->consume(item==Water);else if(world->take(item)){if(item==Herb)world->player.hp=std::min(100.f,world->player.hp+12);if(item==Berry)world->player.hunger=std::max(0.f,world->player.hunger-.15f);if(item==Wine){world->player.warmth=clamp(world->player.warmth+.1f);world->player.stamina=std::max(0.f,world->player.stamina-8);}message="Использовано: "+std::string(itemName(item));}return;}
    switch(action){case 2000:panel=0;world->dialogue.open=false;break;case 2001:world->dialogue.open=false;panel=panel==1?0:1;break;case 2002:world->dialogue.open=false;panel=panel==2?0:2;break;case 2003:message=world->talk(selected>=0?selected:world->nearestActor(world->player.at,9));break;case 2004:atlas=!atlas;panel=0;break;case 2005:debug=!debug;break;case 2006:world->dialogue.open=false;panel=panel==3?0:3;break;}
}
void restart(size_t population){world=std::make_unique<World>(population,world?world->seed:41917,std::max(1u,std::min(8u,std::thread::hardware_concurrency())));selected=-1;panel=0;atlas=false;message="Новая история. У жителей есть дома, занятия и собственное время.";}
LRESULT CALLBACK procedure(HWND hwnd,UINT msg,WPARAM key,LPARAM value){
    switch(msg){
    case WM_SIZE:if(backDC)resize(LOWORD(value),HIWORD(value));return 0;
    case WM_GETMINMAXINFO:static_cast<MINMAXINFO*>(reinterpret_cast<void*>(value))->ptMinTrackSize={1130,830};return 0;
    case WM_DESTROY:PostQuitMessage(0);return 0;
    case WM_PAINT:{PAINTSTRUCT p;HDC dc=BeginPaint(hwnd,&p);if(backDC)BitBlt(dc,0,0,width,height,backDC,0,0,SRCCOPY);EndPaint(hwnd,&p);return 0;}
    case WM_ERASEBKGND:return 1;
    case WM_LBUTTONDOWN:{POINT point{GET_X_LPARAM(value),GET_Y_LPARAM(value)};for(auto it=buttons.rbegin();it!=buttons.rend();++it)if(PtInRect(&it->bounds,point)){dispatch(it->action);return 0;}if(panel||world->dialogue.open)return 0;
        if(point.x>13&&point.x<mapRight()-13&&point.y>102&&point.y<mapBottom()){
            float s=scale();Vec at{camera.x+(point.x-17-(mapRight()-34)/2)/s,camera.y+(point.y-107-(mapBottom()-112)/2)/s};
            if(atlas){waypoint=at;hasWaypoint=true;message="Путевая отметка поставлена. Карта не телепортирует героя или жителей.";}
            else{selected=world->nearestActor(at,8/s);if(selected>=0)message=world->actorName(selected)+": "+lifeName(world->actors[selected].life)+". Подойди и нажми T.";}
        }return 0;}
    case WM_MOUSEWHEEL:if(!atlas)zoom=clamp(zoom*(GET_WHEEL_DELTA_WPARAM(key)>0?1.13f:.885f),4,15);return 0;
    case WM_KEYDOWN:
        if(value&(1ll<<30))return 0;
        if(world->dialogue.open&&key>='1'&&key<='9'){size_t n=size_t(key-'1');if(n<world->dialogue.choices.size())world->choose(world->dialogue.choices[n].code);return 0;}
        switch(key){
        case VK_ESCAPE:if(panel||world->dialogue.open){panel=0;world->dialogue.open=false;}else DestroyWindow(hwnd);break;
        case VK_SPACE:paused=!paused;break;case VK_TAB:case 'M':atlas=!atlas;panel=0;break;
        case 'I':dispatch(2001);break;case 'J':dispatch(2002);break;
        case 'C':dispatch(2006);break;case 'Q':if(!panel&&!world->dialogue.open)message=world->gather();break;
        case 'E':if(!panel&&!world->dialogue.open)message=world->interact();break;
        case 'T':dispatch(2003);break;case 'F':if(!panel&&!world->dialogue.open)message=world->attack();break;
        case 'L':message=world->inspectSite();break;case 'R':message=world->rest();break;
        case 'N':message=world->waitHours(1);break;
        case '5':message=world->consume(false);break;case '6':message=world->consume(true);break;
        case 'B':world->signal(world->player.at,1.5f);message="Ты поднял тревогу. Её слышат только поблизости, а дальше разносят слухи.";break;
        case 'G':bloodVisible=!bloodVisible;break;
        case VK_F1:debug=!debug;break;case VK_F2:world->mode=Mode((int(world->mode)+1)%4);break;case VK_F3:world->fullAI=!world->fullAI;break;case VK_F4:labels=!labels;break;
        case VK_F5:message=world->save(savePath())?"История сохранена. Состояние мира, инвентарь и судьбы жителей записаны.":"Сохранить историю не удалось.";break;
        case VK_F6:{auto restored=World::load(savePath(),8);if(restored){world=std::move(restored);panel=0;selected=-1;message="История восстановлена.";}else message="Сохранение отсутствует, повреждено или относится к другой версии.";break;}
        case VK_F7:{size_t n=world->actors.size();restart(n<512?512:n<4096?4096:n<16384?16384:n<65536?65536:256);break;}
        case VK_F9:restart(256);break;
        }return 0;
    }
    return DefWindowProcW(hwnd,msg,key,value);
}
bool capture(const std::string& path){ULONG_PTR token=0;Gdiplus::GdiplusStartupInput init;if(Gdiplus::GdiplusStartup(&token,&init,nullptr)!=Gdiplus::Ok)return false;CLSID png={0x557cf406,0x1a04,0x11d3,{0x9a,0x73,0,0,0xf8,0x1e,0xf3,0x2e}};bool ok=false;{Gdiplus::Bitmap image(bitmap,nullptr);auto filename=wide(path);ok=image.Save(filename.c_str(),&png,nullptr)==Gdiplus::Ok;}Gdiplus::GdiplusShutdown(token);return ok;}
}
int fiberGui(int argc,char** argv){
    size_t population=256;uint32_t seed=41917;std::string screenshot,view;unsigned workers=std::max(1u,std::min(8u,std::thread::hardware_concurrency()));bool full=false,noLOD=false;
    for(int i=1;i<argc;i++){std::string a=argv[i];if(a=="--agents"&&i+1<argc)population=std::stoul(argv[++i]);else if(a=="--seed"&&i+1<argc)seed=uint32_t(std::stoul(argv[++i]));else if(a=="--workers"&&i+1<argc)workers=unsigned(std::stoul(argv[++i]));else if(a=="--capture"&&i+1<argc)screenshot=argv[++i];else if(a=="--view"&&i+1<argc)view=argv[++i];else if(a=="--full-ai")full=true;else if(a=="--no-lod")noLOD=true;}
    world=std::make_unique<World>(population,seed,workers);world->fullAI=full;world->lodEnabled=!noLOD;
    for(int i=1;i+1<argc;i++)if(std::string(argv[i])=="--mode"){std::string m=argv[i+1];world->mode=m=="strict"?Mode::Strict:m=="overlap"?Mode::Overlap:m=="utility"?Mode::Utility:Mode::Exploratory;break;}
    SetProcessDPIAware();GdiSetBatchLimit(1);auto instance=GetModuleHandleW(nullptr);WNDCLASSW cls{};cls.lpfnWndProc=procedure;cls.hInstance=instance;cls.lpszClassName=L"OUT_FIBER_TALES_02";cls.hCursor=LoadCursor(nullptr,IDC_ARROW);RegisterClassW(&cls);
    windowHandle=CreateWindowExW(0,cls.lpszClassName,L"OUT FIBER — Вересковый предел / Tales 0.2",WS_OVERLAPPEDWINDOW,CW_USEDEFAULT,CW_USEDEFAULT,width+16,height+39,nullptr,nullptr,instance,nullptr);if(!windowHandle)return 1;
    regular=CreateFontW(-17,0,0,0,FW_NORMAL,FALSE,FALSE,FALSE,DEFAULT_CHARSET,OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,CLEARTYPE_QUALITY,DEFAULT_PITCH,L"Segoe UI");fontSmall=CreateFontW(-13,0,0,0,FW_NORMAL,FALSE,FALSE,FALSE,DEFAULT_CHARSET,OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,CLEARTYPE_QUALITY,DEFAULT_PITCH,L"Segoe UI");heading=CreateFontW(-20,0,0,0,FW_SEMIBOLD,FALSE,FALSE,FALSE,DEFAULT_CHARSET,OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,CLEARTYPE_QUALITY,DEFAULT_PITCH,L"Segoe UI");title=CreateFontW(-25,0,0,0,FW_BOLD,FALSE,FALSE,FALSE,DEFAULT_CHARSET,OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,CLEARTYPE_QUALITY,DEFAULT_PITCH,L"Segoe UI");
    RECT client;GetClientRect(windowHandle,&client);resize(client.right,client.bottom);
    if(!screenshot.empty()){
        for(int i=0;i<120;i++)world->step();if(view=="world"){atlas=true;debug=true;}else if(view=="inventory")panel=1;else if(view=="journal")panel=2;else if(view=="crafting")panel=3;else if(view=="dialogue"){selected=world->nearestActor(world->player.at,9);world->openConversation(selected);}else if(view=="night"){world->timeOffset=15*1800;for(int i=0;i<2400;i++)world->step();}
        render();render();bool ok=capture(screenshot);DestroyWindow(windowHandle);return ok?0:1;
    }
    ShowWindow(windowHandle,SW_SHOW);auto prior=std::chrono::steady_clock::now();double accumulator=0;bool running=true;
    while(running){MSG msg;while(PeekMessageW(&msg,nullptr,0,0,PM_REMOVE)){if(msg.message==WM_QUIT){running=false;break;}TranslateMessage(&msg);DispatchMessageW(&msg);}if(!running)break;
        auto now=std::chrono::steady_clock::now();double elapsed=std::chrono::duration<double>(now-prior).count();prior=now;
        if(!paused){accumulator+=std::min(.2,elapsed);int steps=0;while(accumulator>=Dt&&steps<4){bool active=GetForegroundWindow()==windowHandle&&!panel&&!world->dialogue.open;world->input={active?float((GetAsyncKeyState('D')<0)-(GetAsyncKeyState('A')<0)):0,active?float((GetAsyncKeyState('S')<0)-(GetAsyncKeyState('W')<0)):0,active&&GetAsyncKeyState(VK_SHIFT)<0,active&&GetAsyncKeyState(VK_CONTROL)<0};world->step();accumulator-=Dt;steps++;}if(accumulator>=Dt){dropped+=accumulator;accumulator=0;}}else accumulator=0;
        render();::Sleep(8);
    }
    SelectObject(backDC,oldBitmap);DeleteObject(bitmap);DeleteDC(backDC);DeleteObject(regular);DeleteObject(fontSmall);DeleteObject(heading);DeleteObject(title);world.reset();return 0;
}
