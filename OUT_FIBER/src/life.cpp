// SPDX-License-Identifier: LicenseRef-OUT-Source-Review-1.0
// Copyright (c) 2026 Алексей Савельев (Alex Merqury). See ../../LICENSE.
#include "fiber.hpp"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <stdexcept>
#include <type_traits>

namespace fiber {
static float distance(Vec a,Vec b){float x=a.x-b.x,y=a.y-b.y;return std::sqrt(x*x+y*y);}
void World::planLife(size_t i){
    auto& a=actors[i];float h=hour();bool night=h>=22||h<6;
    bool revise=tick>=a.activityUntil||a.hunger>.92f||a.thirst>.92f||(night&&a.life!=Sleep&&a.role!=Guard&&a.team==0);
    if(revise){
        auto life=bank.chooseLife(a.hunger,a.thirst,a.fatigue,h,Role(a.role),a.loneliness,a.stress,hash(seed,uint32_t(i)));
        if(a.role==Guard){float shift=float(hash(seed,uint32_t(i))%3)*8;bool duty=std::fmod(h-shift+24.f,24.f)<8;if(!duty&&(night||a.fatigue>.4f))life=Sleep;else if(duty&&a.hunger<.8f&&a.thirst<.8f)life=Patrol;}
        if(a.role==Innkeeper){if((h>=16||h<2)&&a.hunger<.8f&&a.thirst<.8f)life=Work;else if(h>=2&&h<10)life=Sleep;}
        if(a.hp<45&&a.team==0)life=Recover;
        else if(a.grief>.55f&&a.hunger<.8f&&a.thirst<.8f)life=Pray;
        a.life=uint8_t(life);a.activityUntil=tick+1200+hash(seed,uint32_t(i),tick/600)%1800;
    }
    switch(Life(a.life)){
    case Sleep:case Recover:case Loiter:a.goal=a.homeAt;break;
    case Eat:a.goal=a.food?a.homeAt:map.sites[1].at;break;
    case Drink:a.goal=map.buildings[a.residence].door;break;
    case Socialize:a.goal=map.sites[a.socialSite].at;break;
    case Pray:a.goal=map.sites[7].at;break;
    case Patrol:{int target=(hash(seed,uint32_t(i),tick/1800)%3==0)?0:a.workSite;a.goal=map.sites[target].at;break;}
    case Forage:a.goal=map.sites[4].at;break;
    case Rob:a.goal=map.sites[(tick/1800+i)%2?6:5].at;break;
    default:a.goal=map.sites[a.workSite].at;break;
    }
    // Workplaces and social areas have fixed personal stations, not one shared pixel.
    if(a.life==Work||a.life==Socialize||a.life==Patrol){
        uint32_t key=hash(seed,uint32_t(i),a.life);float dx=(random01(key)*2-1)*5,dy=(random01(hash(key,1))*2-1)*5;
        if(map.clear(a.goal.x+dx,a.goal.y+dy)){a.goal.x+=dx;a.goal.y+=dy;}
    }
    if(a.team==0&&a.character<0&&(a.life==Socialize||a.life==Loiter)){
        int fire=nearbyProject({x[i],y[i]},Campfire,25);if(fire>=0){a.goal=projects[fire].at;a.goal.x+=float(int(i%5)-2)*1.2f;}
    }
}
std::string World::actorName(int i)const{
    if(i<0||i>=int(actors.size()))return "Незнакомец";auto& a=actors[i];
    if(a.character>=0&&a.character<int(characters().size()))return characters()[a.character].name;
    static const char* first[]={"Аран","Лиса","Дален","Руна","Борн","Тая","Эльт","Фрей","Марта","Север"};
    return std::string(first[hash(seed,uint32_t(i))%10])+" "+std::to_string(i);
}
void World::note(const std::string& text){auto& n=notices[noticeCursor++%notices.size()];n.tick=tick;std::snprintf(n.text.data(),n.text.size(),"%s",text.c_str());}
int World::itemCount(Item i)const{if(i==Bread)return player.food;if(i==Water)return player.water;if(i==Herb)return player.herbs;return i<ItemCount?player.bag[i]:0;}
bool World::take(Item i,int n){if(n<0||itemCount(i)<n)return false;if(i==Bread)player.food-=n;else if(i==Water)player.water-=n;else if(i==Herb)player.herbs-=n;else if(i<ItemCount)player.bag[i]-=n;else return false;return true;}
void World::give(Item i,int n){if(n<0||i>=ItemCount)return;if(i==Bread)player.food=std::min(9999,player.food+n);else if(i==Water)player.water=std::min(9999,player.water+n);else if(i==Herb)player.herbs=std::min(9999,player.herbs+n);else player.bag[i]=std::min(9999,player.bag[i]+n);}
std::string World::consume(bool water){
    if(player.hp<=0)return "Ты погиб. Мир продолжает жить. F9 — новая история.";
    if(!take(water?Water:Bread))return water?"Бурдюк пуст. Наполни его у пристани или колодца.":"Хлеб кончился. Его продаёт Мира на рынке.";
    if(water)player.thirst=std::max(0.f,player.thirst-.45f);else player.hunger=std::max(0.f,player.hunger-.4f);
    return water?"Прохладная вода возвращает ясность.":"Ржаной хлеб пахнет дымом и тмином.";
}
std::string World::rest(){
    if(player.hp<=0)return "Мёртвые не отдыхают.";if(player.cooldown>0)return "Подожди немного.";
    if(wanted>0)return "Пока тебя разыскивают, спокойно отдохнуть не выйдет.";
    int site=map.nearest(player.at,2);bool sheltered=distance(player.at,map.sites[site].at)<12||nearbyProject(player.at,Bedroll,5)>=0||nearbyProject(player.at,Shelter,5)>=0||nearbyProject(player.at,Campfire,5)>=0;
    for(size_t i=0;i<map.buildings.size()&&!sheltered;i++)sheltered=map.inside(player.at,int(i));
    if(!sheltered)return "Нужен очаг: зайди в трактир, часовню или жилой дом.";
    player.hp=std::min(100.f,player.hp+20);player.stamina=100;player.warmth=1;player.cooldown=3;
    return "Ты отогрелся у очага. +20 здоровья. Люди за стенами продолжают свои дела.";
}
std::string World::waitHours(int hours){
    if(hours<1||hours>8||wanted>0)return "Сейчас ждать небезопасно.";
    int enemy=nearestActor(player.at,10);if(enemy>=0&&actors[enemy].team)return "Сначала отойди от опасности.";
    timeOffset=(timeOffset+hours*1800)%43200;player.hunger=clamp(player.hunger+.06f*hours);player.thirst=clamp(player.thirst+.09f*hours);
    for(auto& a:actors){a.hunger=clamp(a.hunger+.05f*hours);a.thirst=clamp(a.thirst+.07f*hours);a.fatigue=clamp(a.fatigue+.03f*hours);a.activityUntil=0;}
    updateSandbox(float(hours));
    return "Прошло часов: "+std::to_string(hours)+". Распорядки и потребности изменились; жители добираются до новых целей.";
}
std::string World::trade(Item item,bool sell){
    if(item>=ItemCount||itemValue(item)<=0)return "Это нельзя продавать: вещь связана с чужой историей.";
    int n=dialogue.actor;if(n<0||n>=int(actors.size())||actors[n].hp<=0||distance(player.at,{x[n],y[n]})>10)return "Торговец уже далеко.";
    auto& a=actors[n];int price=std::max(1,int(std::lround(itemValue(item)*(sell?.64f:1.25f)*(1-a.affinity*.2f))));
    if(sell){if(!take(item))return "В сумке нет этой вещи.";if(a.coins<price){give(item);return "У собеседника не хватает монет.";}a.coins=uint16_t(a.coins-price);player.gold+=price;}
    else{if(player.gold<price)return "Не хватает монет.";player.gold-=price;a.coins=uint16_t(std::min(65000,int(a.coins)+price));give(item);}
    return std::string(sell?"Продано: ":"Куплено: ")+itemName(item)+". Монет: "+std::to_string(price)+".";
}
void World::openConversation(int i){
    if(i<0||i>=int(actors.size())||actors[i].hp<=0||distance(player.at,{x[i],y[i]})>9){dialogue={};return;}
    auto& a=actors[i];dialogue={};dialogue.actor=i;dialogue.open=true;dialogue.title=actorName(i)+" — "+(a.character>=0?characters()[a.character].roleText:roleName(a.role));
    if(a.team){dialogue.text=a.role==Wolf?"Зверь глухо рычит и следит за твоими руками.":"«Ещё шаг — и твой кошелёк станет легче. Вместе с головой».";dialogue.choices={{9,"Отступить"},{21,"Угрожать"}};return;}
    int stance=bank.socialStance(a.affinity,a.mind.fear*2-1,.1f,0,a.courage,a.character>=0?characters()[a.character].greed:.4f);
    static const char* tones[]={"Человек выслушивает тебя, не торопясь с ответом.","В голосе появляется тепло.","Он держится настороженно.","Собеседник предпочёл бы закончить разговор.","В этой беседе у каждого слова есть цена.","Он ищет глазами стражу.","Собеседник готов постоять за себя.","Страх заставляет его отступить.","Можно договориться о конкретном деле.","В улыбке читается замысел."};
    if(a.character>=0){auto& d=characters()[a.character];dialogue.text="«"+std::string(d.intro[hash(seed,uint32_t(i),tick/120)%3])+"»\n\n"+tones[stance]+"\n\n"+d.bio;if(a.affinity>.5f)dialogue.text+="\n\nТихо, только тебе: "+std::string(d.secret);}
    else dialogue.text=std::string("«")+(a.life==Sleep?"Ты видишь, который час? Дай поспать.":a.life==Work?"Если без дела — не отвлекай от работы.":a.life==Eat?"Сначала поем, а потом уже разговоры.":a.sanity<.3f?"Колокол звенит под землёй. Ты тоже его слышишь?":"Говори. Только не обещай того, чего не сделаешь.")+"»\n\n"+tones[stance];
    if(a.grudge==int32_t(actors.size()))dialogue.text+="\nОн помнит твою угрозу или удар. Слова не стёрли случившегося.";
    else if(a.rumor)dialogue.text+="\nО недавнем событии уже прошёл слух. Он знает не всё — только то, что услышал.";
    dialogue.choices={{1,"Попытаться подружиться"},{2,"Спросить о слухах"},{4,"Спросить о работе"},{5,"Подарить хлеб"},{3,"Торговать"},{20,"Другие действия"},{9,"Попрощаться"}};
    a.nextSocial=tick+240;a.loneliness=std::max(0.f,a.loneliness-.15f);
}
std::string World::talk(int i){openConversation(i);return dialogue.open?"Разговор с "+actorName(i):"Подойди ближе к живому собеседнику.";}
void World::choose(int code){
    if(code==9){dialogue.open=false;return;}int i=dialogue.actor;if(i<0||i>=int(actors.size())||actors[i].hp<=0){dialogue.open=false;return;}
    auto& a=actors[i];if(distance(player.at,{x[i],y[i]})>12){dialogue.text="Собеседник уже ушёл. Разговор окончен.";dialogue.choices={{9,"Закрыть"}};return;}
    std::string id=a.character>=0?characters()[a.character].id:"";
    auto response=[&](const std::string& msg){openConversation(i);dialogue.text=msg+"\n\n"+dialogue.text;note(msg);};
    if(code>=200&&code<200+ItemCount){auto msg=trade(Item(code-200));response(msg);return;}
    if(code>=300&&code<300+ItemCount){auto msg=trade(Item(code-300),true);response(msg);return;}
    if(code==1){int stance=bank.socialStance(a.affinity,a.mind.fear*2-1,.1f,2,a.courage,.4f);a.affinity=clamp(a.affinity+(stance==1?.08f:.025f),-1,1);a.stress=std::max(0.f,a.stress-.03f);response(stance==1?"Осторожное доверие становится чуть крепче.":"Он не спешит доверять, но запоминает попытку.");return;}
    if(code==2){response(a.rumor?"«Слышал о недавней тревоге. Кто-то видел своими глазами, остальные пересказывают». [Слух №"+std::to_string(a.rumor)+"]":"«Говорят, ночью у вересковой тропы разгружали телегу. Варрен ищет свидетелей. А Нель ищет чужие ключи». ");return;}
    if(code==5){if(!take(Bread)){response("Ты хотел подарить хлеб, но сумка пуста.");return;}a.food++;a.affinity=clamp(a.affinity+.18f,-1,1);a.hunger=std::max(0.f,a.hunger-.15f);response("Подарок не забыт, даже если о нём молчат.");return;}
    if(code==3){float greed=a.character>=0?characters()[a.character].greed:.4f;int stance=bank.socialStance(a.affinity,a.mind.fear*2-1,.1f,4,a.courage,greed);if(stance==3){response("«Не сегодня. Я не хочу с тобой торговать». Отношения имеют значение.");return;}dialogue.text="Монеты на стол. Покупка и продажа меняют настоящий инвентарь и кошелёк собеседника.";dialogue.choices.clear();for(Item item:{Bread,Water,Herb,Wine,Charm})dialogue.choices.push_back({200+item,"Купить: "+std::string(itemName(item))});for(Item item:{Ring,Shard,Berry})if(itemCount(item))dialogue.choices.push_back({300+item,"Продать: "+std::string(itemName(item))});dialogue.choices.push_back({9,"Закончить торговлю"});return;}
    if(code==20){dialogue.text="Не каждое решение можно исправить извинением.";dialogue.choices={{6,"Извиниться"},{7,"Помочь раненому травами"},{8,"Попытаться украсть"},{21,"Угрожать"},{22,"Напасть"},{9,"Уйти"}};return;}
    if(code==6){a.affinity=clamp(a.affinity+.05f,-1,1);a.stress=std::max(0.f,a.stress-.03f);response("«Прошлое не перепишешь. Но я слышу, что ты сказал». Розыск не исчез от одного извинения.");return;}
    if(code==7){if(a.hp>=95){response("Сейчас лечение не требуется.");return;}if(!take(Herb)){response("Нужна вересковая трава.");return;}a.hp=std::min(100.f,a.hp+30);a.affinity=clamp(a.affinity+.25f,-1,1);response("Ты перевязал рану и дал настой. Помощь останется в памяти.");return;}
    if(code==8){
        float perception=a.character>=0?characters()[a.character].perception:.55f;bool success=random01(hash(seed,uint32_t(i),tick,88))<clamp(.28f+player.stealth*.6f-perception*.35f,.08f,.86f);
        if(success&&a.pocketCount){int slot=0;for(int k=0;k<a.pocketCount;k++)if(a.pocket[k]==Purse)slot=k;Item loot=Item(a.pocket[slot]);for(int k=slot;k<a.pocketCount-1;k++)a.pocket[k]=a.pocket[k+1];a.pocketCount--;give(loot);if(id=="letta"&&loot==Purse&&player.quests[2]==1)player.quests[2]=2;bool caught=random01(hash(seed,tick,uint32_t(i),89))<perception*.6f;
            if(caught){a.affinity=clamp(a.affinity-.3f,-1,1);a.grudge=int32_t(actors.size());signal(player.at,1,true);}response("Ты взял: "+std::string(itemName(loot))+(caught?". Но тебя заметили.":". Пока это не заметили."));
        }else{a.affinity=clamp(a.affinity-.25f,-1,1);a.grudge=int32_t(actors.size());signal(player.at,1,true);response("«Руки убери. Я тебе не ящик с бесплатными вещами!»");}return;
    }
    if(code==21){a.affinity=clamp(a.affinity-.3f,-1,1);a.mind.fear=clamp(a.mind.fear+.3f);a.stress=clamp(a.stress+.25f);a.grudge=int32_t(actors.size());signal(player.at,1,true);int stance=bank.socialStance(a.affinity,.8f,.6f,7,a.courage,.4f);response(stance==6?"«Повтори. Громче, чтобы все слышали». Собеседник готов сопротивляться.":"«Не подходи ближе». Человек запомнил угрозу.");return;}
    if(code==22){if(distance(player.at,{x[i],y[i]})>4){response("Для удара ты слишком далеко. Подойди ближе.");return;}auto msg=attack();dialogue.open=false;note(msg);return;}
    if(code==4){
        dialogue.text="Обещание — тоже вещь. Здесь помнят, кому и что ты обещал.";dialogue.choices.clear();
        if(id=="mira"&&player.quests[0]==0)dialogue.choices.push_back({100,"Письмо через пепел: взять письмо"});
        if(id=="ilia"&&player.quests[0]==1&&itemCount(Letter))dialogue.choices.push_back({101,"Передать запечатанное письмо"});
        if(id=="varen"&&player.quests[1]==0)dialogue.choices.push_back({102,"Масляные следы: узнать о грузе"});
        if(id=="varen"&&player.quests[1]==2&&itemCount(Trace))dialogue.choices.push_back({103,"Передать найденную улику"});
        if(id=="nell"&&player.quests[2]==0)dialogue.choices.push_back({104,"Цена молчания: выслушать Неля"});
        if(id=="nell"&&player.quests[2]==2&&itemCount(Purse))dialogue.choices.push_back({105,"Отдать кошелёк Нелю"});
        if(id=="emery"&&player.quests[3]==0)dialogue.choices.push_back({106,"Тёплый настой: помочь Эмери"});
        if(id=="emery"&&player.quests[3]>=1&&player.quests[3]<3&&itemCount(Herb)>=3)dialogue.choices.push_back({107,"Передать Эмери три пучка вереска"});
        if(id=="varen"&&player.quests[2]>=1&&player.quests[2]<3)dialogue.choices.push_back({108,"Раскрыть дозору замысел Неля"});
        if(dialogue.choices.empty())dialogue.text+="\n\n«Спроси Миру на рынке, капитана Варрена, Неля в трактире или лекаря Эмери».";
        dialogue.choices.push_back({9,"Вернуться к своим делам"});return;
    }
    auto complete=[&](int quest,int gold,int faction,const std::string& text){player.quests[quest]=3;player.gold+=gold;player.renown+=8;player.reputation[faction]+=15;a.affinity=clamp(a.affinity+.2f,-1,1);response(text+" Награда: "+std::to_string(gold)+" монет.");};
    if(code==100&&id=="mira"&&!player.quests[0]){player.quests[0]=1;give(Letter);response("«Не вскрывай печать. Илия в часовне ждёт это письмо, но не знает, от кого». ");}
    else if(code==101&&id=="ilia"&&player.quests[0]==1&&take(Letter))complete(0,19,4,"«Я думала, этот голос сгорел вместе с архивом. Спасибо». ");
    else if(code==102&&id=="varen"&&!player.quests[1]){player.quests[1]=1;response("«Осмотри вересковую тропу. Мне нужна улика ночного груза, а не очередной слух». ");}
    else if(code==103&&id=="varen"&&player.quests[1]==2&&take(Trace))complete(1,23,1,"Варрен приобщает масляный след к делу.");
    else if(code==104&&id=="nell"&&!player.quests[2]){player.quests[2]=1;response("«У Летты на площади есть кошелёк. Принеси его — и не спрашивай, чей он был сначала». Дозор тоже может заинтересоваться этим предложением.");}
    else if(code==105&&id=="nell"&&player.quests[2]==2&&take(Purse))complete(2,27,2,"Нель прячет кошелёк. Ты выбрал сторону в этой истории.");
    else if(code==106&&id=="emery"&&!player.quests[3]){player.quests[3]=1;response("«Три пучка вереска из чащи. Иногда этого достаточно, чтобы кто-то дожил до утра». ");}
    else if(code==107&&id=="emery"&&player.quests[3]>0&&player.quests[3]<3&&take(Herb,3))complete(3,21,0,"Эмери ставит тёплый настой на огонь. Больные получат помощь.");
    else if(code==108&&id=="varen"&&player.quests[2]>0&&player.quests[2]<3){if(itemCount(Purse))take(Purse);complete(2,18,1,"Ты раскрыл предложение Неля дозору. Гильдия это запомнит.");player.reputation[2]-=15;}
}
std::string World::inspectSite(){
    int site=map.nearest(player.at);if(distance(player.at,map.sites[site].at)>15)return "Осмотрись ближе к дороге, дому или приметному месту.";
    std::string message=zoneLore(site);
    if(site==4){if(player.cooldown>0)return "Подожди, пока закончишь сбор.";give(Herb);player.cooldown=.7f;message+="\nТы собрал пучок вереска.";if(player.quests[3]==1&&itemCount(Herb)>=3)player.quests[3]=2;if(player.quests[1]==1){give(Trace);player.quests[1]=2;message+=" На тропе найдены отпечаток телеги и густая чёрная смазка: улика для Варрена.";}}
    if(site==5){give(Water,3);message+=" Ты наполнил три бурдюка.";}
    if(site==6&&player.cooldown<=0){player.cooldown=3;if(random01(hash(seed,tick,71))>.55f){give(Shard);message+=" В трещине колонны мерцает осколок реликвии.";}}
    note(message);return message;
}
std::string World::interact(){
    if(player.hp<=0)return "Эта история для тебя закончилась. F9 — новая жизнь.";
    if(nearbyProject(player.at,-1,4)>=0)return useProject();
    int npc=nearestActor(player.at,6);if(npc>=0)return talk(npc);
    int site=map.nearest(player.at);if(distance(player.at,map.sites[site].at)<12){if(site==0){signal(player.at,2);return "Колокол разносит тревогу по ближайшим улицам.";}return inspectSite();}
    return "Рядом никого. Подойди к жителю, двери или приметному месту. L — осмотреть локацию.";
}
std::string World::attack(){
    if(player.hp<=0||player.cooldown>0||player.stamina<10)return "Сейчас ударить не получится.";
    int i=nearestActor(player.at,4);player.cooldown=.45f;player.stamina-=10;if(i<0||!map.line(player.at,{x[i],y[i]}))return "Клинок рассекает воздух.";
    auto& a=actors[i];a.hp=std::max(0.f,a.hp-24);a.clue=player.at;a.lastSeen=tick;a.affinity=-1;a.grudge=int32_t(actors.size());a.stress=clamp(a.stress+.3f);bleed({x[i],y[i]},24,hash(seed,tick,uint32_t(i)));signal(player.at,a.hp>0?1.f:1.7f,a.team==0);
    std::string result=actorName(i)+(a.hp>0?" получил удар.":" погиб. Последствия останутся в мире.");note(result);return result;
}
void World::bleed(Vec at,float damage,uint32_t key){
    int amount=std::min(18,3+int(damage/2));for(int i=0;i<amount;i++){auto& b=blood[bloodCursor++%blood.size()];float angle=random01(hash(key,uint32_t(i)))*6.283185f,speed=.3f+random01(hash(key,uint32_t(i),1))*2;b.at=at;b.velocity={std::cos(angle)*speed,std::sin(angle)*speed};b.life=12+random01(hash(key,uint32_t(i),2))*35;b.radius=.05f+random01(hash(key,uint32_t(i),3))*.2f;b.stain=0;}
}
void World::updateEffects(){for(auto& b:blood)if(b.life>0){b.life=std::max(0.f,b.life-Dt);b.at.x+=b.velocity.x*Dt;b.at.y+=b.velocity.y*Dt;b.velocity.x*=.94f;b.velocity.y*=.94f;if(std::abs(b.velocity.x)+std::abs(b.velocity.y)<.02f)b.stain=1;}}

namespace {
struct SaveHeader {uint32_t magic=0x3242464f,version=3,count=0,seed=0,tick=0;int32_t time=0;float wanted=0;uint32_t nextEvent=0,flags=0;uint64_t bank=0,checksum=0;uint32_t actorBytes=sizeof(Actor),playerBytes=sizeof(Player);};
uint64_t checkBytes(const std::vector<uint8_t>& bytes){uint64_t h=1469598103934665603ull;for(auto b:bytes)h=(h^b)*1099511628211ull;return h;}
}
bool World::save(const std::string& path)const{
    static_assert(std::is_trivially_copyable<Actor>::value&&std::is_trivially_copyable<Player>::value,"Save schema must not contain pointers");
    std::vector<uint8_t> payload;payload.reserve(sizeof(Player)+actors.size()*(sizeof(Actor)+16)+sizeof(events));
    auto add=[&](const void* p,size_t n){auto first=static_cast<const uint8_t*>(p);payload.insert(payload.end(),first,first+n);};
    add(&player,sizeof(player));add(actors.data(),actors.size()*sizeof(Actor));for(auto p:{&x,&y,&vx,&vy})add(p->data(),p->size()*sizeof(float));add(events.data(),sizeof(events));add(resources.data(),sizeof(resources));add(projects.data(),sizeof(projects));add(stockpile.data(),sizeof(stockpile));
    SaveHeader header;header.count=uint32_t(actors.size());header.seed=seed;header.tick=tick;header.time=timeOffset;header.wanted=wanted;header.nextEvent=nextEvent;header.flags=uint32_t(mode)|(fullAI?4:0)|(lodEnabled?8:0);header.bank=bank.checksum();header.checksum=checkBytes(payload);
    std::ofstream file(path,std::ios::binary|std::ios::trunc);if(!file)return false;file.write(reinterpret_cast<const char*>(&header),sizeof(header));file.write(reinterpret_cast<const char*>(payload.data()),std::streamsize(payload.size()));return bool(file);
}
std::unique_ptr<World> World::load(const std::string& path,unsigned workers){
    std::ifstream file(path,std::ios::binary|std::ios::ate);if(!file)return {};auto bytes=file.tellg();if(bytes<std::streamoff(sizeof(SaveHeader))||bytes>64*1024*1024)return {};file.seekg(0);SaveHeader header;file.read(reinterpret_cast<char*>(&header),sizeof(header));
    if(header.magic!=0x3242464f||header.version!=3||header.count<1||header.count>131072||header.actorBytes!=sizeof(Actor)||header.playerBytes!=sizeof(Player))return {};
    size_t expected=sizeof(Player)+size_t(header.count)*(sizeof(Actor)+16)+sizeof(std::array<Event,64>)+sizeof(std::array<ResourceNode,384>)+sizeof(std::array<Project,64>)+sizeof(std::array<int,4>);if(bytes!=std::streamoff(sizeof(header)+expected))return {};
    std::vector<uint8_t> payload(expected);file.read(reinterpret_cast<char*>(payload.data()),std::streamsize(expected));if(!file||checkBytes(payload)!=header.checksum)return {};
    auto world=std::make_unique<World>(header.count,header.seed,workers);if(world->bank.checksum()!=header.bank)return {};size_t offset=0;
    auto get=[&](void* out,size_t n){std::memcpy(out,payload.data()+offset,n);offset+=n;};get(&world->player,sizeof(Player));get(world->actors.data(),header.count*sizeof(Actor));for(auto p:{&world->x,&world->y,&world->vx,&world->vy})get(p->data(),header.count*sizeof(float));get(world->events.data(),sizeof(world->events));get(world->resources.data(),sizeof(world->resources));get(world->projects.data(),sizeof(world->projects));get(world->stockpile.data(),sizeof(world->stockpile));
    world->tick=header.tick;world->timeOffset=header.time%43200;world->wanted=header.wanted;world->nextEvent=header.nextEvent;world->mode=Mode(header.flags&3);world->fullAI=(header.flags&4)!=0;world->lodEnabled=(header.flags&8)!=0;
    if(!world->valid()||!std::isfinite(world->wanted))return {};
    for(auto& r:world->resources)if(r.kind<Wood||r.kind>Seed||r.stock>4||((r.stock||r.renewAt)&&!world->map.clear(r.at.x,r.at.y)))return {};
    for(auto& p:world->projects)if(p.kind>=ProjectCount||p.active>1||p.watered>1||!std::isfinite(p.growth)||p.growth<0||p.growth>4||(p.active&&!world->map.clear(p.at.x,p.at.y)))return {};
    for(auto n:world->stockpile)if(n<0||n>9999)return {};
    for(auto& a:world->actors)if(a.residence>=world->map.buildings.size()||a.role>Scholar||a.life>=LifeCount||a.workSite>=8||a.socialSite>=8||a.mind.action>=Actions||a.mind.focus>=4||a.pocketCount>4||a.character>=int(characters().size())||!std::isfinite(a.hunger)||!std::isfinite(a.thirst)||!std::isfinite(a.fatigue)||!std::isfinite(a.goal.x)||!std::isfinite(a.goal.y))return {};
    return world;
}
}
