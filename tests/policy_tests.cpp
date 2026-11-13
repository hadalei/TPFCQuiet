#include "../quiet/policy.hpp"
#include <iostream>
#include <functional>
#include <vector>
using namespace quiet;
int main(){int failed=0,total=0;auto test=[&](const char*n,const std::function<bool()>&f){++total;bool ok=false;try{ok=f();}catch(...){ok=false;}std::cout<<(ok?"PASS ":"FAIL ")<<n<<'\n';if(!ok)++failed;};
    test("default curve valid",[]{return Curve{}.valid();});
    test("curve cannot raise high-temp trigger",[]{Curve c;c.enter[4]=95;return !c.valid();});
    test("curve requires ordered enter",[]{Curve c;c.enter[2]=50;return !c.valid();});
    test("curve rejects overlapping exit",[]{Curve c;c.leave[2]=71;return !c.valid();});
    test("quiet is default",[]{return Policy{}.profile==Profile::Quiet;});
    test("office cooling dip does not repeatedly stop and restart",[]{Policy p;if(p.step(62,100).fan!=1)return false;for(uint64_t t=2100;t<=60100;t+=2000)if(p.step(53,t).fan!=1)return false;for(uint64_t t=62100;t<=74100;t+=2000)p.step(49,t);return p.index==0;});
    test("fan selection preserves all unrelated EC bits",[]{for(int old=0;old<256;++old)for(int fan=0;fan<2;++fan){int value=fanSelectionValue(old,fan);if((value&0xfe)!=(old&0xfe)||(value&1)!=fan)return false;}return true;});
    test("warm initialization cannot turn fan off",[]{Policy p;return p.step(79,100).fan==2;});
    test("hot initialization immediately requests 7",[]{Policy p;return p.step(91,100).fan==7;});
    test("quiet ignores ordinary single spike",[]{Policy p;p.step(58,100);p.step(72,2100);auto d=p.step(58,4100);return d.fan==0;});
    test("persistent warmth eventually increases",[]{Policy p;p.step(58,100);for(uint64_t t=2100;t<=16100;t+=2000)p.step(74,t);return p.index==2;});
    test("high temperature bypasses pending confirmation",[]{Policy p;p.step(49,100);p.step(70,2100);return p.step(90,2200).fan==7;});
    test("cooling hold cannot block emergency",[]{Policy p;p.step(90,100);p.step(50,2100);return p.step(95,2200).fan==7;});
    test("high temperature bypasses quiet median",[]{Policy p;p.step(40,100);p.step(40,2100);auto d=p.step(90,4100);return d.fan==7&&d.filtered==40;});
    test("two high samples request 4",[]{Policy p;p.step(60,100);p.step(82,2100);return p.step(82,4100).fan==4;});
    test("降温 exits high level after confirmed 85",[]{Policy p;p.step(90,100);for(uint64_t t=2100;t<=12100;t+=2000)p.step(84,t);return p.index==3;});
    test("no drop while above exit",[]{Policy p;p.step(90,100);for(uint64_t t=2100;t<=40100;t+=2000)p.step(86,t);return p.index==4;});
    test("steady lower temp eventually stops",[]{Policy p;p.step(90,100);for(uint64_t t=2100;t<=24100;t+=2000)p.step(50,t);return p.index==0;});
    test("cooling profile has minimum 1",[]{Policy p;p.profile=Profile::Cooling;return p.step(40,100).fan==1;});
    test("invalid zero hands to BIOS",[]{Policy p;return p.step(0,100).fan==128;});
    test("invalid 128 hands to BIOS",[]{Policy p;return p.step(128,100).fan==128;});
    test("invalid negative hands to BIOS",[]{Policy p;return p.step(-1,100).fan==128;});
    test("explicit invalid sample clears window",[]{Policy p;p.step(90,100);auto d=p.step(50,2100,false);return d.fan==128&&p.index==0;});
    test("clock reversal resets safe current target",[]{Policy p;p.step(90,10000);return p.step(80,10).fan==4;});
    test("fan2 wrong value must not pass",[]{return !twoFansMatch(7,7,1,true,true);});
    test("fan1 wrong value must not pass",[]{return !twoFansMatch(7,1,7,true,true);});
    test("fan read failure must not pass",[]{return !twoFansMatch(7,7,7,true,false);});
    test("both fans independently match",[]{return twoFansMatch(4,4,4,true,true);});
    test("original spike regression fixture",[]{int f=1;for(int t:{61,91,84,81})f=originalStep(t,f);return f==7;});
    test("original cool threshold fixture",[]{return originalStep(80,7)==4&&originalStep(81,7)==7;});
    test("sensor byte invalid sentinels",[]{return !temperatureByteValid(128)&&!temperatureByteValid(255)&&!temperatureByteValid(0);});
    test("sensor byte 90 retained",[]{return temperatureByteValid(90);});
    // Sweep every profile across temperature and time: no undocumented fan levels.
    test("all profiles emit only allowed fan levels",[]{for(int k=0;k<3;++k){Policy p;p.profile=static_cast<Profile>(k);uint64_t now=0;for(int round=0;round<10;++round)for(int t=1;t<128;++t){now+=2000;int f=p.step(round%2?t:128-t,now).fan;if(f!=0&&f!=1&&f!=2&&f!=4&&f!=7)return false;}}return true;});
    std::cout<<"RESULT "<<total-failed<<"/"<<total<<" passed\n";return failed?1:0;
}
