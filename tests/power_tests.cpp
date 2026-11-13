#include "../quiet/power.hpp"
#include <map>
#include <iostream>
using namespace quiet::power_detail;
static GUID guid(DWORD v){GUID g{};g.Data1=v;return g;}
struct Fake:Api {
 GUID current=guid(2);Record record{};bool saved=true,failSet=false,failRead=false,failSave=false,failRemove=false,failClear=false,normalizeOriginal=false,switchOnSettle=false;
 DWORD loadError=0;int writes=0,sets=0,removed=0;
 std::map<DWORD,std::array<DWORD,6>> plans{{1,{0,0,0,0,0,0}},{2,{3500,3500,3500,3500,3500,3500}},{9,{2400,2400,2400,2400,2400,2400}}};
 Fake(){record.original=guid(1);record.limited=guid(2);record.session=guid(7);record.mask=63;for(auto&v:record.targets)v=3500;}
 DWORD active(GUID&g)override{g=current;return 0;}
 DWORD set(const GUID&g)override{++sets;if(failSet)return ERROR_ACCESS_DENIED;current=g;if(normalizeOriginal&&g.Data1==1)for(int j=0;j<4;++j)plans[1][j]=0;return 0;}
 DWORD remove(const GUID&g)override{if(failRemove)return ERROR_ACCESS_DENIED;if(g.Data1==current.Data1||g.Data1==1)return ERROR_INVALID_STATE;plans.erase(g.Data1);++removed;return 0;}
 int index(const GUID&s,bool ac){for(int i=0;i<3;++i)if(equal(s,*limits[i]))return i*2+(ac?0:1);return -1;}
 DWORD read(const GUID&g,const GUID&s,bool ac,DWORD&v)override{if(failRead)return ERROR_READ_FAULT;v=plans[g.Data1][index(s,ac)];return 0;}
 DWORD write(const GUID&g,const GUID&s,bool ac,DWORD v)override{plans[g.Data1][index(s,ac)]=v;++writes;return 0;}
 DWORD load(Record&r)override{if(loadError)return loadError;if(!saved)return ERROR_FILE_NOT_FOUND;r=record;return 0;}
 DWORD save(const Record&r)override{if(failSave)return ERROR_DISK_FULL;record=r;saved=true;return 0;}
 DWORD clear()override{if(failClear)return ERROR_ACCESS_DENIED;saved=false;return 0;}
 void settle()override{if(switchOnSettle)current=guid(9);}
};
int main(){int n=0;auto check=[&](bool v,const char*name){if(!v){std::cerr<<"FAIL "<<name<<'\n';exit(1);}++n;};
 {Fake f;check(!restore(f),"old active clone restores");check(f.current.Data1==1&&!f.saved&&!f.plans.count(2),"original selected clone and journal removed");check(f.writes==0&&f.sets==1,"normal original no frequency writes");}
 {Fake f;f.saved=false;check(!restore(f)&&!f.writes&&!f.sets&&!f.removed,"normal startup without journal changes nothing");}
 {Fake f;f.loadError=ERROR_INVALID_DATA;check(restore(f)==ERROR_INVALID_DATA&&f.saved&&!f.sets&&!f.removed,"invalid record fails without mutation");}
 {Fake f;GUID other=guid(8);check(!restore(f,&other)&&f.current.Data1==2&&f.saved,"old session cannot unwind different owner");}
 {Fake f;f.failSet=true;check(restore(f)==ERROR_ACCESS_DENIED&&f.saved&&f.plans.count(2),"failed original selection retains recovery");f.failSet=false;check(!restore(f)&&f.current.Data1==1&&!f.saved,"restore retry succeeds");}
 {Fake f;f.failSave=true;check(restore(f)==ERROR_DISK_FULL&&!f.sets&&f.saved,"durable restoration marker required first");}
 {Fake f;f.failRead=true;check(restore(f)==ERROR_READ_FAULT&&!f.sets&&f.saved,"unreadable original retains journal");}
 {Fake f;f.current=guid(9);auto before=f.plans[9];check(!restore(f)&&f.current.Data1==9&&f.plans[9]==before&&!f.sets&&!f.writes,"preserve user's newly selected plan");check(!f.saved&&!f.plans.count(2)&&f.plans.count(1),"external selection cleanup never removes original");}
 {Fake f;f.current=guid(1);check(!restore(f)&&!f.sets&&!f.writes&&!f.saved,"already restored cleanup only");}
 {Fake f;f.failRemove=true;check(restore(f)==ERROR_ACCESS_DENIED&&f.current.Data1==1&&f.saved,"cleanup failure retains journal after normal performance restored");f.failRemove=false;check(!restore(f)&&!f.saved,"retry cleanup idempotent");}
 {Fake f;f.failClear=true;check(restore(f)==ERROR_ACCESS_DENIED&&f.current.Data1==1&&f.saved,"journal cleanup failure reported");f.failClear=false;check(!restore(f)&&!f.saved,"missing clone tolerated on cleanup retry");}
 {Fake f;f.record.version=3;f.record.mask=63;check(!restore(f)&&f.current.Data1==1&&!f.saved,"version3 recovery compatibility");}
 {Fake f;f.plans[1]={2400,1800,0,2000,4500,1700};auto baseline=f.plans[1];check(!restore(f)&&f.plans[1]==baseline&&!f.writes,"existing user's custom original preserved");}
 {Fake f;f.record.originals[0]=2400;f.record.originals[1]=1800;f.record.originals[2]=2000;f.record.originals[3]=1700;f.plans[1]={2400,1800,2000,1700,0,0};
  // Simulate original normalization only while selecting the original, then allow a reapply.
  struct Once:Fake{DWORD set(const GUID&g)override{bool first=sets==0;auto e=Fake::set(g);if(!e&&first&&g.Data1==1)for(int j=0;j<4;++j)plans[1][j]=0;return e;}} once;
  once.record=f.record;once.plans=f.plans;check(!restore(once)&&once.plans[1]==f.plans[1],"old snapshot survives OS zero normalization on original");check(once.writes==4&&!once.saved,"only old custom values repaired");}
 {Fake f;f.record.originals[0]=2400;f.plans[1][0]=1800;check(!restore(f)&&f.plans[1][0]==1800,"new lower nonzero original wins over older snapshot");}
 {Fake f;f.record.originals[0]=2400;f.record.mask|=0x80000000u;f.current=guid(1);f.switchOnSettle=true;check(restore(f)==ERROR_CANCELLED&&f.current.Data1==9&&f.saved,"scheme change during recovery is preserved and reported");}
 std::cout<<"PASS "<<n<<" legacy power recovery checks\n";
}
