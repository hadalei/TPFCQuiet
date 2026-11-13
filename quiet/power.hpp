#pragma once
#include "power_task.hpp"
namespace quiet {
namespace power_detail {
inline bool equal(const GUID&a,const GUID&b){return !!IsEqualGUID(a,b);}
struct Record {DWORD magic=0x51505735,version=4;GUID original{},limited{},session{};DWORD targets[6]{},mask=0,owner=0;FILETIME created{};DWORD originals[6]{};};
inline fs::path journal(){return dataDir()/L"power-recovery.bin";}
struct Lock {
    HANDLE h=nullptr;bool owned=false;
    Lock(){auto name=L"Local\\TPFCQuiet.Power."+userSid();h=CreateMutexW(nullptr,FALSE,name.c_str());if(h){DWORD r=WaitForSingleObject(h,5000);owned=r==WAIT_OBJECT_0||r==WAIT_ABANDONED;}}
    ~Lock(){if(owned)ReleaseMutex(h);if(h)CloseHandle(h);}
};
// All file opens reject links. The journal is private to this Windows user.
inline DWORD readRecord(Record&r){
    HANDLE f=CreateFileW(journal().c_str(),GENERIC_READ,FILE_SHARE_READ,nullptr,OPEN_EXISTING,FILE_FLAG_OPEN_REPARSE_POINT,nullptr);
    if(f==INVALID_HANDLE_VALUE)return GetLastError();BY_HANDLE_FILE_INFORMATION i{};DWORD n=0;
    bool ok=GetFileInformationByHandle(f,&i)&&i.nNumberOfLinks==1&&!(i.dwFileAttributes&(FILE_ATTRIBUTE_REPARSE_POINT|FILE_ATTRIBUTE_DIRECTORY))&&i.nFileSizeHigh==0&&(i.nFileSizeLow==sizeof(r)||i.nFileSizeLow==96)&&ReadFile(f,&r,i.nFileSizeLow,&n,nullptr)&&n==i.nFileSizeLow;CloseHandle(f);
    return ok&&r.magic==0x51505735&&((r.version==4&&n==sizeof(r))||(r.version==3&&n==96))&&!equal(r.original,GUID_NULL)&&!equal(r.limited,GUID_NULL)&&!equal(r.original,r.limited)?ERROR_SUCCESS:ERROR_INVALID_DATA;
}
inline DWORD writeRecord(const Record&r){
    auto path=journal();auto tmp=path;tmp+=L"."+std::to_wstring(GetCurrentProcessId())+L"."+std::to_wstring(GetTickCount64())+L".new";
    PSECURITY_DESCRIPTOR sd=nullptr;auto acl=L"D:P(A;;FA;;;SY)(A;;FA;;;BA)(A;;FA;;;"+userSid()+L")";
    if(!ConvertStringSecurityDescriptorToSecurityDescriptorW(acl.c_str(),SDDL_REVISION_1,&sd,nullptr))return GetLastError();SECURITY_ATTRIBUTES sa{sizeof(sa),sd,FALSE};
    HANDLE f=CreateFileW(tmp.c_str(),GENERIC_WRITE|FILE_READ_ATTRIBUTES,0,&sa,CREATE_NEW,FILE_ATTRIBUTE_NORMAL|FILE_FLAG_OPEN_REPARSE_POINT,nullptr);LocalFree(sd);
    if(f==INVALID_HANDLE_VALUE)return GetLastError();DWORD n=0;bool ok=WriteFile(f,&r,sizeof(r),&n,nullptr)&&n==sizeof(r)&&FlushFileBuffers(f);DWORD e=ok?0:GetLastError();CloseHandle(f);
    if(ok&&!MoveFileExW(tmp.c_str(),path.c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH)){e=GetLastError();ok=false;}if(!ok)DeleteFileW(tmp.c_str());return ok?0:(e?e:ERROR_WRITE_FAULT);
}
struct Api {
    virtual ~Api()=default;
    virtual DWORD active(GUID&)=0;
    virtual DWORD set(const GUID&)=0;virtual DWORD remove(const GUID&)=0;
    virtual DWORD read(const GUID&,const GUID&,bool,DWORD&)=0;
    virtual DWORD write(const GUID&,const GUID&,bool,DWORD)=0;
    virtual DWORD load(Record&)=0;virtual DWORD save(const Record&)=0;virtual DWORD clear()=0;
    virtual void settle(){}
};
inline const GUID* const limits[]={&GUID_PROCESSOR_FREQUENCY_LIMIT,&GUID_PROCESSOR_FREQUENCY_LIMIT_1,&GUID_PROCESSOR_FREQUENCY_LIMIT_2};
class Native:public Api {
public:
    void settle()override{Sleep(1500);}
    DWORD active(GUID&g)override{GUID*p=nullptr;DWORD e=PowerGetActiveScheme(nullptr,&p);if(!e){g=*p;LocalFree(p);}return e;}
#ifdef QUIET_POWER_DIAGNOSTIC
    DWORD duplicate(const GUID&g,GUID&out){GUID*p=nullptr;DWORD e=PowerDuplicateScheme(nullptr,&g,&p);if(!e){out=*p;LocalFree(p);wchar_t name[]=L"TPFC Quiet diagnostic temporary scheme";PowerWriteFriendlyName(nullptr,&out,nullptr,nullptr,reinterpret_cast<BYTE*>(name),sizeof(name));}return e;}
#endif
    DWORD set(const GUID&g)override{return PowerSetActiveScheme(nullptr,&g);}
    DWORD remove(const GUID&g)override{DWORD e=PowerDeleteScheme(nullptr,&g);return e==ERROR_FILE_NOT_FOUND?0:e;}
    DWORD read(const GUID&g,const GUID&s,bool ac,DWORD&v)override{return ac?PowerReadACValueIndex(nullptr,&g,&GUID_PROCESSOR_SETTINGS_SUBGROUP,&s,&v):PowerReadDCValueIndex(nullptr,&g,&GUID_PROCESSOR_SETTINGS_SUBGROUP,&s,&v);}
    DWORD write(const GUID&g,const GUID&s,bool ac,DWORD v)override{return ac?PowerWriteACValueIndex(nullptr,&g,&GUID_PROCESSOR_SETTINGS_SUBGROUP,&s,v):PowerWriteDCValueIndex(nullptr,&g,&GUID_PROCESSOR_SETTINGS_SUBGROUP,&s,v);}
    DWORD load(Record&r)override{return readRecord(r);}DWORD save(const Record&r)override{return writeRecord(r);}
    DWORD clear()override{return DeleteFileW(journal().c_str())?0:GetLastError();}

};
// Recover a restriction left by an older version; never activate a new limit.
inline DWORD restore(Api&a,const GUID*session=nullptr){
    Record r;DWORD e=a.load(r);if(e==ERROR_FILE_NOT_FOUND)return 0;if(e)return e;if(session&&!equal(*session,r.session))return 0;
    GUID active{};if((e=a.active(active)))return e;
    if(equal(active,r.limited)){if(r.version>=4){for(int j=0;j<6;++j)if(r.mask&(1u<<j)){DWORD v=0;if((e=a.read(r.original,*limits[j/2],j%2==0,v)))return e;if(v||j>=4)r.originals[j]=v;}r.mask|=0x80000000u;if((e=a.save(r)))return e;}if((e=a.set(r.original)))return e;if((e=a.active(active)))return e;if(!equal(active,r.original))return ERROR_INVALID_STATE;}
    if(equal(active,r.original)&&(r.mask&0x80000000u)){
        // Windows may rewrite pre-existing frequency settings when changing
        // schemes. Reconcile only our saved processor settings on restoration.
        bool custom=false;for(int j=0;j<6;++j)if((r.mask&(1u<<j))&&r.originals[j])custom=true;
        if(r.version>=4&&custom){a.settle();if((e=a.active(active)))return e;if(!equal(active,r.original))return ERROR_CANCELLED;
            bool changed=false,recordChanged=false;for(int j=0;j<6;++j)if(r.mask&(1u<<j)){DWORD v=0;if((e=a.read(r.original,*limits[j/2],j%2==0,v)))return e;if(v!=r.originals[j]){if(j<4&&!v&&r.originals[j]){if((e=a.write(r.original,*limits[j/2],j%2==0,r.originals[j])))return e;changed=true;}else{r.originals[j]=v;recordChanged=true;}}}if(recordChanged&&(e=a.save(r)))return e;
            if(changed){if((e=a.set(r.original)))return e;a.settle();if((e=a.active(active)))return e;if(!equal(active,r.original))return ERROR_CANCELLED;}
            for(int j=0;j<6;++j)if(r.mask&(1u<<j)){DWORD v=0;if((e=a.read(r.original,*limits[j/2],j%2==0,v)))return e;if(v!=r.originals[j])return ERROR_INVALID_DATA;}
        }
    }
    // Preserve a different plan chosen by the user. Never delete the original.
    if((e=a.remove(r.limited)))return e;return a.clear();
}
// Compatibility only: unwind old 3.5.x restrictions before normal startup.
// With no journal this does not read or change any power scheme.
inline DWORD recoverLegacyPower(){Lock lock;Native api;return lock.owned?restore(api):ERROR_TIMEOUT;}
}
}
