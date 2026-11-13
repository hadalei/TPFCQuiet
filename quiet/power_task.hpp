#pragma once
#include "platform.hpp"
#include <taskschd.h>
#pragma comment(lib,"taskschd.lib")
namespace quiet { namespace power_detail {
template<class T>struct Com {T*p=nullptr;~Com(){if(p)p->Release();}T**put(){return &p;}T*operator->(){return p;}};
struct Bstr {BSTR p;explicit Bstr(const std::wstring&s):p(SysAllocString(s.c_str())){}~Bstr(){SysFreeString(p);}operator BSTR()const{return p;}};
inline std::wstring watchName(){return L"Local\\TPFCQuiet.PowerRecovery."+userSid();}
inline bool watchRunning();
class RecoveryTask {
    HRESULT initialized;Com<ITaskService> service;Com<ITaskFolder> folder;HRESULT status;
    std::wstring name=L"TPFCQuiet.PowerRecovery."+userSid();
public:
    RecoveryTask():initialized(CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED)),status(E_FAIL){
        status=CoCreateInstance(CLSID_TaskScheduler,nullptr,CLSCTX_INPROC_SERVER,IID_ITaskService,reinterpret_cast<void**>(service.put()));
        VARIANT empty{};if(SUCCEEDED(status))status=service->Connect(empty,empty,empty,empty);if(SUCCEEDED(status))status=service->GetFolder(Bstr(L"\\"),folder.put());
    }
    ~RecoveryTask(){// Release COM interfaces before balancing this apartment.
        if(folder.p){folder.p->Release();folder.p=nullptr;}if(service.p){service.p->Release();service.p=nullptr;}if(SUCCEEDED(initialized))CoUninitialize();
    }
    DWORD stop(){if(FAILED(status))return static_cast<DWORD>(status);Com<IRegisteredTask> task;HRESULT e=folder->GetTask(Bstr(name),task.put());if(e==HRESULT_FROM_WIN32(ERROR_FILE_NOT_FOUND))return watchRunning()?ERROR_BUSY:0;if(FAILED(e))return static_cast<DWORD>(e);TASK_STATE initial=TASK_STATE_UNKNOWN;e=task->get_State(&initial);if(FAILED(e))return static_cast<DWORD>(e);if(initial!=TASK_STATE_RUNNING&&initial!=TASK_STATE_QUEUED&&!watchRunning())return 0;e=task->Stop(0);if(e!=S_OK)return e==S_FALSE?ERROR_BUSY:static_cast<DWORD>(e);
        for(int i=0;i<50;++i){TASK_STATE state=TASK_STATE_UNKNOWN;e=task->get_State(&state);if(FAILED(e))return static_cast<DWORD>(e);if(state!=TASK_STATE_RUNNING&&state!=TASK_STATE_QUEUED&&!watchRunning())return 0;Sleep(100);}return ERROR_TIMEOUT;}
    // Removal only. This version never registers a CPU power guardian.
    DWORD install(const fs::path&,bool remove=false){
        if(!remove)return ERROR_NOT_SUPPORTED;
        if(FAILED(status))return static_cast<DWORD>(status);HRESULT e=folder->DeleteTask(Bstr(name),0);
        return SUCCEEDED(e)||e==HRESULT_FROM_WIN32(ERROR_FILE_NOT_FOUND)?0:static_cast<DWORD>(e);
    }
};
inline bool watchRunning(){HANDLE h=OpenMutexW(SYNCHRONIZE|MUTEX_MODIFY_STATE,FALSE,watchName().c_str());if(!h)return false;DWORD e=WaitForSingleObject(h,0);if(e==WAIT_OBJECT_0||e==WAIT_ABANDONED)ReleaseMutex(h);CloseHandle(h);return e==WAIT_TIMEOUT;}
} }
