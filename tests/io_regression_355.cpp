#define QUIET_TESTING
#include "../quiet/backend.hpp"
#include "../quiet/vendor.hpp"
#include <iostream>
using namespace quiet;
namespace quiet {
struct BackendTestAccess {
 static inline int command=0,offset=0,phase=0,fan=0,badReads=0,badValue=0,temp=60,writes=0;
 static inline bool output=false;
 static UCHAR __stdcall rp(USHORT port){if(port==0x1604)return output?1:0;output=false;
  if(offset==0x31)return static_cast<UCHAR>(0xa0|fan);
  if(offset==0x2f){if(fan==1&&badReads){if(badReads>0)--badReads;return static_cast<UCHAR>(badValue);}return 2;}
  if(offset==0x84)return 0xc0;if(offset==0x85)return 0x0b;if(offset==0x78)return static_cast<UCHAR>(temp);return 60;
 }
 static void __stdcall wp(USHORT port,UCHAR val){if(port==0x1604){command=val;phase=0;return;}if(command==0x80){offset=val;output=true;}else if(command==0x81){if(!phase){offset=val;phase=1;}else{if(offset==0x31)fan=val&1;if(offset==0x2f)++writes;phase=0;}}}
 static void __stdcall cp(){}
 static void setup(Backend&b,int count,int value,int temperature=60){command=offset=phase=fan=writes=0;badReads=count;badValue=value;temp=temperature;output=false;b.opened=true;b.validatedPort=true;b.readPort=rp;b.writePort=wp;b.closePort=cp;b.lock=CreateMutexW(nullptr,FALSE,nullptr);}
};
}
int main(){int failed=0,total=0;auto check=[&](bool ok,const char*name){++total;if(!ok)++failed;std::cout<<(ok?"PASS ":"FAIL ")<<name<<'\n';};
 auto sample=[](Backend&b){
#ifdef LEGACY_354
 return b.sample();
#else
 return b.sample(2);
#endif
 };
 {Backend b;BackendTestAccess::setup(b,1,0);auto s=sample(b);check(s.valid&&s.level[0]==2&&s.level[1]==2,"isolated 2/0 mismatch is read-only confirmed");check(!BackendTestAccess::writes,"confirmation never writes fan control");}
 {Backend b;BackendTestAccess::setup(b,-1,0);auto s=sample(b);check(s.valid&&s.level[1]==0,"persistent mismatch is preserved for BIOS fallback");}
 {Backend b;BackendTestAccess::setup(b,1,129);auto s=sample(b);check(s.valid&&s.level[1]==129,"firmware high-bit takeover is never hidden by retry");}
 {Backend b;BackendTestAccess::setup(b,1,0,95);auto s=sample(b);check(s.raw[0]==95,"readback confirmation preserves critical temperature");}
 VendorSample valid;valid.at=1000;valid.valid=1;valid.cpu=60;valid.coreMax=70;valid.gpu=65;valid.sourceAgeMs=2000;VendorSample absent;
 auto merge=[](const VendorSample&a,const VendorSample&b,uint64_t t){
#ifdef LEGACY_354
 (void)a;(void)t;return b;
#else
 return mergeVendorSample(a,b,t);
#endif
 };
 auto held=merge(valid,absent,4000);check(vendorFresh(held,4000)&&held.at==1000&&held.sourceAgeMs==2000,"query failure retains original fresh timestamp and source age");
 auto expired=merge(held,absent,11001);check(!vendorFresh(expired,11001),"cached packet expires at original 12-second age boundary");
 auto newer=valid;newer.at=5000;newer.coreMax=95;check(merge(valid,newer,5000).coreMax==95,"fresh hot packet replaces prior cooler packet immediately");
 check(!vendorFresh(merge(valid,absent,999),999),"clock reversal never revives a cached packet");
 std::cout<<"RESULT "<<total-failed<<'/'<<total<<" passed\n";return failed?1:0;
}
