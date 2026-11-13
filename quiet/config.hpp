#pragma once
#include "platform.hpp"
namespace quiet {
struct Config {
    int schema=1, cpu=0, mask=1, period=2000, display=0,manualMinutes=10,manualExit=78;
    bool verified=false,autoSmart=false,closedLidWork=false;
    Profile profile=Profile::Quiet;
    Curve curve;
    std::wstring machine;
    std::array<int,12> offset{};
    bool valid()const{
        if(cpu<0||cpu>11||mask<1||mask>4095||!(mask&(1<<cpu))||period<2000||period>10000||display<0||display>2||manualMinutes<1||manualMinutes>10||manualExit<60||manualExit>78||static_cast<int>(profile)<0||static_cast<int>(profile)>2||!curve.valid())return false;
        for(int n:offset)if(n<-10||n>10)return false;
        return true;
    }
    bool certified()const{return verified&&!machine.empty()&&machine==machineId()&&valid();}
};
inline int iniInt(const fs::path&p,const wchar_t*key,int fallback){wchar_t b[128];GetPrivateProfileStringW(L"Quiet",key,L"",b,128,p.c_str());if(!*b)return fallback;wchar_t*end=nullptr;errno=0;long n=wcstol(b,&end,10);if(errno||*end||n<INT_MIN||n>INT_MAX) return INT_MIN;return static_cast<int>(n);}
inline Config loadConfig(const fs::path&p){
    Config c;c.cpu=iniInt(p,L"CpuSensor",0);c.mask=iniInt(p,L"ControlMask",1);c.period=iniInt(p,L"SampleMs",2000);c.display=iniInt(p,L"Display",0);
    c.verified=iniInt(p,L"Verified",0)==1;c.autoSmart=iniInt(p,L"AutoSmart",0)==1;c.closedLidWork=iniInt(p,L"ClosedLidWork",0)==1;c.profile=static_cast<Profile>(iniInt(p,L"Profile",0));
    c.manualMinutes=iniInt(p,L"ManualMinutes",10);c.manualExit=iniInt(p,L"ManualExit",78);
    wchar_t b[1024];GetPrivateProfileStringW(L"Quiet",L"Machine",L"",b,1024,p.c_str());c.machine=b;
    for(int i=0;i<12;++i){wchar_t k[32];swprintf_s(k,L"Offset%d",i);c.offset[i]=iniInt(p,k,0);}
    for(int i=1;i<5;++i){wchar_t k[32];swprintf_s(k,L"Enter%d",i);c.curve.enter[i]=iniInt(p,k,c.curve.enter[i]);swprintf_s(k,L"Leave%d",i);c.curve.leave[i]=iniInt(p,k,c.curve.leave[i]);}
    // 3.0.0's untouched, unverified default included diagnostic registers.
    // Never migrate a certified or explicitly model-bound configuration.
    if(!c.verified&&c.machine.empty()&&c.cpu==0&&c.mask==0xFEF)c.mask=1;
    if(!c.verified&&c.machine.empty()&&c.curve.enter==std::array<int,5>{0,60,70,80,90}&&c.curve.leave==std::array<int,5>{0,55,65,75,85})c.curve.leave[1]=50;
    if(!c.valid())throw std::runtime_error("Invalid configuration; hardware control remains disabled");
    return c;
}
inline bool saveConfig(const fs::path&p,const Config&c){
    if(!c.valid())return false;std::error_code ec;fs::create_directories(p.parent_path(),ec);
    auto temp=p;temp+=L".new";std::wstring text=L"[Quiet]\r\nSchema=1\r\n";
    auto add=[&](const wchar_t*k,int v){text+=std::wstring(k)+L"="+std::to_wstring(v)+L"\r\n";};
    add(L"CpuSensor",c.cpu);add(L"ControlMask",c.mask);add(L"SampleMs",c.period);add(L"Display",c.display);add(L"Verified",c.verified);add(L"AutoSmart",c.autoSmart);add(L"Profile",static_cast<int>(c.profile));add(L"ManualMinutes",c.manualMinutes);add(L"ManualExit",c.manualExit);text+=L"Machine="+c.machine+L"\r\n";
    add(L"ClosedLidWork",c.closedLidWork);
    for(int i=0;i<12;++i){std::wstring k=L"Offset"+std::to_wstring(i);add(k.c_str(),c.offset[i]);}
    for(int i=1;i<5;++i){std::wstring k=L"Enter"+std::to_wstring(i);add(k.c_str(),c.curve.enter[i]);k=L"Leave"+std::to_wstring(i);add(k.c_str(),c.curve.leave[i]);}
    HANDLE f=CreateFileW(temp.c_str(),GENERIC_WRITE|FILE_READ_ATTRIBUTES,0,nullptr,OPEN_ALWAYS,FILE_ATTRIBUTE_NORMAL|FILE_FLAG_OPEN_REPARSE_POINT,nullptr);if(f==INVALID_HANDLE_VALUE)return false;
    BY_HANDLE_FILE_INFORMATION info{};if(!GetFileInformationByHandle(f,&info)||info.nNumberOfLinks!=1||(info.dwFileAttributes&(FILE_ATTRIBUTE_REPARSE_POINT|FILE_ATTRIBUTE_DIRECTORY))||SetFilePointer(f,0,nullptr,FILE_BEGIN)==INVALID_SET_FILE_POINTER||!SetEndOfFile(f)){CloseHandle(f);return false;}
    WORD bom=0xFEFF;DWORD n=0;bool ok=WriteFile(f,&bom,2,&n,nullptr)&&n==2;DWORD size=static_cast<DWORD>(text.size()*2);ok=ok&&WriteFile(f,text.data(),size,&n,nullptr)&&n==size&&FlushFileBuffers(f);CloseHandle(f);
    return ok&&MoveFileExW(temp.c_str(),p.c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH);
}
}
