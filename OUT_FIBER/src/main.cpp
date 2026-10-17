// Copyright (c) 2026 Алексей Савельев (Alex Merqury / AlexMerq).
// SPDX-License-Identifier: LicenseRef-OUT-Source-Review-1.0
#include "fiber.hpp"
#include <iostream>
#include <stdexcept>
#ifdef _WIN32
int fiberGui(int,char**);
#endif
int main(int argc,char** argv){
    try{
        fiber::BenchmarkOptions o;bool bench=false,test=false;
        for(int i=1;i<argc;i++){
            std::string arg=argv[i];auto value=[&](){if(i+1>=argc)throw std::invalid_argument("Missing option value: "+arg);return std::string(argv[++i]);};
            if(arg=="--bench")bench=true;else if(arg=="--selftest")test=true;
            else if(arg=="--agents")o.agents=std::stoul(value());else if(arg=="--workers")o.workers=unsigned(std::stoul(value()));
            else if(arg=="--ticks")o.ticks=std::stoi(value());else if(arg=="--warmup")o.warmup=std::stoi(value());
            else if(arg=="--seed")o.seed=uint32_t(std::stoul(value()));else if(arg=="--output")o.output=value();
            else if(arg=="--full-ai")o.fullAI=true;
            else if(arg=="--mode"){auto v=value();if(v=="strict")o.mode=fiber::Mode::Strict;else if(v=="overlap")o.mode=fiber::Mode::Overlap;else if(v=="exploratory")o.mode=fiber::Mode::Exploratory;else if(v=="utility")o.mode=fiber::Mode::Utility;else throw std::invalid_argument("Unknown mode");}
            else if(arg=="--help"){std::cout<<"OUT FIBER / Heather Reach native benchmark\nNo args: Windows graphical sandbox\n--selftest\n--bench --agents N --workers N --ticks N --warmup N --seed N\n--mode strict|overlap|exploratory|utility --full-ai --output report.json\n--capture image.png (actual graphical frame, Windows)\n";return 0;}
            else if(arg=="--capture"){value();}
            else throw std::invalid_argument("Unknown option: "+arg);
        }
        if(test)return fiber::selftest();if(bench)return fiber::benchmark(o);
#ifdef _WIN32
        return fiberGui(argc,argv);
#else
        std::cerr<<"Graphical sandbox uses Win32. Use --bench or --selftest here.\n";return 1;
#endif
    }catch(const std::exception& e){std::cerr<<"OUT FIBER: "<<e.what()<<"\n";return 1;}
}
