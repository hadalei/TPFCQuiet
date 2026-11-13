#define QUIET_TESTING
#include "../quiet/engine.hpp"
#include <iostream>
using namespace quiet;
namespace quiet {
struct EngineTestAccess {
 static void tick(Engine&e,const Sample&s,uint64_t t){VendorSample v;v.valid=1;v.at=t;v.cpu=46;v.coreMax=65;v.gpu=58;e.testSample=&s;e.testVendor=&v;e.tick(t);e.testSample=nullptr;e.testVendor=nullptr;}
};
}
int main(){Config config;auto dir=exeDir()/L"recovery-regression-data";Engine e(config,dir/L"profile.ini",true,false,dir);uint64_t now=GetTickCount64();Sample s;s.valid=true;s.raw.fill(0);s.raw[0]=46;s.rpm={2967,2707};s.level={128,128};
 for(int i=0;i<3;++i)EngineTestAccess::tick(e,s,now+i*2000);e.submit({2,2});EngineTestAccess::tick(e,s,now+6000);if(e.snapshot().mode!=2)return 2;
 s.raw[0]=0;s.rpm[0]=0;s.level={1,1};EngineTestAccess::tick(e,s,now+8000);if(e.snapshot().mode!=0)return 3;
 s.raw[0]=47;s.rpm={2747,2512};s.level={128,128};for(int i=5;i<=30;++i){EngineTestAccess::tick(e,s,now+i*2000);auto state=e.snapshot();if(state.mode==2&&!state.fault){std::cout<<"PASS EC frame recovered and Smart restored after continuous valid samples\n";return 0;}}
 std::cerr<<"FAIL EC recovered but old fault latch keeps BIOS permanently\n";return 1;
}
