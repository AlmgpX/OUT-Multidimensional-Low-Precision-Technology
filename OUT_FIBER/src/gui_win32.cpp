// Copyright (c) 2026 Алексей Савельев (Alex Merqury / AlexMerq).
// SPDX-License-Identifier: LicenseRef-OUT-Source-Review-1.0
#define NOMINMAX
#include <windows.h>
#include <windowsx.h>
#include <gdiplus.h>
#include "fiber.hpp"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <memory>

namespace {
using namespace fiber;
std::unique_ptr<World> world;
HWND windowHandle=nullptr;HDC backDC=nullptr;HBITMAP bitmap=nullptr,oldBitmap=nullptr;
HFONT regular=nullptr,fontSmall=nullptr,title=nullptr;
uint32_t* surface=nullptr;bool mapClip=false;
int width=1360,height=880,selected=-1;bool paused=false,follow=false,showLabels=true,fullView=true;
float zoom=2.2f;Vec camera{Width/2,Height/2};double renderMs=0,dropped=0;int stepsLast=0;
std::string message="Heather Reach | talk to a citizen to begin the chapel task.";
COLORREF ink=RGB(225,236,229),muted=RGB(139,165,163),lime=RGB(172,239,122);
int mapRight(){return width-330;}int mapBottom(){return height-108;}
float scale(){return fullView?std::min((mapRight()-32)/Width,(mapBottom()-120)/Height):zoom;}
int screenX(float x){return 16+(mapRight()-32)/2+int((x-camera.x)*scale());}
int screenY(float y){return 116+(mapBottom()-120)/2+int((y-camera.y)*scale());}
void box(int x,int y,int w,int h,COLORREF color){
    int left=std::max(mapClip?13:0,x),right=std::min(mapClip?mapRight()-13:width,x+w);
    int top=std::max(mapClip?101:0,y),bottom=std::min(mapClip?mapBottom()-1:height,y+h);
    if(left>=right||top>=bottom||!surface)return;
    uint32_t pixel=((color&255)<<16)|(color&0xff00)|((color>>16)&255);
    for(int row=top;row<bottom;row++)std::fill(surface+row*width+left,surface+row*width+right,pixel);
}
void text(int x,int y,const std::string& s,COLORREF color=RGB(225,236,229),HFONT font=nullptr){SelectObject(backDC,font?font:regular);SetTextColor(backDC,color);SetBkMode(backDC,TRANSPARENT);TextOutA(backDC,x,y,s.c_str(),int(s.size()));}
std::string number(double n,int digits=1){char b[80];std::snprintf(b,sizeof(b),"%.*f",digits,n);return b;}
void wrapped(int x,int y,int w,int h,const std::string& s,COLORREF color){RECT r{x,y,x+w,y+h};SelectObject(backDC,fontSmall);SetTextColor(backDC,color);DrawTextA(backDC,s.c_str(),int(s.size()),&r,DT_LEFT|DT_WORDBREAK|DT_NOPREFIX);}
COLORREF actorColor(const Actor& a){if(a.hp<=0)return RGB(86,83,81);switch(a.role){case Guard:return RGB(117,207,235);case Merchant:return RGB(242,200,111);case Healer:return RGB(168,222,175);case Bandit:return RGB(229,120,109);case Wolf:return RGB(178,157,201);default:return RGB(199,210,174);}}
void meter(int x,int y,const char* label,float value,COLORREF color){text(x,y,label,muted,fontSmall);box(x+76,y+3,202,8,RGB(35,51,57));box(x+76,y+3,int(202*clamp(value)),8,color);}
void resize(int w,int h){width=std::max(960,w);height=std::max(700,h);if(bitmap){SelectObject(backDC,oldBitmap);DeleteObject(bitmap);}if(!backDC){HDC dc=GetDC(windowHandle);backDC=CreateCompatibleDC(dc);ReleaseDC(windowHandle,dc);}BITMAPINFO info{};info.bmiHeader.biSize=sizeof(BITMAPINFOHEADER);info.bmiHeader.biWidth=width;info.bmiHeader.biHeight=-height;info.bmiHeader.biPlanes=1;info.bmiHeader.biBitCount=32;info.bmiHeader.biCompression=BI_RGB;void* pixels=nullptr;bitmap=CreateDIBSection(backDC,&info,DIB_RGB_COLORS,&pixels,nullptr,0);surface=static_cast<uint32_t*>(pixels);oldBitmap=(HBITMAP)SelectObject(backDC,bitmap);}
void restart(size_t population){uint32_t seed=world?world->seed:41917;Mode mode=world?world->mode:Mode::Exploratory;bool fullAI=world&&world->fullAI;world=std::make_unique<World>(population,seed,std::max(1u,std::min(8u,std::thread::hardware_concurrency())));world->mode=mode;world->fullAI=fullAI;world->signal(world->map.sites[0].at,1.7f);selected=-1;message="Fresh deterministic world. NPC cognition and movement are active.";}
void render(){
    auto started=std::chrono::steady_clock::now();GdiFlush();auto& w=*world;
    if(follow){camera=w.player.at;fullView=false;}else if(fullView)camera={Width/2,Height/2};
    box(0,0,width,height,RGB(10,18,24));box(0,0,width,90,RGB(16,30,36));box(0,0,width,4,lime);
    text(24,18,"OUT / FIBER",lime,title);text(272,22,"HEATHER REACH",ink,title);
    text(26,62,"NATIVE C++  /  3D BRANCHES  +  4D MOTOR  +  5D CONTEXT",muted,fontSmall);
    text(width-328,22,paused?"PAUSED":modeName(w.mode),lime);text(width-328,49,"Alex Merqury  |  benchmark 0.1",muted,fontSmall);
    box(12,100,mapRight()-24,mapBottom()-100,RGB(18,33,32));
    auto tileColor=[](uint8_t t){switch(t){case 1:return RGB(76,77,59);case 2:return RGB(35,76,58);case 3:return RGB(112,107,93);case 4:return RGB(32,71,87);case 5:return RGB(122,98,65);case 6:return RGB(46,65,43);case 7:return RGB(49,53,48);default:return RGB(31,49,41);}};
    HRGN clip=CreateRectRgn(13,101,mapRight()-13,mapBottom()-1);SelectClipRgn(backDC,clip);mapClip=true;
    float s=scale();int tilePixels=std::max(1,int(Tile*s)+1);
    for(int y=0;y<MapH;y++)for(int x=0;x<MapW;x++){
        int xx=screenX(x*Tile),yy=screenY(y*Tile);if(xx<8-tilePixels||xx>mapRight()||yy<98-tilePixels||yy>mapBottom())continue;
        box(xx,yy,tilePixels,tilePixels,tileColor(w.map.tiles[y*MapW+x]));
    }
    int labels=0,visible=0;size_t alive=0;std::array<int,Actions> actions{};
    std::array<RECT,48> occupied{};int occupiedCount=0;
    for(auto& site:w.map.sites){int xx=screenX(site.at.x),yy=screenY(site.at.y);occupied[occupiedCount++]={xx-2,yy-11,xx+160,yy+10};}
    int playerX=screenX(w.player.at.x),playerY=screenY(w.player.at.y);occupied[occupiedCount++]={playerX-9,playerY-12,playerX+80,playerY+23};
    SelectObject(backDC,fontSmall);
    for(size_t i=0;i<w.actors.size();i++){
        auto& a=w.actors[i];if(a.hp>0){alive++;actions[a.mind.action]++;}
        int xx=screenX(w.x[i]),yy=screenY(w.y[i]);if(xx<14||xx>mapRight()-14||yy<101||yy>mapBottom()-2)continue;visible++;
        COLORREF color=actorColor(a);int r=a.hp>0?std::max(2,int(s*.4f)):1;box(xx-r,yy-r,r*2+1,r*2+1,color);
        if(a.hp>0&&showLabels&&int(i)!=selected&&labels<18&&std::abs(w.x[i]-w.player.at.x)<55&&std::abs(w.y[i]-w.player.at.y)<55){
            auto label=std::string(roleName(a.role))+" #"+std::to_string(i)+"  "+actionName(a.mind.action);
            RECT bounds{xx+5,yy-16,xx+10+int(label.size())*7,yy-1};bool overlap=bounds.right>mapRight()-15;RECT intersection;
            for(int k=0;k<occupiedCount;k++)if(IntersectRect(&intersection,&bounds,&occupied[k]))overlap=true;
            if(!overlap){occupied[occupiedCount++]=bounds;text(bounds.left,bounds.top,label,color,fontSmall);labels++;}
        }
        if(int(i)==selected){HPEN p=CreatePen(PS_SOLID,1,lime);auto old=SelectObject(backDC,p);auto oldBrush=SelectObject(backDC,GetStockObject(HOLLOW_BRUSH));Ellipse(backDC,xx-8,yy-8,xx+9,yy+9);SelectObject(backDC,oldBrush);SelectObject(backDC,old);DeleteObject(p);}
    }
    if(selected>=0&&selected<int(w.actors.size())){auto& a=w.actors[selected];int xx=screenX(w.x[selected]),yy=screenY(w.y[selected]);auto label=std::string(roleName(a.role))+" #"+std::to_string(selected)+" | "+actionName(a.mind.action);box(xx+6,yy-22,int(label.size())*7+10,18,RGB(12,25,29));text(xx+10,yy-20,label,lime,fontSmall);}
    for(int i=0;i<8;i++){auto& site=w.map.sites[i];int xx=screenX(site.at.x),yy=screenY(site.at.y);box(xx-3,yy-3,7,7,RGB(218,190,127));text(xx+7,yy-7,site.name,RGB(235,210,153),fontSmall);}
    int px=screenX(w.player.at.x),py=screenY(w.player.at.y);box(px-4,py-4,9,9,lime);text(px-4,py-10,"@",RGB(12,30,18));text(px+10,py+6,w.player.hp>0?"YOU":"FALLEN [F9]",lime,fontSmall);
    SelectClipRgn(backDC,nullptr);DeleteObject(clip);mapClip=false;
    int sx=width-314,y=109;auto row=[&](const std::string& a,const std::string& b){text(sx,y,a,muted,fontSmall);text(sx+148,y,b,ink,fontSmall);y+=21;};
    text(sx,y,"LIVE SIMULATION",lime);y+=30;
    row("Entities / alive",std::to_string(w.actors.size())+" / "+std::to_string(alive));row("Workers / visible",std::to_string(w.workers())+" / "+std::to_string(visible));row("Tick / world hour",std::to_string(w.tick)+" / "+number(w.hour()));
    row("Simulation tick",number(w.metrics.totalMs,2)+" ms");row("Decision phase",number(w.metrics.thinkMs,2)+" ms");row("Movement phase",number(w.metrics.moveMs,2)+" ms");row("Render CPU",number(renderMs,2)+" ms");
    row("Decisions / tick",std::to_string(w.metrics.decisions));row("Cognition",w.fullAI?"60 Hz (FULL)":"10 Hz staggered");row("Capped perceptions",std::to_string(w.metrics.capped));row("Safety-filtered",std::to_string(w.metrics.safetyBlocks));row("Shared bank",number(w.bank.bytes()/1024.,1)+" KiB");row("Memory / agent",std::to_string(sizeof(Mind))+" B policy");row("Owned sim memory",number(w.bytes()/1048576.,2)+" MiB");row("Catch-up dropped",number(dropped)+" s");
    y+=8;text(sx,y,"PLAYER / SURVIVAL",lime);y+=28;
    meter(sx,y,"Health",w.player.hp/100,RGB(150,214,148));y+=22;meter(sx,y,"Stamina",w.player.stamina/100,RGB(126,199,228));y+=22;meter(sx,y,"Food",1-w.player.hunger,RGB(218,183,114));y+=22;meter(sx,y,"Water",1-w.player.thirst,RGB(123,184,230));y+=22;meter(sx,y,"Warmth",w.player.warmth,RGB(225,157,110));y+=25;
    row("Bread / water",std::to_string(w.player.food)+" / "+std::to_string(w.player.water));row("Gold / heather",std::to_string(w.player.gold)+" / "+std::to_string(w.player.herbs));row("Wanted",number(w.wanted));
    if(selected>=0&&selected<int(w.actors.size())){
        auto& a=w.actors[selected];y+=5;text(sx,y,"SELECTED #"+std::to_string(selected),lime);y+=25;
        row(roleName(a.role),actionName(a.mind.action));row("Health / fear",number(a.hp,0)+" / "+number(a.mind.fear,2));row("Cue / branch",std::to_string(a.mind.cue)+" / "+std::to_string(a.mind.branch));row("Confidence",number(a.mind.confidence,2));row("Rumor / relation",std::to_string(a.rumor)+" / "+number(a.affinity,2));
    }else{y+=5;text(sx,y,"ACTION DISTRIBUTION",lime);y+=25;for(int i=0;i<Actions&&y<height-115;i++)row(actionName(i),std::to_string(actions[i]));}
    box(12,height-98,width-24,86,RGB(19,36,43));
    wrapped(25,height-88,width-50,29,message,RGB(231,207,158));
    text(25,height-54,"WASD move | Shift sprint | Ctrl sneak | E interact | F strike | 5 eat | 6 drink | R rest | B bell | T talk",muted,fontSmall);
    text(25,height-33,"Space pause | Tab follow/map | Wheel zoom | Click inspect | F2 mode | F3 FULL AI | 1/2/3/4 population | F9 reset | Esc exit",muted,fontSmall);
    renderMs=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-started).count();
    HDC dc=GetDC(windowHandle);BitBlt(dc,0,0,width,height,backDC,0,0,SRCCOPY);ReleaseDC(windowHandle,dc);
}
LRESULT CALLBACK procedure(HWND hwnd,UINT msg,WPARAM key,LPARAM value){
    switch(msg){
    case WM_SIZE:if(backDC)resize(LOWORD(value),HIWORD(value));return 0;
    case WM_GETMINMAXINFO:{auto p=(MINMAXINFO*)value;p->ptMinTrackSize={1000,760};return 0;}
    case WM_DESTROY:PostQuitMessage(0);return 0;
    case WM_PAINT:{PAINTSTRUCT ps;HDC dc=BeginPaint(hwnd,&ps);if(backDC)BitBlt(dc,0,0,width,height,backDC,0,0,SRCCOPY);EndPaint(hwnd,&ps);return 0;}
    case WM_ERASEBKGND:return 1;
    case WM_LBUTTONDOWN:{float s=scale();Vec p{camera.x+(GET_X_LPARAM(value)-16-(mapRight()-32)/2)/s,camera.y+(GET_Y_LPARAM(value)-116-(mapBottom()-120)/2)/s};selected=world->nearestActor(p,8/s);return 0;}
    case WM_MOUSEWHEEL:fullView=false;follow=false;zoom=clamp(zoom*(GET_WHEEL_DELTA_WPARAM(key)>0?1.15f:.87f),1,10);return 0;
    case WM_KEYDOWN:
        if(value&(1ll<<30))return 0;
        switch(key){
        case VK_ESCAPE:DestroyWindow(hwnd);break;
        case VK_SPACE:paused=!paused;break;
        case VK_TAB:follow=!follow;fullView=!follow;break;
        case 'E':message=world->interact();break;
        case 'F':message=world->attack();break;
        case 'R':message=world->rest();break;
        case 'T':message=world->talk(world->nearestActor(world->player.at,8));break;
        case 'B':world->signal(world->player.at,2);message="You raise a local alarm. Watch the rumor IDs and decisions.";break;
        case '5':message=world->consume(false);break;case '6':message=world->consume(true);break;
        case '1':restart(512);break;case '2':restart(4096);break;case '3':restart(16384);break;case '4':restart(65536);break;
        case VK_F2:world->mode=Mode((int(world->mode)+1)%4);break;
        case VK_F3:world->fullAI=!world->fullAI;break;
        case VK_F4:showLabels=!showLabels;break;
        case VK_F9:restart(world->actors.size());break;
        }return 0;
    }
    return DefWindowProcW(hwnd,msg,key,value);
}
bool capture(const std::string& path){
    ULONG_PTR token=0;Gdiplus::GdiplusStartupInput init;if(Gdiplus::GdiplusStartup(&token,&init,nullptr)!=Gdiplus::Ok)return false;
    CLSID png={0x557cf406,0x1a04,0x11d3,{0x9a,0x73,0,0,0xf8,0x1e,0xf3,0x2e}};
    int n=MultiByteToWideChar(CP_UTF8,0,path.c_str(),-1,nullptr,0);std::wstring wide(size_t(n),0);MultiByteToWideChar(CP_UTF8,0,path.c_str(),-1,wide.data(),n);bool ok=false;
    {Gdiplus::Bitmap image(bitmap,nullptr);ok=image.Save(wide.c_str(),&png,nullptr)==Gdiplus::Ok;}Gdiplus::GdiplusShutdown(token);return ok;
}
}
int fiberGui(int argc,char** argv){
    size_t population=2048;uint32_t seed=41917;std::string screenshot;unsigned workers=std::max(1u,std::min(8u,std::thread::hardware_concurrency()));Mode mode=Mode::Exploratory;bool fullAI=false;
    for(int i=1;i<argc;i++){std::string a=argv[i];if(a=="--agents"&&i+1<argc)population=std::stoul(argv[++i]);else if(a=="--seed"&&i+1<argc)seed=uint32_t(std::stoul(argv[++i]));else if(a=="--workers"&&i+1<argc)workers=unsigned(std::stoul(argv[++i]));else if(a=="--capture"&&i+1<argc)screenshot=argv[++i];else if(a=="--full-ai")fullAI=true;else if(a=="--mode"&&i+1<argc){std::string v=argv[++i];mode=v=="strict"?Mode::Strict:v=="overlap"?Mode::Overlap:v=="utility"?Mode::Utility:Mode::Exploratory;}}
    world=std::make_unique<World>(population,seed,workers);world->mode=mode;world->fullAI=fullAI;world->signal(world->map.sites[0].at,1.7f);
    SetProcessDPIAware();GdiSetBatchLimit(1);auto instance=GetModuleHandleW(nullptr);WNDCLASSW cls{};cls.lpfnWndProc=procedure;cls.hInstance=instance;cls.lpszClassName=L"OUT_FIBER_NATIVE";cls.hCursor=LoadCursor(nullptr,IDC_ARROW);RegisterClassW(&cls);
    windowHandle=CreateWindowExW(0,cls.lpszClassName,L"OUT FIBER | Heather Reach | Multidimensional Low-Precision Benchmark",WS_OVERLAPPEDWINDOW,CW_USEDEFAULT,CW_USEDEFAULT,width+16,height+39,nullptr,nullptr,instance,nullptr);
    if(!windowHandle)return 1;
    regular=CreateFontW(-15,0,0,0,FW_NORMAL,FALSE,FALSE,FALSE,DEFAULT_CHARSET,OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,CLEARTYPE_QUALITY,DEFAULT_PITCH,L"Segoe UI");
    fontSmall=CreateFontW(-12,0,0,0,FW_NORMAL,FALSE,FALSE,FALSE,DEFAULT_CHARSET,OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,CLEARTYPE_QUALITY,DEFAULT_PITCH,L"Consolas");
    title=CreateFontW(-25,0,0,0,FW_BOLD,FALSE,FALSE,FALSE,DEFAULT_CHARSET,OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,CLEARTYPE_QUALITY,DEFAULT_PITCH,L"Segoe UI");
    RECT client;GetClientRect(windowHandle,&client);resize(client.right,client.bottom);
    if(screenshot.empty())ShowWindow(windowHandle,SW_SHOW);else{for(int i=0;i<180;i++)world->step();selected=world->nearestActor(world->player.at,15);render();render();bool ok=capture(screenshot);DestroyWindow(windowHandle);return ok?0:1;}
    auto prior=std::chrono::steady_clock::now();double accumulator=0;bool running=true;
    while(running){MSG msg;while(PeekMessageW(&msg,nullptr,0,0,PM_REMOVE)){if(msg.message==WM_QUIT){running=false;break;}TranslateMessage(&msg);DispatchMessageW(&msg);}if(!running)break;
        auto now=std::chrono::steady_clock::now();double elapsed=std::chrono::duration<double>(now-prior).count();prior=now;
        if(!paused){accumulator+=std::min(.2,elapsed);stepsLast=0;
            while(accumulator>=Dt&&stepsLast<4){bool focused=GetForegroundWindow()==windowHandle;world->input={focused?float((GetAsyncKeyState('D')<0)-(GetAsyncKeyState('A')<0)):0,focused?float((GetAsyncKeyState('S')<0)-(GetAsyncKeyState('W')<0)):0,focused&&GetAsyncKeyState(VK_SHIFT)<0,focused&&GetAsyncKeyState(VK_CONTROL)<0};world->step();accumulator-=Dt;stepsLast++;}
            if(accumulator>=Dt){dropped+=accumulator;accumulator=0;}
        }else accumulator=0;
        render();Sleep(8);
    }
    SelectObject(backDC,oldBitmap);DeleteObject(bitmap);DeleteDC(backDC);DeleteObject(regular);DeleteObject(fontSmall);DeleteObject(title);world.reset();return 0;
}
