#pragma once
#include "platform.hpp"
#include <rpc.h>
#include <wbemidl.h>
#include <wintrust.h>
#include <softpub.h>
#include <aclapi.h>
#include <roapi.h>
#include <windows.data.json.h>
#include <wrl.h>
#include <wrl/wrappers/corewrappers.h>
#include <set>
namespace quiet {
// Fixed, private child-process packet. No filenames or commands cross this pipe.
struct VendorSample {
    uint32_t magic=0x56504654,size=sizeof(VendorSample);
    uint64_t at=0;
    int cpu=-1,coreMax=-1,gpu=-1,acpi=-1,battery=-1,storage=-1;
    int valid=0,error=ERROR_NOT_READY;
    int sourceAgeMs=0;uint32_t producerPid=0;
};
inline bool vendorFresh(const VendorSample&s,uint64_t now){return s.magic==0x56504654&&s.size==sizeof(s)&&s.valid==1&&s.at&&s.sourceAgeMs>=0&&s.sourceAgeMs<=12000&&now>=s.at&&now-s.at<=12000-static_cast<uint64_t>(s.sourceAgeMs)&&temperatureByteValid(s.cpu)&&temperatureByteValid(s.coreMax)&&temperatureByteValid(s.gpu);}
// A failed query does not erase a still-fresh packet. Its original timestamp
// and source age remain unchanged, so the existing 12-second limit still applies.
inline VendorSample mergeVendorSample(const VendorSample&last,const VendorSample&incoming,uint64_t now){
    if(vendorFresh(incoming,now))return incoming;
    return vendorFresh(last,now)?last:incoming;
}
namespace vendor_detail {
using Microsoft::WRL::ComPtr;
using Microsoft::WRL::Wrappers::HString;
using Microsoft::WRL::Wrappers::HStringReference;
using namespace ABI::Windows::Data::Json;
inline bool signedFile(const fs::path&p){
    WINTRUST_FILE_INFO file{sizeof(file)};file.pcwszFilePath=p.c_str();
    WINTRUST_DATA data{sizeof(data)};data.dwUIChoice=WTD_UI_NONE;data.fdwRevocationChecks=WTD_REVOKE_NONE;data.dwUnionChoice=WTD_CHOICE_FILE;data.pFile=&file;data.dwStateAction=WTD_STATEACTION_VERIFY;data.dwProvFlags=WTD_CACHE_ONLY_URL_RETRIEVAL;
    GUID action=WINTRUST_ACTION_GENERIC_VERIFY_V2;LONG result=WinVerifyTrust(nullptr,&action,&data);data.dwStateAction=WTD_STATEACTION_CLOSE;WinVerifyTrust(nullptr,&action,&data);return result==ERROR_SUCCESS;
}
inline bool protectedLocation(const fs::path&p,const fs::path&anchor){
    BYTE installer[SECURITY_MAX_SID_SIZE];DWORD installerBytes=sizeof(installer),domainChars=128;wchar_t domain[128];SID_NAME_USE use{};
    bool haveInstaller=LookupAccountNameW(nullptr,L"NT SERVICE\\TrustedInstaller",installer,&installerBytes,domain,&domainChars,&use)!=FALSE;
    auto trusted=[&](PSID sid){return sid&&(IsWellKnownSid(sid,WinLocalSystemSid)||IsWellKnownSid(sid,WinBuiltinAdministratorsSid)||(haveInstaller&&EqualSid(sid,installer)));};
    for(auto current=p;!current.empty();current=current.parent_path()){
        DWORD attributes=GetFileAttributesW(current.c_str());if(attributes==INVALID_FILE_ATTRIBUTES||(attributes&FILE_ATTRIBUTE_REPARSE_POINT))return false;
        PSECURITY_DESCRIPTOR security=nullptr;PSID owner=nullptr;PACL acl=nullptr;
        if(GetNamedSecurityInfoW(const_cast<wchar_t*>(current.c_str()),SE_FILE_OBJECT,OWNER_SECURITY_INFORMATION|DACL_SECURITY_INFORMATION,&owner,nullptr,&acl,nullptr,&security)!=ERROR_SUCCESS)return false;
        bool ok=trusted(owner)&&acl;
        if(ok)for(DWORD i=0;i<acl->AceCount;++i){void*entry=nullptr;if(!GetAce(acl,i,&entry)){ok=false;break;}auto header=static_cast<ACE_HEADER*>(entry);if((header->AceFlags&INHERIT_ONLY_ACE)||header->AceType==ACCESS_DENIED_ACE_TYPE)continue;if(header->AceType!=ACCESS_ALLOWED_ACE_TYPE){ok=false;break;}auto ace=static_cast<ACCESS_ALLOWED_ACE*>(entry);PSID sid=&ace->SidStart;constexpr DWORD writes=GENERIC_WRITE|GENERIC_ALL|FILE_WRITE_DATA|FILE_APPEND_DATA|FILE_WRITE_EA|FILE_WRITE_ATTRIBUTES|FILE_DELETE_CHILD|DELETE|WRITE_DAC|WRITE_OWNER;if((ace->Mask&writes)&&!trusted(sid)){ok=false;break;}}
        LocalFree(security);if(!ok)return false;if(!_wcsicmp(current.c_str(),anchor.c_str()))return true;
    }return false;
}
inline std::wstring string(IJsonObject*object,const wchar_t*key){HString value;if(!object||FAILED(object->GetNamedString(HStringReference(key).Get(),value.GetAddressOf())))return {};UINT32 n=0;auto p=WindowsGetStringRawBuffer(value.Get(),&n);return std::wstring(p,n);}
inline int temperature(const std::wstring&s){if(s.empty())return -1;wchar_t*end=nullptr;errno=0;long n=wcstol(s.c_str(),&end,10);return errno||*end||n<1||n>127?-1:static_cast<int>(n);}
inline ComPtr<IJsonObject> parse(const std::wstring&s){ComPtr<IJsonObjectStatics> factory;ComPtr<IJsonObject> object;if(s.empty()||s.size()>2*1024*1024)return object;if(FAILED(RoGetActivationFactory(HStringReference(RuntimeClass_Windows_Data_Json_JsonObject).Get(),IID_PPV_ARGS(&factory))))return object;if(FAILED(factory->Parse(HStringReference(s.c_str()).Get(),&object)))object.Reset();return object;}
inline int length(IJsonArray*array,int maximum){ComPtr<ABI::Windows::Foundation::Collections::IVector<IJsonValue*>> vector;if(!array||FAILED(array->QueryInterface(IID_PPV_ARGS(&vector))))return -1;UINT32 size=0;if(FAILED(vector->get_Size(&size))||size>static_cast<UINT32>(maximum))return -1;return static_cast<int>(size);}
inline uint64_t utcTicks(){FILETIME f;GetSystemTimeAsFileTime(&f);ULARGE_INTEGER u;u.LowPart=f.dwLowDateTime;u.HighPart=f.dwHighDateTime;return u.QuadPart;}
inline bool recentTime(const std::wstring&s,uint64_t now,int*ageMs=nullptr){
    wchar_t day[4]{},month[4]{},zone[4]{};unsigned date=0,year=0,hour=0,minute=0,second=0;
    if(swscanf_s(s.c_str(),L"%3ls, %u %3ls %u %u:%u:%u %3ls",day,4,&date,month,4,&year,&hour,&minute,&second,zone,4)!=8||wcscmp(zone,L"UTC"))return false;
    const wchar_t*months[]={L"Jan",L"Feb",L"Mar",L"Apr",L"May",L"Jun",L"Jul",L"Aug",L"Sep",L"Oct",L"Nov",L"Dec"};int m=0;for(int i=0;i<12;++i)if(!wcscmp(month,months[i]))m=i+1;
    if(year<100)year+=2000;if(!m||year<2020||year>2200||date<1||date>31||hour>23||minute>59||second>59)return false;
    SYSTEMTIME t{};t.wYear=static_cast<WORD>(year);t.wMonth=static_cast<WORD>(m);t.wDay=static_cast<WORD>(date);t.wHour=static_cast<WORD>(hour);t.wMinute=static_cast<WORD>(minute);t.wSecond=static_cast<WORD>(second);FILETIME f;if(!SystemTimeToFileTime(&t,&f))return false;ULARGE_INTEGER value;value.LowPart=f.dwLowDateTime;value.HighPart=f.dwHighDateTime;
    bool recent=value.QuadPart<=now+20000000ULL&&now<=value.QuadPart+100000000ULL;if(recent&&ageMs)*ageMs=now>=value.QuadPart?static_cast<int>((now-value.QuadPart)/10000):0;return recent;
}
inline VendorSample decode(const std::string&response,uint64_t nowUtc=utcTicks()){
    VendorSample out;out.error=ERROR_INVALID_DATA;auto envelope=parse(wide(response));if(!envelope||string(envelope.Get(),L"Type")!=L"Success")return out;
    auto data=parse(string(envelope.Get(),L"response_payload"));if(!data)return out;ComPtr<IJsonArray> modules;if(FAILED(data->GetNamedArray(HStringReference(L"moduleList").Get(),&modules)))return out;
    bool cpuSeen=false,gpuSeen=false;int moduleCount=length(modules.Get(),32);if(moduleCount<1)return out;
    for(int i=0;i<moduleCount;++i){ComPtr<IJsonObject> module;if(FAILED(modules->GetObjectAt(static_cast<UINT32>(i),&module)))return out;auto name=string(module.Get(),L"moduleName");bool cpu=name==L"CPU",gpu=name==L"VIDEO_CARD";
        // Reject duplicate critical modules and timestamps before accepting any values.
        if((cpu&&cpuSeen)||(gpu&&gpuSeen))return VendorSample{};if(cpu)cpuSeen=true;if(gpu)gpuSeen=true;
        int age=0;if(!recentTime(string(module.Get(),L"time"),nowUtc,&age)){if(cpu||gpu)return out;continue;}if(cpu||gpu)out.sourceAgeMs=std::max(out.sourceAgeMs,age);
        ComPtr<IJsonArray> devices;if(FAILED(module->GetNamedArray(HStringReference(L"deviceList").Get(),&devices))){if(cpu||gpu)return out;continue;}
        int maximum=-1,core=-1;bool malformed=false;int deviceCount=length(devices.Get(),16);if(deviceCount<1){if(cpu||gpu)return out;continue;}
        for(int j=0;j<deviceCount;++j){ComPtr<IJsonObject> device;if(FAILED(devices->GetObjectAt(static_cast<UINT32>(j),&device))){malformed=true;break;}int temp=temperature(string(device.Get(),L"temperatureCelsius"));if(temp<0){if(cpu||gpu)malformed=true;continue;}maximum=std::max(maximum,temp);
            if(cpu){ComPtr<IJsonArray> cores;if(FAILED(device->GetNamedArray(HStringReference(L"temperatureCoresCelsius").Get(),&cores))){malformed=true;continue;}int count=length(cores.Get(),128);if(count<1){malformed=true;continue;}for(int k=0;k<count;++k){HString value;if(FAILED(cores->GetStringAt(static_cast<UINT32>(k),value.GetAddressOf()))){malformed=true;break;}UINT32 n=0;auto p=WindowsGetStringRawBuffer(value.Get(),&n);int t=temperature(std::wstring(p,n));if(t<0)malformed=true;else core=std::max(core,t);}}
        }
        if((cpu||gpu)&&(malformed||maximum<0))return out;
        if(cpu){out.cpu=maximum;out.coreMax=core;}else if(gpu)out.gpu=maximum;else if(name==L"MOTHERBOARD")out.acpi=maximum;else if(name==L"BATTERY")out.battery=maximum;else if(name==L"STORAGE")out.storage=maximum;
    }
    if(cpuSeen&&gpuSeen&&temperatureByteValid(out.cpu)&&temperatureByteValid(out.coreMax)&&temperatureByteValid(out.gpu)){out.valid=1;out.error=0;out.at=GetTickCount64();}return out;
}
// Discover endpoints only from the command line of a signed, installed Lenovo
// server. Public endpoint names alone are not evidence of a trusted producer.
struct TrustedServer {std::wstring endpoint;HANDLE process=nullptr;DWORD pid=0;int error=ERROR_NOT_FOUND;~TrustedServer(){if(process)CloseHandle(process);}bool alive()const{return process&&WaitForSingleObject(process,0)==WAIT_TIMEOUT;}};
// A WMI timeout is a pending enumeration, not proof that the process is absent.
// Keep the total lookup bounded; child process and sensor freshness budgets apply separately.
template<class Next,class Clock> inline std::pair<HRESULT,ULONG> nextWmiRow(Next next,Clock clock){
    uint64_t deadline=clock()+1500;
    do{uint64_t now=clock();if(now>=deadline)break;ULONG count=0;HRESULT hr=next(static_cast<DWORD>(std::min<uint64_t>(500,deadline-now)),count);if(hr!=WBEM_S_TIMEDOUT||count)return {hr,count};}while(clock()<deadline);
    return {WBEM_S_TIMEDOUT,0};
}
inline bool systemProcess(HANDLE process,const std::wstring&expected){
    wchar_t path[32768];DWORD chars=32768;if(!QueryFullProcessImageNameW(process,0,path,&chars)||_wcsicmp(path,expected.c_str()))return false;
    HANDLE token=nullptr;if(!OpenProcessToken(process,TOKEN_QUERY,&token))return false;DWORD bytes=0;GetTokenInformation(token,TokenUser,nullptr,0,&bytes);std::vector<BYTE> data(bytes);
    bool ok=bytes&&GetTokenInformation(token,TokenUser,data.data(),bytes,&bytes)&&IsWellKnownSid(reinterpret_cast<TOKEN_USER*>(data.data())->User.Sid,WinLocalSystemSid);CloseHandle(token);return ok;
}
inline bool trustedEndpoint(TrustedServer&server,DWORD preferredPid=0){
    ComPtr<IWbemLocator> locator;ComPtr<IWbemServices> services;ComPtr<IEnumWbemClassObject> results;
    if(FAILED(CoCreateInstance(CLSID_WbemLocator,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(&locator))))return false;
    BSTR ns=SysAllocString(L"ROOT\\CIMV2");HRESULT hr=locator->ConnectServer(ns,nullptr,nullptr,nullptr,0,nullptr,nullptr,&services);SysFreeString(ns);if(FAILED(hr))return false;
    if(FAILED(CoSetProxyBlanket(services.Get(),RPC_C_AUTHN_WINNT,RPC_C_AUTHZ_NONE,nullptr,RPC_C_AUTHN_LEVEL_CALL,RPC_C_IMP_LEVEL_IMPERSONATE,nullptr,EOAC_NONE)))return false;
    auto sql=L"SELECT ExecutablePath,CommandLine,ProcessId FROM Win32_Process WHERE Name='LdeApi.Server.exe'"+std::wstring(preferredPid?L" AND ProcessId="+std::to_wstring(preferredPid):L"");BSTR lang=SysAllocString(L"WQL"),query=SysAllocString(sql.c_str());hr=services->ExecQuery(lang,query,WBEM_FLAG_FORWARD_ONLY|WBEM_FLAG_RETURN_IMMEDIATELY,nullptr,&results);SysFreeString(lang);SysFreeString(query);if(FAILED(hr))return false;
    std::set<std::wstring> matches;const std::wstring prefix=L"lde-api-server-endpoint-";
    wchar_t common[MAX_PATH];SHGetFolderPathW(nullptr,CSIDL_COMMON_APPDATA,nullptr,SHGFP_TYPE_CURRENT,common);auto base=(fs::path(common)/L"Lenovo"/L"Vantage"/L"Addins"/L"LenovoHardwareScanAddin").wstring()+L"\\";
    for(int i=0;i<8;++i){ComPtr<IWbemClassObject> process;auto row=nextWmiRow([&](DWORD wait,ULONG&count){return results->Next(wait,1,process.ReleaseAndGetAddressOf(),&count);},[]{return GetTickCount64();});if(FAILED(row.first)||!row.second){if(row.first==WBEM_S_TIMEDOUT)server.error=ERROR_TIMEOUT;break;}VARIANT path,command,pid;VariantInit(&path);VariantInit(&command);VariantInit(&pid);process->Get(L"ExecutablePath",0,&path,nullptr,nullptr);process->Get(L"CommandLine",0,&command,nullptr,nullptr);process->Get(L"ProcessId",0,&pid,nullptr,nullptr);
        if(path.vt==VT_BSTR&&command.vt==VT_BSTR&&path.bstrVal&&command.bstrVal){std::wstring image=path.bstrVal,cmd=command.bstrVal;bool under=image.size()>base.size()&&!_wcsnicmp(image.c_str(),base.c_str(),base.size())&&fs::path(image).filename()==L"LdeApi.Server.exe";
            HANDLE handle=(pid.vt==VT_I4||pid.vt==VT_UI4)?OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION|SYNCHRONIZE,FALSE,pid.ulVal):nullptr;
            if(handle&&systemProcess(handle,image)&&WaitForSingleObject(handle,0)==WAIT_TIMEOUT&&under&&protectedLocation(fs::path(image),fs::path(common)/L"Lenovo"/L"Vantage")&&signedFile(fs::path(image))){auto pos=cmd.find(prefix);if(pos!=std::wstring::npos&&cmd.size()>=pos+prefix.size()+36){auto endpoint=cmd.substr(pos,prefix.size()+36);auto guid=endpoint.substr(prefix.size());UUID id;if(UuidFromStringW(reinterpret_cast<RPC_WSTR>(guid.data()),&id)==RPC_S_OK){matches.insert(endpoint);if(!server.process){server.process=handle;server.endpoint=endpoint;server.pid=pid.ulVal;handle=nullptr;}}}}
            if(handle)CloseHandle(handle);
        }VariantClear(&path);VariantClear(&command);VariantClear(&pid);
    }return matches.size()==1&&server.alive();
}
inline std::mutex responseLock;inline std::string response;inline int responseCount=0;
inline void __cdecl result(const unsigned char*data,int size){std::lock_guard<std::mutex>l(responseLock);++responseCount;if(data&&size>0&&size<=2*1024*1024)response.assign(reinterpret_cast<const char*>(data),static_cast<size_t>(size));}
inline int __cdecl progress(const unsigned char*,int){return 0;}
inline VendorSample query(DWORD preferredPid=0){
    VendorSample out;HRESULT init=RoInitialize(RO_INIT_MULTITHREADED);if(FAILED(init))return out;
    struct RoScope{~RoScope(){RoUninitialize();}} ro;
    TrustedServer server;if(!trustedEndpoint(server,preferredPid)){out.error=server.error;return out;}
    wchar_t pf[MAX_PATH];SHGetFolderPathW(nullptr,CSIDL_PROGRAM_FILES,nullptr,SHGFP_TYPE_CURRENT,pf);
    // Pin the installed client tested on this machine; never search user PATH.
    auto path=fs::path(pf)/L"Lenovo"/L"VantageService"/L"5.1.2609.9"/L"x86"/L"VantageRpcClient.dll";
    if(!protectedLocation(path,fs::path(pf))||!signedFile(path)){out.error=TRUST_E_NOSIGNATURE;return out;}HMODULE dll=LoadLibraryExW(path.c_str(),nullptr,LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR|LOAD_LIBRARY_SEARCH_SYSTEM32);if(!dll){out.error=GetLastError();return out;}
    using Make=int(__cdecl*)(const unsigned char*,int,void(__cdecl*)(const unsigned char*,int),int(__cdecl*)(const unsigned char*,int),const wchar_t*);auto call=reinterpret_cast<Make>(GetProcAddress(dll,"MakeRpcRequest"));
    if(!call){FreeLibrary(dll);out.error=ERROR_PROC_NOT_FOUND;return out;}
    const std::string request=R"({"method":"GetPerformanceData","payload":"{\"GenerateLogs\":false}"})";
    {std::lock_guard<std::mutex>l(responseLock);response.clear();responseCount=0;}
    if(!server.alive()){FreeLibrary(dll);out.error=ERROR_PROCESS_ABORTED;return out;}
    int code=call(reinterpret_cast<const unsigned char*>(request.data()),static_cast<int>(request.size()),result,progress,server.endpoint.c_str());
    {std::lock_guard<std::mutex>l(responseLock);if(!code&&responseCount==1&&server.alive())out=decode(response);else out.error=code?code:ERROR_INVALID_DATA;}
    if(out.valid)out.producerPid=server.pid;FreeLibrary(dll);return out;
}
inline std::wstring base64(const std::wstring&text){const auto bytes=utf8(text);const char*alphabet="ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";std::wstring out;for(size_t i=0;i<bytes.size();i+=3){unsigned a=static_cast<unsigned char>(bytes[i]),b=i+1<bytes.size()?static_cast<unsigned char>(bytes[i+1]):0,c=i+2<bytes.size()?static_cast<unsigned char>(bytes[i+2]):0;unsigned value=(a<<16)|(b<<8)|c;out+=alphabet[(value>>18)&63];out+=alphabet[(value>>12)&63];out+=i+1<bytes.size()?alphabet[(value>>6)&63]:L'=';out+=i+2<bytes.size()?alphabet[value&63]:L'=';}return out;}
// Only the installed SYSTEM service owns a private, signed Lenovo producer.
// The job contains only this child and its descendants; no existing Vantage
// process is terminated or reconfigured.
class OwnedTemperature {
#ifdef QUIET_TESTING
    friend struct OwnedTemperatureTestAccess;
#endif
    HANDLE process=nullptr,job=nullptr;DWORD pid=0;uint64_t nextLaunch=0;int failures=0;
public:
    ~OwnedTemperature(){stop();}
    void stop(){if(job){CloseHandle(job);job=nullptr;}if(process){WaitForSingleObject(process,2000);CloseHandle(process);process=nullptr;}pid=0;failures=0;}
    DWORD ensure(){
        if(process&&WaitForSingleObject(process,0)==WAIT_TIMEOUT)return pid;
        if(process)stop();auto now=GetTickCount64();if(now<nextLaunch)return 0;nextLaunch=now+60000;
        if(!systemProcess(GetCurrentProcess(),exePath()))return 0;
        wchar_t common[MAX_PATH],windows[MAX_PATH];if(FAILED(SHGetFolderPathW(nullptr,CSIDL_COMMON_APPDATA,nullptr,SHGFP_TYPE_CURRENT,common))||!GetWindowsDirectoryW(windows,MAX_PATH))return 0;
        auto anchor=fs::path(common)/L"Lenovo"/L"Vantage",directory=anchor/L"Addins"/L"LenovoHardwareScanAddin"/L"4.12.26070.12",server=directory/L"LdeApi.Server.exe";
        if(!protectedLocation(server,anchor)||!signedFile(server)||!protectedLocation(directory/L"lsdk.dll",anchor)||!signedFile(directory/L"lsdk.dll"))return 0;
        auto logs=dataDir(true)/L"vendor";std::error_code error;fs::create_directories(logs,error);if(error||!protectedLocation(logs,dataDir(true)))return 0;
        UUID id{};if(UuidCreate(&id)!=RPC_S_OK)return 0;RPC_WSTR raw=nullptr;if(UuidToStringW(&id,&raw)!=RPC_S_OK)return 0;std::wstring token(reinterpret_cast<wchar_t*>(raw));RpcStringFreeW(&raw);
        std::wstring command=L"\""+server.wstring()+L"\" lde-api-server-endpoint-"+token+L" Global\\TPFCQuiet.VendorReady."+token+L" "+base64(logs.wstring())+L" "+base64(fs::path(exePath()).stem().wstring())+L" "+std::to_wstring(GetCurrentProcessId());
        job=CreateJobObjectW(nullptr,nullptr);if(!job)return 0;JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{};limits.BasicLimitInformation.LimitFlags=JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
        if(!SetInformationJobObject(job,JobObjectExtendedLimitInformation,&limits,sizeof(limits))){stop();return 0;}
        STARTUPINFOW startup{sizeof(startup)};PROCESS_INFORMATION created{};auto working=fs::path(windows)/L"System32";
        if(!CreateProcessW(server.c_str(),command.data(),nullptr,nullptr,FALSE,CREATE_SUSPENDED|CREATE_NO_WINDOW,nullptr,working.c_str(),&startup,&created)){stop();return 0;}
        process=created.hProcess;pid=created.dwProcessId;
        if(!AssignProcessToJobObject(job,process)){TerminateProcess(process,ERROR_ACCESS_DENIED);CloseHandle(created.hThread);stop();return 0;}
        DWORD resumed=ResumeThread(created.hThread);CloseHandle(created.hThread);if(resumed==static_cast<DWORD>(-1)){stop();return 0;}return pid;
    }
    void observe(bool fresh){if(!pid)return;if(fresh)failures=0;else if(++failures>=8){stop();nextLaunch=GetTickCount64()+60000;}}
};
} // vendor_detail

