// SPDX-License-Identifier: LicenseRef-OUT-Source-Review-1.0
// Copyright (c) 2026 Алексей Савельев (Alex Merqury). See ../../LICENSE.
#include "fiber.hpp"
#include <algorithm>
#include <cmath>

namespace fiber {
static float range(Vec a,Vec b){return std::hypot(a.x-b.x,a.y-b.y);}
const char* projectName(int k){static const char* names[]={"Костёр","Спальное место","Верстак","Грядка","Сундук","Навес"};return names[std::max(0,std::min(int(ProjectCount)-1,k))];}
void World::initSandbox(){
    // New deterministic world resources, not an imported dataset or mutable policy.
    for(size_t i=0;i<resources.size();i++){
        auto& node=resources[i];node.kind=uint8_t(Wood+hash(seed,uint32_t(i),814)%4);node.stock=4;
        for(uint32_t attempt=0;attempt<300;attempt++){
            uint32_t key=hash(seed,uint32_t(i),attempt,815);Vec at{8+random01(key)*(Width-16),8+random01(hash(key,1))*(Height-16)};
            if(i<8){float angle=float(i)*.785398f;at={player.at.x+std::cos(angle)*15,player.at.y+std::sin(angle)*15};at.x+=float(attempt%5)*2;at.y+=float(attempt/5)*2;node.kind=uint8_t(Wood+i%4);}
            auto tile=map.tile(int(at.x/Tile),int(at.y/Tile));if((tile==0||tile==6)&&map.clear(at.x,at.y)){node.at=at;break;}
        }
        if(!map.clear(node.at.x,node.at.y))node.stock=0;
    }
    give(Seed,2);
}
int World::nearbyProject(Vec at,int kind,float radius)const{
    int best=-1;for(size_t i=0;i<projects.size();i++)if(projects[i].active&&(kind<0||projects[i].kind==kind)){
        float d=range(at,projects[i].at);if(d<radius&&map.line(at,projects[i].at)){best=int(i);radius=d;}
    }return best;
}
std::string World::gather(){
    if(player.hp<=0||player.cooldown>0)return "Подожди, пока закончишь действие.";
    if(player.stamina<5)return "Нужно перевести дух.";
    int best=-1;float radius=5;
    for(size_t i=0;i<resources.size();i++)if(resources[i].stock){float d=range(player.at,resources[i].at);if(d<radius&&map.line(player.at,resources[i].at)){best=int(i);radius=d;}}
    if(best<0)return "Нужны ветви, каменная россыпь, волокно или дикие семена рядом. Их видно на земле; Q — собрать.";
    auto& node=resources[best];int amount=1;
    if((node.kind==Wood&&itemCount(Axe))||(node.kind==Stone&&itemCount(Pick)))amount=2;
    amount=std::min(amount,int(node.stock));give(Item(node.kind),amount);node.stock=uint8_t(node.stock-amount);
    if(!node.stock)node.renewAt=tick+7200;
    player.stamina-=4;player.cooldown=.8f;
    return "Собрано: "+std::string(itemName(node.kind))+" × "+std::to_string(amount)+". Запас участка конечен и восстановится позже.";
}
std::string World::craft(int recipe){
    if(player.hp<=0||player.cooldown>0)return "Подожди, пока закончишь действие.";
    if(recipe<0||recipe>2)return "Неизвестный рецепт.";
    if(recipe<2){
        Item tool=recipe?Pick:Axe;if(itemCount(tool))return "Этот инструмент у тебя уже есть.";
        int wood=recipe?3:2,stone=recipe?2:1;
        if(itemCount(Wood)<wood||itemCount(Stone)<stone||!itemCount(Fiber))return "Не хватает материалов для инструмента.";
        take(Wood,wood);take(Stone,stone);take(Fiber);give(tool);player.cooldown=1;
        return "Изготовлено: "+std::string(itemName(tool))+". Подходящие ресурсы собираются по две единицы.";
    }
    if(nearbyProject(player.at,Campfire,5)<0)return "Для лепёшки нужен свой костёр рядом.";
    if(itemCount(Berry)<2||itemCount(Water)<1)return "Нужны две порции ягод и вода.";
    take(Berry,2);take(Water);give(Bread,2);player.cooldown=1;return "У огня готовы две дорожные лепёшки. В сумке они считаются хлебом.";
}
std::string World::build(int kind){
    if(kind<0||kind>=ProjectCount||player.hp<=0||player.cooldown>0)return "Сейчас строить не получится.";
    if(wanted>0)return "Сначала разберись с дозором: сейчас спокойно обустроиться нельзя.";
    static const int costs[ProjectCount][4]={{3,2,0,0},{2,0,3,0},{6,2,1,0},{2,0,1,1},{4,0,1,0},{8,2,5,0}};
    for(int k=0;k<4;k++)if(itemCount(Item(Wood+k))<costs[kind][k])return "Не хватает материалов. Рецепт указан в меню обустройства.";
    if(kind==Storage&&nearbyProject(player.at,Workbench,12)<0)return "Сундук собирают у своего верстака: подойди к нему.";
    int slot=-1;for(size_t i=0;i<projects.size();i++)if(!projects[i].active){slot=int(i);break;}if(slot<0)return "Достигнут предел 64 предметов обустройства.";
    float dx=input.x,dy=input.y,n=std::hypot(dx,dy);if(n<.1f){dx=1;dy=0;n=1;}
    Vec at{player.at.x+dx/n*3,player.at.y+dy/n*3};auto tile=map.tile(int(at.x/Tile),int(at.y/Tile));
    if((tile!=0&&tile!=6)||!map.clear(at.x,at.y,2)||!map.line(player.at,at))return "Выбери свободную землю вне дороги и чужих домов. Предмет появится в трёх метрах по направлению движения, по умолчанию справа.";
    for(auto& project:projects)if(project.active&&range(at,project.at)<3)return "Здесь уже стоит твоя постройка. Оставь между предметами три метра.";
    if(nearbyProject(at,-1,3)>=0)return "Нужно больше места.";
    for(int k=0;k<4;k++)take(Item(Wood+k),costs[kind][k]);
    projects[slot]={at,0,uint8_t(kind),1,0};player.cooldown=1;
    return "Построено: "+std::string(projectName(kind))+". E — использовать рядом; C — обустройство.";
}
std::string World::useProject(){
    if(player.hp<=0||player.cooldown>0)return "Подожди немного.";
    int index=nearbyProject(player.at,-1,4);if(index<0)return {};
    auto& p=projects[index];if(p.kind==Bedroll||p.kind==Shelter)return rest();player.cooldown=.6f;
    switch(p.kind){
    case Campfire:player.warmth=1;return "Огонь греет руки. R — отдохнуть, C — испечь лепёшки.";
    case Workbench:return "Верстак готов. Открой C: инструменты, сундук, грядки и лагерь.";
    case Storage:{int moved=0;for(int k=0;k<4;k++){int amount=std::min(itemCount(Item(Wood+k)),9999-stockpile[k]);take(Item(Wood+k),amount);stockpile[k]+=amount;moved+=amount;}return "В общий запас лагеря убрано: "+std::to_string(moved)+". Забрать материалы можно через C рядом с сундуком.";}
    case Garden:
        if(p.growth>=4){give(Berry,4);give(Seed,2);p.growth=0;p.watered=0;return "Урожай: четыре порции ягод и два семени. Многолетний куст снова ждёт воды.";}
        if(p.watered)return "Грядка полита. Созреет через игровых часов: "+std::to_string(std::max(1,int(std::ceil(4-p.growth))))+".";
        if(!take(Water))return "Для полива нужен бурдюк воды.";
        p.watered=1;return "Ты полил грядку. Ягодам нужно четыре игровых часа; E — собрать созревший урожай.";
    default:return {};
    }
}
std::string World::withdrawStockpile(){
    if(nearbyProject(player.at,Storage,5)<0)return "Подойди к своему сундуку.";
    int moved=0;for(int k=0;k<4;k++){int amount=std::min(stockpile[k],9999-itemCount(Item(Wood+k)));give(Item(Wood+k),amount);stockpile[k]-=amount;moved+=amount;}
    return "Забрано из запаса лагеря: "+std::to_string(moved)+".";
}
void World::updateSandbox(float hours){
    for(auto& p:projects)if(p.active&&p.kind==Garden&&p.watered)p.growth=std::min(4.f,p.growth+hours);
    if(nearbyProject(player.at,Campfire,6)>=0||nearbyProject(player.at,Shelter,5)>=0)player.warmth=std::min(1.f,player.warmth+.02f);
    for(auto& node:resources)if(!node.stock&&node.renewAt&&tick>=node.renewAt){node.stock=4;node.renewAt=0;}
}
}
