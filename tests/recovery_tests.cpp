#include "../quiet/recovery.hpp"
#include <iostream>
using quiet::SmartRecovery;
int main(){int n=0;auto check=[&](bool v,const char*s){if(!v){std::cerr<<"FAIL "<<s<<'\n';exit(1);}++n;};using K=SmartRecovery::Kind;
 auto settle=[](SmartRecovery&r,uint64_t start){bool result=false;for(uint64_t t=start;t<=start+30000;t+=2000)result=r.observe(t,true);return result;};
 {SmartRecovery r;check(!r.pending()&&!r.observe(100000,true),"no intent cannot restore");check(r.arm(K::Sensor,100000)&&r.pending(),"sensor intent armed");for(uint64_t t=100000;t<130000;t+=2000)check(!r.observe(t,true),"stable interval cannot be shortened");check(r.observe(130000,true)&&!r.pending(),"restores only after full stable interval");}
 {SmartRecovery r;r.arm(K::Sensor,100000);for(uint64_t t=100000;t<=120000;t+=2000)r.observe(t,true);r.observe(122000,false);check(!r.observe(124000,true),"unsafe sample resets stability");for(uint64_t t=126000;t<154000;t+=2000)check(!r.observe(t,true),"new stable interval required");check(r.observe(154000,true),"restores after fresh continuous interval");}
 {SmartRecovery r;r.arm(K::Sensor,100000);r.observe(100000,true);r.observe(110000,true);check(!r.observe(140000,true),"long sampling gap is not stable time");check(settle(r,140000),"stable after gap");}
 {SmartRecovery r;for(int i=0;i<3;++i){uint64_t t=100000+i*60000;check(r.arm(K::Sensor,t)&&settle(r,t),"bounded sensor retries allowed");}check(!r.arm(K::Sensor,300000),"fourth attempt in hour blocked");r.cancel();check(!r.arm(K::Sensor,302000),"manual cancellation cannot reset hourly budget");check(r.arm(K::Sensor,3730000),"rolling budget expires");}
 {SmartRecovery r;check(r.arm(K::Write,100000)&&settle(r,100000),"one write verification retry");check(!r.arm(K::Write,160000),"persistent writes cannot loop");check(r.arm(K::Sensor,162000)&&settle(r,162000),"write budget separate from remaining sensor budget");check(r.arm(K::Write,3730000),"write retry available after full rolling hour");}
 {SmartRecovery r;r.arm(K::Sensor,100000);r.observe(110000,true);check(!r.observe(109000,true)&&!r.pending(),"clock reversal cancels recovery");check(!r.arm(K::Sensor,109000),"clock reversal cannot rearm before prior clock");}
 {SmartRecovery r;r.arm(K::Write,100000);r.cancel();check(!r.pending()&&!r.observe(200000,true),"explicit stop cancels queued restoration");check(r.arm(K::Write,202000)&&settle(r,202000),"cancelled attempt does not consume write retry");}
 std::cout<<"PASS "<<n<<" Smart recovery checks\n";
}