class VendorReader {
    HANDLE stopped=nullptr;std::thread thread;std::mutex lock;VendorSample last;bool ownProducer=false;
    VendorSample child(DWORD producerPid=0){
        VendorSample out;SECURITY_ATTRIBUTES sa{sizeof(sa),nullptr,TRUE};HANDLE read=nullptr,write=nullptr;
        if(!CreatePipe(&read,&write,&sa,sizeof(VendorSample)*2))return out;SetHandleInformation(read,HANDLE_FLAG_INHERIT,0);
        struct QueryJob{HANDLE handle=nullptr;~QueryJob(){if(handle)CloseHandle(handle);}} job{CreateJobObjectW(nullptr,nullptr)};JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{};limits.BasicLimitInformation.LimitFlags=JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
        if(!job.handle||!SetInformationJobObject(job.handle,JobObjectExtendedLimitInformation,&limits,sizeof(limits))){CloseHandle(read);CloseHandle(write);return out;}
        STARTUPINFOEXW info{};info.StartupInfo.cb=sizeof(info);info.StartupInfo.dwFlags=STARTF_USESTDHANDLES;info.StartupInfo.hStdOutput=write;info.StartupInfo.hStdError=write;
        SIZE_T bytes=0;InitializeProcThreadAttributeList(nullptr,1,0,&bytes);std::vector<BYTE> attributes(bytes);info.lpAttributeList=reinterpret_cast<LPPROC_THREAD_ATTRIBUTE_LIST>(attributes.data());
        bool initialized=InitializeProcThreadAttributeList(info.lpAttributeList,1,0,&bytes)!=FALSE;
        bool updated=initialized&&UpdateProcThreadAttribute(info.lpAttributeList,0,PROC_THREAD_ATTRIBUTE_HANDLE_LIST,&write,sizeof(write),nullptr,nullptr);
        PROCESS_INFORMATION process{};std::wstring command=L"\""+exePath()+L"\" --vendor-packet"+(producerPid?L" --vendor-pid "+std::to_wstring(producerPid):L"");
        bool created=updated&&CreateProcessW(exePath().c_str(),command.data(),nullptr,nullptr,TRUE,CREATE_SUSPENDED|CREATE_NO_WINDOW|EXTENDED_STARTUPINFO_PRESENT,nullptr,exeDir().c_str(),&info.StartupInfo,&process);
        if(created&&(!AssignProcessToJobObject(job.handle,process.hProcess)||ResumeThread(process.hThread)==static_cast<DWORD>(-1))){TerminateProcess(process.hProcess,ERROR_ACCESS_DENIED);WaitForSingleObject(process.hProcess,1000);CloseHandle(process.hThread);CloseHandle(process.hProcess);created=false;}
        if(initialized)DeleteProcThreadAttributeList(info.lpAttributeList);CloseHandle(write);
        if(created){CloseHandle(process.hThread);HANDLE waits[]{stopped,process.hProcess};DWORD wait=WaitForMultipleObjects(2,waits,FALSE,4000);DWORD exit=1;
            if(wait==WAIT_OBJECT_0+1){GetExitCodeProcess(process.hProcess,&exit);DWORD available=0,n=0;if(PeekNamedPipe(read,nullptr,0,nullptr,&available,nullptr)&&available==sizeof(out)&&ReadFile(read,&out,sizeof(out),&n,nullptr)&&n==sizeof(out)&&out.magic==0x56504654&&out.size==sizeof(out)){
                    if(exit||!vendorFresh(out,GetTickCount64())){int error=out.valid==0&&out.error?out.error:ERROR_INVALID_DATA;out=VendorSample{};out.error=error;}
                }else out=VendorSample{};}
            else{TerminateProcess(process.hProcess,ERROR_TIMEOUT);WaitForSingleObject(process.hProcess,1000);out.error=ERROR_TIMEOUT;}
            CloseHandle(process.hProcess);
        }CloseHandle(read);return out;
    }
    void loop(){try{vendor_detail::OwnedTemperature producer;do{DWORD pid=ownProducer?producer.ensure():0;auto sample=ownProducer&&!pid?VendorSample{}:child(pid);if(ownProducer)producer.observe(vendorFresh(sample,GetTickCount64()));{std::lock_guard<std::mutex>l(lock);last=mergeVendorSample(last,sample,GetTickCount64());}}while(WaitForSingleObject(stopped,3000)==WAIT_TIMEOUT);}catch(...){std::lock_guard<std::mutex>l(lock);last=VendorSample{};}}
public:
    bool start(bool owned=false){if(thread.joinable())return true;ownProducer=owned;stopped=CreateEventW(nullptr,TRUE,FALSE,nullptr);if(!stopped)return false;thread=std::thread([this]{loop();});return true;}
    VendorSample snapshot(){std::lock_guard<std::mutex>l(lock);return last;}
    void stop(){if(stopped)SetEvent(stopped);if(thread.joinable())thread.join();if(stopped){CloseHandle(stopped);stopped=nullptr;}}
    ~VendorReader(){stop();}
};
} // quiet
