// Offline replay only. This executable cannot load the EC backend or write hardware.
#include "../quiet/policy.hpp"
#include <fstream>
#include <iostream>
#include <sstream>
#include <vector>
using namespace quiet;
int main(int argc,char**argv){
    if(argc!=2)return 2;std::ifstream input(argv[1]);if(!input)return 3;
    std::string line;std::getline(input,line);Policy quietPolicy;int legacy=0,lastLegacy=-1,lastQuiet=-1;
    int valid=0,invalid=0,legacyChanges=0,quietChanges=0,hot=0,emergencyMiss=0,gaps=0;
    int legacyStarts=0,quietStarts=0,legacyHighEntries=0,quietHighEntries=0;
    uint64_t previous=0;bool priorValid=false;double seconds=0,legacyTime[8]{},quietTime[8]{};
    while(std::getline(input,line)){
        std::stringstream row(line);std::string field;std::vector<std::string> parts;
        while(std::getline(row,field,','))parts.push_back(field);if(parts.size()<19)continue;
        uint64_t at;int temp;try{at=std::stoull(parts[1]);temp=std::stoi(parts[18]);}catch(...){continue;}
        bool good=temperatureByteValid(temp);
        if(previous&&at>previous&&at-previous<=10000&&priorValid&&good){double dt=(at-previous)/1000.0;seconds+=dt;if(lastLegacy>=0&&lastLegacy<=7)legacyTime[lastLegacy]+=dt;if(lastQuiet>=0&&lastQuiet<=7)quietTime[lastQuiet]+=dt;}
        else if(previous&&at>previous+10000)++gaps;
        previous=at;priorValid=good;
        if(!good){++invalid;quietPolicy.step(temp,at,false);legacy=0;lastLegacy=lastQuiet=-1;continue;}
        ++valid;legacy=originalStep(temp,legacy);auto d=quietPolicy.step(temp,at);
        if(lastLegacy>=0&&legacy!=lastLegacy)++legacyChanges;
        if(lastQuiet>=0&&d.fan!=lastQuiet)++quietChanges;
        if(lastLegacy==0&&legacy>0)++legacyStarts;
        if(lastQuiet==0&&d.fan>0)++quietStarts;
        if(lastLegacy>=0&&lastLegacy<4&&legacy>=4)++legacyHighEntries;
        if(lastQuiet>=0&&lastQuiet<4&&d.fan>=4)++quietHighEntries;
        if(temp>=90){++hot;if(d.fan!=7)++emergencyMiss;}
        lastLegacy=legacy;lastQuiet=d.fan;
    }
    auto times=[](double*v){std::cout<<'{';bool first=true;for(int level:{0,1,2,4,7}){if(!first)std::cout<<',';first=false;std::cout<<'"'<<level<<"\":"<<v[level];}std::cout<<'}';};
    std::cout<<"{\"kind\":\"offline EC0x78 replay, not acoustic or hardware acceptance\",\"valid_samples\":"<<valid<<",\"invalid_samples\":"<<invalid<<",\"covered_seconds\":"<<seconds<<",\"gaps\":"<<gaps<<",\"A_curve_transitions\":"<<legacyChanges<<",\"quiet_transitions\":"<<quietChanges<<",\"A_curve_starts\":"<<legacyStarts<<",\"quiet_starts\":"<<quietStarts<<",\"A_curve_high_entries\":"<<legacyHighEntries<<",\"quiet_high_entries\":"<<quietHighEntries<<",\"hot_samples\":"<<hot<<",\"quiet_emergency_misses\":"<<emergencyMiss<<",\"A_curve_level_seconds\":";times(legacyTime);std::cout<<",\"quiet_level_seconds\":";times(quietTime);std::cout<<"}\n";
    return emergencyMiss?1:0;
}
