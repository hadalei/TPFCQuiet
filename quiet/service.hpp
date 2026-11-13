#pragma once
#include "engine.hpp"
#include <aclapi.h>
namespace quiet {
constexpr wchar_t ServiceName[]=L"TPFCQuiet";
#ifdef QUIET_TESTING
constexpr wchar_t PipeName[]=L"\\\\.\\pipe\\TPFCQuiet.Test.State.v3";
#else
constexpr wchar_t PipeName[]=L"\\\\.\\pipe\\TPFCQuiet.State.v3";
#endif
struct Request {uint32_t magic=Magic,version=Protocol,size=sizeof(Request);int command=0,value=0;int enter[4]{60,70,80,90},leave[4]{50,65,75,85};int display=0,autoSmart=0,closedLidWork=0;};
inline bool administratorPipeClient(HANDLE pipe){
    if(!ImpersonateNamedPipeClient(pipe))return false;HANDLE token=nullptr;BOOL member=FALSE;SID_IDENTIFIER_AUTHORITY authority=SECURITY_NT_AUTHORITY;PSID sid=nullptr;
    bool ok=OpenThreadToken(GetCurrentThread(),TOKEN_QUERY,TRUE,&token)!=FALSE;
    if(ok)ok=AllocateAndInitializeSid(&authority,2,SECURITY_BUILTIN_DOMAIN_RID,DOMAIN_ALIAS_RID_ADMINS,0,0,0,0,0,0,&sid)!=FALSE;
    if(ok)ok=CheckTokenMembership(token,sid,&member)!=FALSE;if(sid)FreeSid(sid);if(token)CloseHandle(token);if(!RevertToSelf())std::terminate();return ok&&member;
}
inline bool pipeIo(HANDLE pipe,bool writing,void*buffer,DWORD size,DWORD&done,DWORD timeout){
    OVERLAPPED o{};o.hEvent=CreateEventW(nullptr,TRUE,FALSE,nullptr);if(!o.hEvent)return false;
    BOOL ok=writing?WriteFile(pipe,buffer,size,&done,&o):ReadFile(pipe,buffer,size,&done,&o);
    if(!ok&&GetLastError()==ERROR_IO_PENDING){if(WaitForSingleObject(o.hEvent,timeout)==WAIT_OBJECT_0)ok=GetOverlappedResult(pipe,&o,&done,FALSE);else{CancelIoEx(pipe,&o);WaitForSingleObject(o.hEvent,INFINITE);ok=FALSE;}}
    CloseHandle(o.hEvent);return ok!=FALSE&&done==size;
}
inline bool remote(const Request&r,State&state){
    HANDLE pipe=INVALID_HANDLE_VALUE;uint64_t until=GetTickCount64()+400;
    do{pipe=CreateFileW(PipeName,GENERIC_READ|GENERIC_WRITE,0,nullptr,OPEN_EXISTING,FILE_FLAG_OVERLAPPED|SECURITY_SQOS_PRESENT|SECURITY_IDENTIFICATION,nullptr);if(pipe!=INVALID_HANDLE_VALUE)break;DWORD err=GetLastError();if(err!=ERROR_PIPE_BUSY&&err!=ERROR_FILE_NOT_FOUND)return false;Sleep(15);}while(GetTickCount64()<until);
    if(pipe==INVALID_HANDLE_VALUE)return false;
    DWORD n=0;Request copy=r;bool ok=pipeIo(pipe,true,&copy,sizeof(copy),n,500)&&pipeIo(pipe,false,&state,sizeof(state),n,500);
    if(ok){uint32_t acknowledgement=Magic;pipeIo(pipe,true,&acknowledgement,sizeof(acknowledgement),n,500);}
    CloseHandle(pipe);return ok&&state.magic==Magic&&state.protocol==Protocol;
}
class PipeServer {
    Engine&engine;std::atomic<bool>running{false};std::thread thread;std::wstring sid;HANDLE firstPipe=INVALID_HANDLE_VALUE;
    HANDLE make(){PSECURITY_DESCRIPTOR sd=nullptr;std::wstring sddl=L"D:P(A;;GA;;;SY)(A;;GA;;;BA)(A;;GRGW;;;"+sid+L")";if(!ConvertStringSecurityDescriptorToSecurityDescriptorW(sddl.c_str(),SDDL_REVISION_1,&sd,nullptr))return INVALID_HANDLE_VALUE;
        SECURITY_ATTRIBUTES sa{sizeof(sa),sd,FALSE};HANDLE h=CreateNamedPipeW(PipeName,PIPE_ACCESS_DUPLEX|FILE_FLAG_OVERLAPPED|FILE_FLAG_FIRST_PIPE_INSTANCE,PIPE_TYPE_MESSAGE|PIPE_READMODE_MESSAGE|PIPE_WAIT|PIPE_REJECT_REMOTE_CLIENTS,1,65536,65536,0,&sa);LocalFree(sd);return h;}
    void loop(){
        HANDLE h=firstPipe;firstPipe=INVALID_HANDLE_VALUE;
        while(running&&h!=INVALID_HANDLE_VALUE){
            OVERLAPPED o{};o.hEvent=CreateEventW(nullptr,TRUE,FALSE,nullptr);BOOL connected=ConnectNamedPipe(h,&o);DWORD err=connected?ERROR_SUCCESS:GetLastError();
            if(!connected&&err==ERROR_IO_PENDING){while(running&&WaitForSingleObject(o.hEvent,100)==WAIT_TIMEOUT){}DWORD n=0;if(running)connected=GetOverlappedResult(h,&o,&n,FALSE);else{CancelIoEx(h,&o);WaitForSingleObject(o.hEvent,INFINITE);}}
            else if(err==ERROR_PIPE_CONNECTED)connected=TRUE;
            CloseHandle(o.hEvent);
            if(connected&&running){Request r;DWORD n=0;if(pipeIo(h,false,&r,sizeof(r),n,600)&&r.magic==Magic&&r.version==Protocol&&r.size==sizeof(r)){
                if((r.command>=1&&r.command<=5)||r.command==7||(r.command>=9&&r.command<=11))engine.submit({r.command,r.value});
                else if(r.command==8&&r.value>0&&administratorPipeClient(h)){Command c;c.kind=8;c.value=r.value;c.admin=true;engine.submit(c);}
                else if(r.command==6){Command c;c.kind=6;c.config.profile=static_cast<Profile>(r.value);c.config.display=r.display;c.config.autoSmart=r.autoSmart==1;c.config.closedLidWork=r.closedLidWork==1;for(int i=0;i<4;++i){c.config.curve.enter[i+1]=r.enter[i];c.config.curve.leave[i+1]=r.leave[i];}engine.submit(c);}
                auto s=engine.snapshot();if(pipeIo(h,true,&s,sizeof(s),n,600)){uint32_t acknowledgement=0;pipeIo(h,false,&acknowledgement,sizeof(acknowledgement),n,600);}
            }}
            DisconnectNamedPipe(h);
        }if(h!=INVALID_HANDLE_VALUE)CloseHandle(h);
    }
public:
    PipeServer(Engine&e,std::wstring owner):engine(e),sid(std::move(owner)){}
    bool start(){PSID test=nullptr;if(!ConvertStringSidToSidW(sid.c_str(),&test))return false;LocalFree(test);firstPipe=make();if(firstPipe==INVALID_HANDLE_VALUE)return false;running=true;thread=std::thread([this]{loop();});return true;}
    ~PipeServer(){running=false;if(thread.joinable())thread.join();if(firstPipe!=INVALID_HANDLE_VALUE)CloseHandle(firstPipe);}
};
inline SERVICE_STATUS_HANDLE serviceHandle=nullptr;
inline SERVICE_STATUS serviceStatus{};
inline HANDLE serviceStop=nullptr;
inline Engine*serviceEngine=nullptr;
inline std::mutex serviceEngineLock;
struct ServiceEngineBinding {
    explicit ServiceEngineBinding(Engine&e){std::lock_guard<std::mutex> guard(serviceEngineLock);serviceEngine=&e;}
    ~ServiceEngineBinding(){std::lock_guard<std::mutex> guard(serviceEngineLock);serviceEngine=nullptr;}
};
inline void reportService(DWORD state,DWORD error=0){serviceStatus.dwServiceType=SERVICE_WIN32_OWN_PROCESS;serviceStatus.dwCurrentState=state;serviceStatus.dwControlsAccepted=state==SERVICE_RUNNING?SERVICE_ACCEPT_STOP|SERVICE_ACCEPT_SHUTDOWN|SERVICE_ACCEPT_POWEREVENT:0;serviceStatus.dwWin32ExitCode=error;serviceStatus.dwWaitHint=state==SERVICE_STOP_PENDING?10000:0;SetServiceStatus(serviceHandle,&serviceStatus);}
inline DWORD WINAPI serviceControl(DWORD control,DWORD eventType,LPVOID eventData,LPVOID){
    if(control==SERVICE_CONTROL_STOP||control==SERVICE_CONTROL_SHUTDOWN){reportService(SERVICE_STOP_PENDING);SetEvent(serviceStop);}
    std::lock_guard<std::mutex> engineGuard(serviceEngineLock);
    if(control==SERVICE_CONTROL_POWEREVENT&&serviceEngine){
        if(eventType==PBT_APMSUSPEND)serviceEngine->power(true);
        else if(eventType==PBT_APMRESUMEAUTOMATIC||eventType==PBT_APMRESUMESUSPEND)serviceEngine->power(false);
        else if(eventType==PBT_POWERSETTINGCHANGE&&eventData){auto*p=static_cast<POWERBROADCAST_SETTING*>(eventData);if(p->PowerSetting==GUID_LIDSWITCH_STATE_CHANGE&&p->DataLength>=sizeof(DWORD))serviceEngine->power(*reinterpret_cast<DWORD*>(p->Data)==0,true);}
    }return NO_ERROR;
}
inline void WINAPI serviceMain(DWORD,LPWSTR*){
    serviceHandle=RegisterServiceCtrlHandlerExW(ServiceName,serviceControl,nullptr);if(!serviceHandle)return;reportService(SERVICE_START_PENDING);
    serviceStop=CreateEventW(nullptr,TRUE,FALSE,nullptr);
    if(!serviceStop){reportService(SERVICE_STOPPED,GetLastError());return;}
    try{
        auto dir=dataDir(true);Config c=loadConfig(dir/L"profile.ini");std::wstring owner;{std::wifstream f(dir/L"owner.sid");std::getline(f,owner);}
        Engine e(c,dir/L"profile.ini",false,true);ServiceEngineBinding binding(e);
        if(!e.start()){reportService(SERVICE_STOPPED,ERROR_ALREADY_EXISTS);CloseHandle(serviceStop);return;}
        PipeServer server(e,owner);if(!server.start()){e.stop();reportService(SERVICE_STOPPED,ERROR_ACCESS_DENIED);CloseHandle(serviceStop);return;}
        auto lid=RegisterPowerSettingNotification(serviceHandle,&GUID_LIDSWITCH_STATE_CHANGE,DEVICE_NOTIFY_SERVICE_HANDLE);
        reportService(SERVICE_RUNNING);HANDLE waits[]{serviceStop,e.exited()};DWORD why=WaitForMultipleObjects(2,waits,FALSE,INFINITE);reportService(SERVICE_STOP_PENDING);e.stop();if(lid)UnregisterPowerSettingNotification(lid);{std::lock_guard<std::mutex> guard(serviceEngineLock);serviceEngine=nullptr;}
        reportService(SERVICE_STOPPED,why==WAIT_OBJECT_0?0:ERROR_PROCESS_ABORTED);
    }catch(...){reportService(SERVICE_STOPPED,ERROR_INVALID_DATA);}CloseHandle(serviceStop);
}
inline bool protectedPath(const fs::path&p){
    for(auto check=p;!check.empty()&&check!=check.root_path();check=check.parent_path()){DWORD attr=GetFileAttributesW(check.c_str());if(attr!=INVALID_FILE_ATTRIBUTES&&(attr&FILE_ATTRIBUTE_REPARSE_POINT))return false;}
    PSECURITY_DESCRIPTOR sd=nullptr;if(!ConvertStringSecurityDescriptorToSecurityDescriptorW(L"O:BAD:P(A;OICI;FA;;;SY)(A;OICI;FA;;;BA)(A;OICI;GRGX;;;BU)",SDDL_REVISION_1,&sd,nullptr))return false;
    BOOL ok=SetFileSecurityW(p.c_str(),OWNER_SECURITY_INFORMATION|DACL_SECURITY_INFORMATION|PROTECTED_DACL_SECURITY_INFORMATION,sd);LocalFree(sd);return ok!=FALSE;
}
inline bool safeLeaf(const fs::path&p){
    DWORD attr=GetFileAttributesW(p.c_str());if(attr==INVALID_FILE_ATTRIBUTES)return GetLastError()==ERROR_FILE_NOT_FOUND;
    if(attr&(FILE_ATTRIBUTE_REPARSE_POINT|FILE_ATTRIBUTE_DIRECTORY))return false;
    HANDLE f=CreateFileW(p.c_str(),FILE_READ_ATTRIBUTES,FILE_SHARE_READ|FILE_SHARE_WRITE|FILE_SHARE_DELETE,nullptr,OPEN_EXISTING,FILE_FLAG_OPEN_REPARSE_POINT,nullptr);if(f==INVALID_HANDLE_VALUE)return false;
    BY_HANDLE_FILE_INFORMATION info{};bool ok=GetFileInformationByHandle(f,&info)&&info.nNumberOfLinks==1;CloseHandle(f);return ok;
}
inline bool accountUserSid(const std::wstring&text){
    PSID sid=nullptr;if(!ConvertStringSidToSidW(text.c_str(),&sid))return false;
    DWORD nameChars=0,domainChars=0;SID_NAME_USE use{};LookupAccountSidW(nullptr,sid,nullptr,&nameChars,nullptr,&domainChars,&use);
    std::vector<wchar_t> name(nameChars),domain(domainChars);bool ok=nameChars&&LookupAccountSidW(nullptr,sid,name.data(),&nameChars,domain.data(),&domainChars,&use)&&use==SidTypeUser;LocalFree(sid);return ok;
}
inline bool writeOwnerSid(const fs::path&path,const std::wstring&sid){
    HANDLE file=CreateFileW(path.c_str(),GENERIC_WRITE|FILE_READ_ATTRIBUTES|WRITE_DAC|WRITE_OWNER,0,nullptr,OPEN_ALWAYS,FILE_FLAG_OPEN_REPARSE_POINT,nullptr);if(file==INVALID_HANDLE_VALUE)return false;
    BY_HANDLE_FILE_INFORMATION info{};bool ok=GetFileInformationByHandle(file,&info)&&info.nNumberOfLinks==1&&!(info.dwFileAttributes&(FILE_ATTRIBUTE_REPARSE_POINT|FILE_ATTRIBUTE_DIRECTORY));
    PSECURITY_DESCRIPTOR sd=nullptr;PSID owner=nullptr;PACL acl=nullptr;BOOL present=FALSE,defaulted=FALSE;
    if(ok)ok=ConvertStringSecurityDescriptorToSecurityDescriptorW(L"O:BAD:P(A;;FA;;;SY)(A;;FA;;;BA)(A;;GR;;;BU)",SDDL_REVISION_1,&sd,nullptr)!=FALSE;
    if(ok)ok=GetSecurityDescriptorOwner(sd,&owner,&defaulted)&&GetSecurityDescriptorDacl(sd,&present,&acl,&defaulted)&&present&&SetSecurityInfo(file,SE_FILE_OBJECT,OWNER_SECURITY_INFORMATION|DACL_SECURITY_INFORMATION|PROTECTED_DACL_SECURITY_INFORMATION,owner,nullptr,acl,nullptr)==ERROR_SUCCESS;
    if(ok)ok=SetFilePointer(file,0,nullptr,FILE_BEGIN)!=INVALID_SET_FILE_POINTER&&SetEndOfFile(file);auto text=utf8(sid);DWORD n=0;
    if(ok)ok=WriteFile(file,text.data(),static_cast<DWORD>(text.size()),&n,nullptr)&&n==text.size()&&FlushFileBuffers(file);if(sd)LocalFree(sd);CloseHandle(file);return ok;
}
struct ServiceHandle {SC_HANDLE value=nullptr;~ServiceHandle(){if(value)CloseServiceHandle(value);}operator SC_HANDLE()const{return value;}};
inline fs::path installedExecutable(){
    ServiceHandle scm{OpenSCManagerW(nullptr,nullptr,SC_MANAGER_CONNECT)};if(!scm.value)return {};ServiceHandle service{OpenServiceW(scm,ServiceName,SERVICE_QUERY_CONFIG)};if(!service.value)return {};
    DWORD bytes=0;QueryServiceConfigW(service,nullptr,0,&bytes);std::vector<BYTE> buffer(bytes);auto info=reinterpret_cast<QUERY_SERVICE_CONFIGW*>(buffer.data());if(!bytes||!QueryServiceConfigW(service,info,bytes,&bytes))return {};
    int count=0;auto args=CommandLineToArgvW(info->lpBinaryPathName,&count);fs::path result;if(args){if(count==2&&!wcscmp(args[1],L"--service"))result=fs::path(args[0]).lexically_normal();LocalFree(args);}
    wchar_t pf[MAX_PATH];if(FAILED(SHGetFolderPathW(nullptr,CSIDL_PROGRAM_FILES,nullptr,SHGFP_TYPE_CURRENT,pf)))return {};
    auto relative=result.lexically_relative(fs::path(pf)/L"TPFCQuiet");if(relative.empty()||*relative.begin()==L".."||result.filename()!=L"TPFCQuiet.exe"||!vendor_detail::protectedLocation(result,fs::path(pf)))return {};return result;
}
inline DWORD stopServiceChecked(SC_HANDLE service){
    SERVICE_STATUS_PROCESS state{};DWORD n=0;if(!QueryServiceStatusEx(service,SC_STATUS_PROCESS_INFO,reinterpret_cast<BYTE*>(&state),sizeof(state),&n))return GetLastError();if(state.dwCurrentState==SERVICE_STOPPED)return 0;
    struct ProcessExit{HANDLE handle=nullptr;~ProcessExit(){if(handle)CloseHandle(handle);}} exit;
    if(state.dwProcessId){exit.handle=OpenProcess(SYNCHRONIZE,FALSE,state.dwProcessId);if(!exit.handle&&GetLastError()!=ERROR_INVALID_PARAMETER)return GetLastError();}
    SERVICE_STATUS ignored{};if(!ControlService(service,SERVICE_CONTROL_STOP,&ignored)&&GetLastError()!=ERROR_SERVICE_NOT_ACTIVE)return GetLastError();
    for(int i=0;i<150;++i){if(!QueryServiceStatusEx(service,SC_STATUS_PROCESS_INFO,reinterpret_cast<BYTE*>(&state),sizeof(state),&n))return GetLastError();if(state.dwCurrentState==SERVICE_STOPPED)return !exit.handle||WaitForSingleObject(exit.handle,5000)==WAIT_OBJECT_0?0:ERROR_SERVICE_REQUEST_TIMEOUT;Sleep(100);}return ERROR_SERVICE_REQUEST_TIMEOUT;
}
inline bool sameContents(const fs::path&a,const fs::path&b){std::error_code ec;auto n=fs::file_size(a,ec);if(ec)return false;auto m=fs::file_size(b,ec);if(ec||n!=m)return false;std::ifstream x(a,std::ios::binary),y(b,std::ios::binary);if(!x||!y)return false;std::array<char,16384>left{},right{};while(x){x.read(left.data(),left.size());y.read(right.data(),right.size());if(x.gcount()!=y.gcount()||memcmp(left.data(),right.data(),static_cast<size_t>(x.gcount())))return false;}return !x.bad()&&!y.bad();}
inline DWORD closeInstalledClient(const fs::path&target){
    HWND window=FindWindowW(L"TPFCQuietWindow.C",nullptr);if(!window)return 0;DWORD pid=0;GetWindowThreadProcessId(window,&pid);if(pid==GetCurrentProcessId())return ERROR_SHARING_VIOLATION;
    HANDLE process=OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION|SYNCHRONIZE,FALSE,pid);if(!process)return GetLastError();wchar_t image[32768];DWORD chars=32768;
    bool match=QueryFullProcessImageNameW(process,0,image,&chars)&&!_wcsicmp(image,target.c_str());if(!match){CloseHandle(process);return 0;}
    DWORD_PTR result=0;bool sent=SendMessageTimeoutW(window,WM_APP+3,0,0,SMTO_ABORTIFHUNG,15000,&result)!=0;DWORD error=sent?0:GetLastError();if(sent&&WaitForSingleObject(process,5000)!=WAIT_OBJECT_0)error=ERROR_SHARING_VIOLATION;CloseHandle(process);return error;
}
inline DWORD saveUserShortcuts(const fs::path&target){
    HRESULT initialized=CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED);DWORD error=0;
    for(int folder:{CSIDL_STARTUP,CSIDL_PROGRAMS}){wchar_t location[MAX_PATH];if(FAILED(SHGetFolderPathW(nullptr,folder,nullptr,SHGFP_TYPE_CURRENT,location))){error=ERROR_PATH_NOT_FOUND;break;}Microsoft::WRL::ComPtr<IShellLinkW> link;Microsoft::WRL::ComPtr<IPersistFile> file;
          if(FAILED(CoCreateInstance(CLSID_ShellLink,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(&link)))||FAILED(link->SetPath(target.c_str()))||FAILED(link->SetIconLocation(target.c_str(),0))||FAILED(link->SetArguments(folder==CSIDL_STARTUP?L"--tray":L""))||FAILED(link->SetWorkingDirectory(target.parent_path().c_str()))||FAILED(link.As(&file))||FAILED(file->Save((fs::path(location)/L"TPFCQuiet.lnk").c_str(),TRUE))){error=ERROR_WRITE_FAULT;break;}
    }if(SUCCEEDED(initialized))CoUninitialize();return error;
}
inline int installService(bool remove){
    if(!isAdmin())return ERROR_ELEVATION_REQUIRED;HANDLE guard=CreateMutexW(nullptr,TRUE,L"Global\\TPFCQuiet.Installer");DWORD mutexError=GetLastError();if(!guard||mutexError==ERROR_ALREADY_EXISTS){if(guard)CloseHandle(guard);return ERROR_ALREADY_EXISTS;}struct Guard{HANDLE h;~Guard(){ReleaseMutex(h);CloseHandle(h);}} ownership{guard};
    ServiceHandle scm{OpenSCManagerW(nullptr,nullptr,SC_MANAGER_CONNECT|SC_MANAGER_CREATE_SERVICE)};if(!scm.value)return GetLastError();
    if(remove){ServiceHandle s{OpenServiceW(scm,ServiceName,SERVICE_STOP|SERVICE_QUERY_STATUS|DELETE)};if(!s.value){DWORD e=GetLastError();return e==ERROR_SERVICE_DOES_NOT_EXIST?0:e;}DWORD error=stopServiceChecked(s);if(error)return error;if(!DeleteService(s))return GetLastError();for(int folder:{CSIDL_STARTUP,CSIDL_PROGRAMS}){wchar_t location[MAX_PATH];SHGetFolderPathW(nullptr,folder,nullptr,SHGFP_TYPE_CURRENT,location);DeleteFileW((fs::path(location)/L"TPFCQuiet.lnk").c_str());}return 0;}
    if(originalRunning())return ERROR_ALREADY_EXISTS;
    wchar_t pf[MAX_PATH];SHGetFolderPathW(nullptr,CSIDL_PROGRAM_FILES,nullptr,SHGFP_TYPE_CURRENT,pf);auto root=fs::path(pf)/L"TPFCQuiet";auto versions=root/L"versions";auto dest=versions/version();std::error_code ec;
    auto dir=dataDir(true);std::wstring ownerSid=userSid();bool trustedPriorConfig=false;
    // Preserve an old account only from an already protected formal installation.
    // Never inherit a first-install file merely because it contains a valid SID.
    {ServiceHandle old{OpenServiceW(scm,ServiceName,SERVICE_QUERY_CONFIG)};
        if(old.value&&vendor_detail::protectedLocation(dir/L"owner.sid",dir)&&vendor_detail::protectedLocation(root,fs::path(pf))){
            DWORD bytes=0;QueryServiceConfigW(old,nullptr,0,&bytes);std::vector<BYTE> data(bytes);auto prior=reinterpret_cast<QUERY_SERVICE_CONFIGW*>(data.data());
            if(bytes&&QueryServiceConfigW(old,prior,bytes,&bytes)){int argc=0;auto argv=CommandLineToArgvW(prior->lpBinaryPathName,&argc);
                if(argv){auto image=argc==2?fs::path(argv[0]).lexically_normal():fs::path{};auto relative=image.lexically_relative(root);
                    if(argc==2&&!wcscmp(argv[1],L"--service")&&!relative.empty()&&*relative.begin()!=L".."&&image.filename()==L"TPFCQuiet.exe"&&vendor_detail::protectedLocation(image,fs::path(pf))){
                        std::wifstream file(dir/L"owner.sid");std::wstring sid;std::getline(file,sid);if(accountUserSid(sid))ownerSid=sid;
                        trustedPriorConfig=vendor_detail::protectedLocation(dir/L"profile.ini",dir);
                    }LocalFree(argv);
                }
            }
        }
    }
    if(!accountUserSid(ownerSid))return ERROR_INVALID_SID;
    for(const auto&p:{root,versions,dest}){fs::create_directories(p,ec);if(ec||!protectedPath(p))return ERROR_ACCESS_DENIED;}
    auto staged=dest/L"TPFCQuiet.exe",target=root/L"TPFCQuiet.exe",backup=root/L"TPFCQuiet.rollback",pending=root/L"TPFCQuiet.pending";if(!safeLeaf(staged)||!safeLeaf(target)||!safeLeaf(backup)||!safeLeaf(pending))return ERROR_ACCESS_DENIED;
    // Prepare and protect the new version before stopping the working service.
    if(fs::weakly_canonical(exePath())!=fs::weakly_canonical(staged)&&!sameContents(exePath(),staged)&&!CopyFileW(exePath().c_str(),staged.c_str(),FALSE))return GetLastError();
    bool replace=!sameContents(staged,target),hadTarget=fs::exists(target),promoted=false;
    if(hadTarget&&!vendor_detail::protectedLocation(target,fs::path(pf)))return ERROR_ACCESS_DENIED;
    if(replace){if(!CopyFileW(staged.c_str(),pending.c_str(),FALSE)||!protectedPath(pending))return GetLastError()?GetLastError():ERROR_ACCESS_DENIED;if(hadTarget&&(!CopyFileW(target.c_str(),backup.c_str(),FALSE)||!protectedPath(backup)))return GetLastError()?GetLastError():ERROR_ACCESS_DENIED;auto prior=installedExecutable();if(!prior.empty()){DWORD closed=closeInstalledClient(prior);if(closed)return closed;}DWORD closed=closeInstalledClient(target);if(closed)return closed;}
    auto restoreBinary=[&]()->bool{if(!promoted)return true;for(int i=0;i<25;++i){bool ok=hadTarget?CopyFileW(backup.c_str(),target.c_str(),FALSE)!=FALSE:DeleteFileW(target.c_str())!=FALSE;if(ok)return !hadTarget||sameContents(backup,target);DWORD error=GetLastError();if(!hadTarget&&error==ERROR_FILE_NOT_FOUND)return true;if(error!=ERROR_ACCESS_DENIED&&error!=ERROR_SHARING_VIOLATION)return false;Sleep(200);}return false;};
    auto promote=[&]()->DWORD{if(!replace)return 0;if(!MoveFileExW(pending.c_str(),target.c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH))return GetLastError();promoted=true;return 0;};
    if(!protectedPath(dir)||!safeLeaf(dir/L"owner.sid")||!safeLeaf(dir/L"profile.ini")||!safeLeaf(dir/L"profile.ini.new"))return ERROR_ACCESS_DENIED;
    for(const auto&p:{dir/L"profile.ini",dir/L"profile.ini.new"})if(fs::exists(p)&&!protectedPath(p))return ERROR_ACCESS_DENIED;
    if(!trustedPriorConfig){Config c;auto local=fs::exists(dir/L"profile.ini")?dir/L"profile.ini":dataDir()/L"profile.ini";if(fs::exists(local))c=loadConfig(local);c.verified=false;c.machine.clear();if(!saveConfig(dir/L"profile.ini",c))return ERROR_WRITE_FAULT;}
    for(const auto&p:{dir/L"owner.sid",dir/L"profile.ini",staged,target})if(fs::exists(p)&&!protectedPath(p))return ERROR_ACCESS_DENIED;
    std::wstring path=L"\""+target.wstring()+L"\" --service",oldPath;DWORD oldStart=SERVICE_AUTO_START;
    DWORD access=SERVICE_START|SERVICE_STOP|SERVICE_QUERY_STATUS|SERVICE_QUERY_CONFIG|SERVICE_CHANGE_CONFIG|DELETE;
    ServiceHandle service{OpenServiceW(scm,ServiceName,access)};bool existed=service.value!=nullptr;
    if(!existed&&GetLastError()!=ERROR_SERVICE_DOES_NOT_EXIST)return GetLastError();
    if(existed){DWORD bytes=0;QueryServiceConfigW(service,nullptr,0,&bytes);std::vector<BYTE> data(bytes);auto prior=reinterpret_cast<QUERY_SERVICE_CONFIGW*>(data.data());if(!bytes||!QueryServiceConfigW(service,prior,bytes,&bytes))return GetLastError();oldPath=prior->lpBinaryPathName;oldStart=prior->dwStartType;DWORD stopped=stopServiceChecked(service);if(stopped)return stopped;
        DWORD promotedError=promote();if(promotedError){StartServiceW(service,0,nullptr);return promotedError;}
        if(!writeOwnerSid(dir/L"owner.sid",ownerSid)){if(!restoreBinary())return ERROR_INSTALL_FAILURE;StartServiceW(service,0,nullptr);return ERROR_WRITE_FAULT;}
        if(!ChangeServiceConfigW(service,SERVICE_NO_CHANGE,SERVICE_AUTO_START,SERVICE_NO_CHANGE,path.c_str(),nullptr,nullptr,nullptr,nullptr,nullptr,nullptr)){DWORD error=GetLastError();if(!restoreBinary())return ERROR_INSTALL_FAILURE;StartServiceW(service,0,nullptr);return error;}
    }else{DWORD promotedError=promote();if(promotedError)return promotedError;if(!writeOwnerSid(dir/L"owner.sid",ownerSid)){return restoreBinary()?ERROR_WRITE_FAULT:ERROR_INSTALL_FAILURE;}service.value=CreateServiceW(scm,ServiceName,L"TPFC Quiet 风扇控制",access,SERVICE_WIN32_OWN_PROCESS,SERVICE_AUTO_START,SERVICE_ERROR_NORMAL,path.c_str(),nullptr,nullptr,nullptr,nullptr,nullptr);if(!service.value){DWORD error=GetLastError();return restoreBinary()?error:ERROR_INSTALL_FAILURE;}}
    auto rollback=[&]()->bool{if(!restoreBinary())return false;if(existed){if(!ChangeServiceConfigW(service,SERVICE_NO_CHANGE,oldStart,SERVICE_NO_CHANGE,oldPath.c_str(),nullptr,nullptr,nullptr,nullptr,nullptr,nullptr))return false;return StartServiceW(service,0,nullptr)!=FALSE;}return DeleteService(service)!=FALSE;};
    SERVICE_DESCRIPTIONW description{const_cast<LPWSTR>(L"本机风扇引擎，默认静音办公，温度来源与实机验证未就绪时保持BIOS。")};ChangeServiceConfig2W(service,SERVICE_CONFIG_DESCRIPTION,&description);
    SERVICE_DELAYED_AUTO_START_INFO delay{TRUE};SC_ACTION action{SC_ACTION_RESTART,10000};SERVICE_FAILURE_ACTIONSW actions{};actions.dwResetPeriod=86400;actions.cActions=1;actions.lpsaActions=&action;SERVICE_FAILURE_ACTIONS_FLAG failure{TRUE};
    if(!ChangeServiceConfig2W(service,SERVICE_CONFIG_DELAYED_AUTO_START_INFO,&delay)||!ChangeServiceConfig2W(service,SERVICE_CONFIG_FAILURE_ACTIONS,&actions)||!ChangeServiceConfig2W(service,SERVICE_CONFIG_FAILURE_ACTIONS_FLAG,&failure)){DWORD error=GetLastError();return rollback()?error:ERROR_INSTALL_FAILURE;}
    if(!StartServiceW(service,0,nullptr)){DWORD error=GetLastError();return rollback()?error:ERROR_INSTALL_FAILURE;}
    SERVICE_STATUS_PROCESS state{};DWORD size=0;bool running=false;for(int i=0;i<100;++i){if(!QueryServiceStatusEx(service,SC_STATUS_PROCESS_INFO,reinterpret_cast<BYTE*>(&state),sizeof(state),&size))break;if(state.dwCurrentState==SERVICE_RUNNING){running=true;break;}if(state.dwCurrentState==SERVICE_STOPPED)break;Sleep(100);}
    if(!running){DWORD stopped=stopServiceChecked(service);if(stopped)return stopped;return rollback()?ERROR_SERVICE_REQUEST_TIMEOUT:ERROR_INSTALL_FAILURE;}
    return saveUserShortcuts(target);
}
}
