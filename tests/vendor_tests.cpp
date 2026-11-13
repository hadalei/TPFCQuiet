#define QUIET_TESTING
#include "../quiet/display.hpp"
#include <iostream>
#include <functional>
using namespace quiet;
namespace quiet::vendor_detail {
struct OwnedTemperatureTestAccess {
 static bool idleDoesNotPostpone(){OwnedTemperature p;p.nextLaunch=1234;for(int i=0;i<32;++i)p.observe(false);return p.nextLaunch==1234&&p.failures==0;}
 static bool failedThenIdleKeepsDeadline(){OwnedTemperature p;p.pid=123;p.failures=7;p.observe(true);if(p.failures)return false;for(int i=0;i<8;++i)p.observe(false);auto deadline=p.nextLaunch;for(int i=0;i<32;++i)p.observe(false);return p.pid==0&&deadline>GetTickCount64()&&p.nextLaunch==deadline;}
};
}
static const char*when="Sat, 07 Nov 26 04:00:00 UTC";
static uint64_t fixedTime(){SYSTEMTIME t{};t.wYear=2026;t.wMonth=11;t.wDay=7;t.wHour=4;FILETIME f;SystemTimeToFileTime(&t,&f);ULARGE_INTEGER u;u.LowPart=f.dwLowDateTime;u.HighPart=f.dwHighDateTime;return u.QuadPart;}
static std::string module(const char*name,const std::string&devices,const char*time=when){return "{\"moduleName\":"+jsonQuoted(name)+",\"time\":"+jsonQuoted(time)+",\"deviceList\":"+devices+"}";}
static std::string cpu(const std::string&cores="[\"61\",\"78\"]",const char*time=when){return module("CPU","[{\"temperatureCelsius\":\"62\",\"temperatureCoresCelsius\":"+cores+"}]",time);}
static std::string gpu(const std::string&temp="\"70\"",const char*time=when){return module("VIDEO_CARD","[{\"temperatureCelsius\":"+temp+"}]",time);}
static std::string envelope(const std::string&modules){return "{\"Type\":\"Success\",\"response_payload\":"+jsonQuoted("{\"moduleList\":["+modules+"]}")+"}";}
static VendorSample decode(const std::string&s){return vendor_detail::decode(s,fixedTime());}
int main(){HRESULT initialized=RoInitialize(RO_INIT_MULTITHREADED);if(FAILED(initialized))return 2;int failed=0,total=0;
 auto test=[&](const char*name,const std::function<bool()>&f){++total;bool ok=false;try{ok=f();}catch(...){}std::cout<<(ok?"PASS ":"FAIL ")<<name<<'\n';if(!ok)++failed;};
 test("named readings retain highest CPU core",[]{auto s=decode(envelope(cpu()+","+gpu()));return s.valid&&s.cpu==62&&s.coreMax==78&&s.gpu==70;});
 test("missing GPU cannot supply protection",[]{return !decode(envelope(cpu())).valid;});
 test("missing CPU cannot supply protection",[]{return !decode(envelope(gpu())).valid;});
 test("missing CPU core array cannot supply protection",[]{return !decode(envelope(cpu("[]")+","+gpu())).valid;});
 test("malformed later core cannot hide hottest core",[]{return !decode(envelope(cpu("[\"62\",null,\"98\"]")+","+gpu())).valid;});
 test("malformed GPU device cannot be silently skipped",[]{return !decode(envelope(cpu()+","+module("VIDEO_CARD","[{\"temperatureCelsius\":\"70\"},null]"))).valid;});
 test("GPU sentinel rejected",[]{return !decode(envelope(cpu()+","+gpu("\"128\""))).valid;});
 test("GPU nonnumeric rejected",[]{return !decode(envelope(cpu()+","+gpu("\"unknown\""))).valid;});
 test("duplicate CPU module rejected",[]{return !decode(envelope(cpu()+","+cpu()+","+gpu())).valid;});
 test("stale module timestamp rejected",[]{return !decode(envelope(cpu()+","+gpu("\"70\"","Sat, 07 Nov 26 03:59:40 UTC"))).valid;});
 test("future module timestamp rejected",[]{return !decode(envelope(cpu()+","+gpu("\"70\"","Sat, 07 Nov 26 04:01:00 UTC"))).valid;});
 test("old producer age consumes freshness budget",[]{auto s=decode(envelope(cpu()+","+gpu("\"70\"","Sat, 07 Nov 26 03:59:51 UTC")));s.at=1000;return s.valid&&s.sourceAgeMs==9000&&vendorFresh(s,3900)&&!vendorFresh(s,4101);});
 test("too many cores rejected without truncation",[]{std::string cores="[";for(int i=0;i<129;++i){if(i)cores+=',';cores+="\"60\"";}cores+=']';return !decode(envelope(cpu(cores)+","+gpu())).valid;});
 test("ACPI label stays separate from CPU and GPU",[]{auto s=decode(envelope(cpu()+","+gpu()+","+module("MOTHERBOARD","[{\"temperatureCelsius\":\"110\"}]")));return s.valid&&s.acpi==110&&s.coreMax==78&&s.gpu==70;});
 test("packet expiry cannot leave old values usable",[]{VendorSample s;s.valid=1;s.at=1000;s.cpu=60;s.coreMax=75;s.gpu=65;return vendorFresh(s,13000)&&!vendorFresh(s,13001);});
 test("packet from future rejected",[]{VendorSample s;s.valid=1;s.at=1000;s.cpu=60;s.coreMax=75;s.gpu=65;return !vendorFresh(s,999);});
 test("WMI pending row retries instead of falsely reporting absent producer",[]{uint64_t clock=0;int calls=0;auto row=vendor_detail::nextWmiRow([&](DWORD wait,ULONG&count)->HRESULT{clock+=wait;if(++calls==1)return WBEM_S_TIMEDOUT;count=1;return S_OK;},[&]{return clock;});return row.first==S_OK&&row.second==1&&calls==2;});
 test("WMI lookup stays within total timeout budget",[]{uint64_t clock=0;int calls=0;auto row=vendor_detail::nextWmiRow([&](DWORD wait,ULONG&)->HRESULT{clock+=wait;++calls;return WBEM_S_TIMEDOUT;},[&]{return clock;});return row.first==WBEM_S_TIMEDOUT&&row.second==0&&clock==1500&&calls==3;});
 test("WMI completed empty enumeration never retries",[]{int calls=0;auto row=vendor_detail::nextWmiRow([&](DWORD,ULONG&)->HRESULT{++calls;return WBEM_S_FALSE;},[]{return uint64_t(0);});return row.first==WBEM_S_FALSE&&row.second==0&&calls==1;});
 test("WMI hard failure is not hidden by retry",[]{int calls=0;auto row=vendor_detail::nextWmiRow([&](DWORD,ULONG&)->HRESULT{++calls;return E_ACCESSDENIED;},[]{return uint64_t(0);});return row.first==E_ACCESSDENIED&&row.second==0&&calls==1;});
 test("packet size mismatch rejected",[]{VendorSample s;s.valid=1;s.at=1000;s.cpu=60;s.coreMax=75;s.gpu=65;--s.size;return !vendorFresh(s,1000);});
 test("display cache bridges short source failure without changing input",[]{DisplayCache cache;VendorSample s;s.valid=1;s.at=1000;s.cpu=51;s.coreMax=70;s.gpu=55;cache.observe(s,1000);VendorSample failed;auto shown=cache.reading(failed,6000);return shown&&shown->cpu==51&&!vendorFresh(failed,6000);});
 test("display retries cannot extend old source lifetime",[]{DisplayCache cache;VendorSample s;s.valid=1;s.at=1000;s.cpu=51;s.coreMax=70;s.gpu=55;cache.observe(s,1000);VendorSample failed;for(int i=0;i<15;++i)cache.observe(failed,2000+i*1000);return !cache.reading(failed,13001);});
 test("display cache recovers to newer live source",[]{DisplayCache cache;VendorSample s;s.valid=1;s.at=1000;s.cpu=51;s.coreMax=70;s.gpu=55;cache.observe(s,1000);s.at=8000;s.cpu=52;cache.observe(s,8000);VendorSample failed;auto shown=cache.reading(failed,8001);return shown&&shown->cpu==52;});
 test("display cache includes original producer age",[]{DisplayCache cache;VendorSample s;s.valid=1;s.at=1000;s.sourceAgeMs=9000;s.cpu=51;s.coreMax=70;s.gpu=55;cache.observe(s,1000);VendorSample failed;return cache.reading(failed,4000)&&!cache.reading(failed,4001);});
 test("JSON escaping roundtrips diagnostic paths",[]{std::string text="C:\\ProgramData\\TPFCQuiet\\diagnostic.csv\n\"quoted\"";auto object=vendor_detail::parse(wide("{\"reason\":"+jsonQuoted(text)+"}"));return object&&vendor_detail::string(object.Get(),L"reason")==wide(text);});
 test("producer arguments use UTF8 base64 without shell expansion",[]{return vendor_detail::base64(L"TPFCQuiet")==L"VFBGQ1F1aWV0"&&vendor_detail::base64(L"C:\\ProgramData\\TPFCQuiet\\vendor")==L"QzpcUHJvZ3JhbURhdGFcVFBGQ1F1aWV0XHZlbmRvcg==";});
 test("producer argument encoding preserves unicode path",[]{return vendor_detail::base64(L"温度")==L"5rip5bqm"&&vendor_detail::base64(L"").empty();});
 test("idle retry samples cannot postpone producer launch",[]{return vendor_detail::OwnedTemperatureTestAccess::idleDoesNotPostpone();});
 test("failure recovery deadline survives an idle backoff",[]{return vendor_detail::OwnedTemperatureTestAccess::failedThenIdleKeepsDeadline();});
 std::cout<<"RESULT "<<total-failed<<"/"<<total<<" passed\n";RoUninitialize();return failed?1:0;
}
