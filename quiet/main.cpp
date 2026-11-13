#include "ui.hpp"
#include "power.hpp"
#include <cstdio>
using namespace quiet;
static bool has(int n,wchar_t**a,const wchar_t*key){for(int i=1;i<n;++i)if(!wcscmp(a[i],key))return true;return false;}
static std::wstring argument(int n,wchar_t**a,const wchar_t*key,const wchar_t*fallback=L""){for(int i=1;i+1<n;++i)if(!wcscmp(a[i],key))return a[i+1];return fallback;}
static void statusJson(std::ostream&f,bool connected,const State&s){
    f<<"{\"connected\":"<<(connected?"true":"false");if(connected){bool live=vendorFresh(s.vendor,GetTickCount64());
        f<<",\"version\":"<<jsonQuoted(utf8(version()))<<",\"mode\":"<<s.mode<<",\"profile\":"<<s.profile<<",\"verified\":"<<s.verified<<",\"healthy\":"<<s.healthy<<",\"ready\":"<<s.ready<<",\"alive\":"<<s.alive<<",\"requested\":"<<s.requested;
        f<<",\"rpm\":["<<s.rpm[0]<<','<<s.rpm[1]<<"],\"readback\":["<<s.level[0]<<','<<s.level[1]<<"],\"sample_duration_ms\":"<<s.sampleDurationMs;
        f<<",\"display_temp\":"<<(live?s.vendor.cpu:s.cpu)<<",\"display_source\":"<<jsonQuoted(live?"vantage_cpu":"ec_reference")<<",\"ec_reference_temp\":"<<s.cpu<<",\"control_temp\":"<<s.filtered<<",\"source\":"<<s.source;
        f<<",\"vendor_fresh\":"<<(live?"true":"false")<<",\"cpu\":"<<(live?s.vendor.cpu:-1)<<",\"core_max\":"<<(live?s.vendor.coreMax:-1)<<",\"gpu\":"<<(live?s.vendor.gpu:-1)<<",\"acpi_reference\":"<<(live?s.vendor.acpi:-1)<<",\"lid_closed\":"<<s.lid<<",\"resume_pending\":"<<s.resumePending<<",\"fault\":"<<s.fault<<",\"reason\":"<<jsonQuoted(utf8(std::wstring(s.reason)));
        f<<",\"closed_lid_work\":"<<s.closedLidWork<<",\"suspended\":"<<s.suspended<<",\"control_paused\":"<<s.controlPaused<<",\"commission_status\":"<<s.commissionStatus<<",\"commission_step\":"<<s.commissionStep<<",\"commission_id\":"<<s.commissionId<<",\"vendor_error\":"<<s.vendor.error<<",\"vendor_pid\":"<<s.vendor.producerPid<<",\"auto_smart\":"<<s.autoSmart<<",\"commission_rpm\":[";
        for(int i=0;i<4;++i){if(i)f<<',';f<<'['<<s.commissionRpm[i][0]<<','<<s.commissionRpm[i][1]<<']';}f<<']';
    }f<<'}';
}
int WINAPI wWinMain(HINSTANCE instance,HINSTANCE,LPWSTR,int){
    appInstance=instance;SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);SetDefaultDllDirectories(LOAD_LIBRARY_SEARCH_SYSTEM32|LOAD_LIBRARY_SEARCH_USER_DIRS);
    int argc=0;LPWSTR*argv=CommandLineToArgvW(GetCommandLineW(),&argc);if(!argv)return 1;
    if(has(argc,argv,L"--power-watch")){LocalFree(argv);return power_detail::recoverLegacyPower();}
    if(has(argc,argv,L"--eco-demo")){LocalFree(argv);return ERROR_NOT_SUPPORTED;}
    bool demo=has(argc,argv,L"--demo"),smoke=has(argc,argv,L"--ui-smoke"),headless=has(argc,argv,L"--probe"),svc=has(argc,argv,L"--service");
    if(has(argc,argv,L"--vendor-packet")||has(argc,argv,L"--vendor-json")){
        auto pidText=argument(argc,argv,L"--vendor-pid");DWORD producerPid=0;if(!pidText.empty()){wchar_t*end=nullptr;errno=0;unsigned long long value=wcstoull(pidText.c_str(),&end,10);if(errno||*end||pidText[0]==L'-'||value==0||value>MAXDWORD){LocalFree(argv);return ERROR_INVALID_PARAMETER;}producerPid=static_cast<DWORD>(value);}
        VendorSample sample;try{sample=vendor_detail::query(producerPid);}catch(...){sample.error=ERROR_INVALID_DATA;}
        if(has(argc,argv,L"--vendor-packet")){DWORD n=0;WriteFile(GetStdHandle(STD_OUTPUT_HANDLE),&sample,sizeof(sample),&n,nullptr);}
        else{std::ofstream f(fs::path(argument(argc,argv,L"--output",L"vendor.json")),std::ios::binary);if(!f){LocalFree(argv);return ERROR_WRITE_FAULT;}f<<"{\"valid\":"<<sample.valid<<",\"error\":"<<sample.error<<",\"cpu\":"<<sample.cpu<<",\"core_max\":"<<sample.coreMax<<",\"gpu\":"<<sample.gpu<<",\"acpi_reference\":"<<sample.acpi<<",\"battery\":"<<sample.battery<<",\"storage\":"<<sample.storage<<'}';}
        LocalFree(argv);return sample.valid?0:ERROR_NOT_READY;
    }
    try{
        if(has(argc,argv,L"--commission")||has(argc,argv,L"--mode")||has(argc,argv,L"--dock-work")||has(argc,argv,L"--auto-smart")){
            bool commission=has(argc,argv,L"--commission");if(commission&&!isAdmin())return ERROR_ELEVATION_REQUIRED;
            Request request;int wanted=-1;if(commission){request.command=8;UUID id{};UuidCreate(&id);request.value=static_cast<int>(id.Data1&0x7fffffff);if(!request.value)request.value=1;}
            else if(has(argc,argv,L"--auto-smart")){auto value=argument(argc,argv,L"--auto-smart");if(value!=L"on"&&value!=L"off")return ERROR_INVALID_PARAMETER;request.command=10;request.value=wanted=value==L"on"?1:0;}
            else if(has(argc,argv,L"--dock-work")){auto value=argument(argc,argv,L"--dock-work");if(value!=L"on"&&value!=L"off")return ERROR_INVALID_PARAMETER;request.command=9;request.value=wanted=value==L"on"?1:0;}
            else{auto value=argument(argc,argv,L"--mode");wanted=value==L"bios"?0:value==L"shadow"?1:value==L"smart"?2:-1;if(wanted<0)return ERROR_INVALID_PARAMETER;request.command=2;request.value=wanted;}
            std::ofstream file(fs::path(argument(argc,argv,L"--output",L"command-result.json")),std::ios::binary);if(!file)return ERROR_WRITE_FAULT;
            State state;bool success=false;
            struct Cancel {bool active;int id;bool&success;~Cancel(){if(active&&!success){Request stop;stop.command=11;stop.value=id;State ignored;remote(stop,ignored);}}} cancel{commission,request.value,success};
            if(!remote(request,state)){file<<"{\"success\":false,\"failure\":\"service_communication\"}";return ERROR_SERVICE_NOT_ACTIVE;}
            uint64_t until=GetTickCount64()+(commission?100000:12000);file<<"{\"samples\":[";bool comma=false;int lost=0;
            do{Sleep(1000);Request query;if(!remote(query,state)){if(++lost<3)continue;break;}lost=0;if(comma)file<<',';statusJson(file,true,state);file.flush();comma=true;if(!file)break;
                if(commission){if(state.commissionId==request.value){if(state.commissionStatus==2){success=true;break;}if(state.commissionStatus==-1)break;}else if(state.commissionStatus==1)break;}
                else if(request.command==9?state.closedLidWork==wanted:request.command==10?state.autoSmart==wanted:state.mode==wanted){success=true;break;}
            }while(GetTickCount64()<until);
            file<<"],\"success\":"<<(success?"true":"false")<<'}';file.flush();if(!file)success=false;LocalFree(argv);return success?0:ERROR_NOT_READY;
        }
        if(has(argc,argv,L"--install")||has(argc,argv,L"--uninstall")||has(argc,argv,L"--upgrade")){
            if constexpr(QUIET_STAGE!=3)return ERROR_NOT_SUPPORTED;bool remove=has(argc,argv,L"--uninstall");int code=0;auto installed=installedExecutable();if(!installed.empty())code=closeInstalledClient(installed);if(!code){power_detail::Lock lock;power_detail::Native api;code=lock.owned?power_detail::restore(api):ERROR_TIMEOUT;}if(!code){power_detail::RecoveryTask task;code=task.stop();}if(!code){power_detail::RecoveryTask task;code=task.install({},true);}if(!code)code=installService(remove);
            auto report=argument(argc,argv,L"--output");if(!report.empty()){std::ofstream f(fs::path(report),std::ios::binary);f<<"{\"version\":"<<jsonQuoted(utf8(version()))<<",\"error\":"<<code<<",\"removed\":"<<(remove?"true":"false")<<'}';}
            std::wstring message=code?L"操作未完成，Windows错误码："+std::to_wstring(code):remove?L"后台服务和当前用户启动项已移除。诊断与配置仍保留。":L"后台服务已更新，已添加开始菜单与登录托盘入口。\n从开始菜单打开 TPFCQuiet 即可查看面板。\n未完成本机控制验证时保持BIOS控制。";
            if(!has(argc,argv,L"--silent"))MessageBoxW(nullptr,message.c_str(),L"TPFC Quiet",code?MB_ICONWARNING:MB_OK);LocalFree(argv);return code;
        }
        if(has(argc,argv,L"--status")){
            State s;Request r;bool ok=remote(r,s);auto output=argument(argc,argv,L"--output",L"status.json");std::ofstream f(fs::path(output),std::ios::binary);if(!f)return ERROR_WRITE_FAULT;
            statusJson(f,ok,s);LocalFree(argv);return ok?0:ERROR_SERVICE_NOT_ACTIVE;
        }
        if(svc){if constexpr(QUIET_STAGE!=3)return ERROR_NOT_SUPPORTED;SERVICE_TABLE_ENTRYW entries[]={{const_cast<LPWSTR>(ServiceName),serviceMain},{nullptr,nullptr}};BOOL ok=StartServiceCtrlDispatcherW(entries);LocalFree(argv);return ok?0:GetLastError();}
        if(headless){
            std::wstring output=argument(argc,argv,L"--output",L"probe.csv");int seconds=std::clamp(_wtoi(argument(argc,argv,L"--seconds",L"30").c_str()),2,3600);
            if(originalRunning())return ERROR_ALREADY_EXISTS;
            HANDLE owned=CreateMutexW(nullptr,TRUE,L"Global\\TPFCQuiet.Engine");DWORD ownership=GetLastError();if(!owned||ownership==ERROR_ALREADY_EXISTS){if(owned)CloseHandle(owned);return ERROR_ALREADY_EXISTS;}struct ProbeOwnership{HANDLE h;~ProbeOwnership(){ReleaseMutex(h);CloseHandle(h);}} guard{owned};
            Backend b;std::ofstream f(fs::path(output),std::ios::binary);if(!f)return ERROR_WRITE_FAULT;
            f<<"time,valid,rpm1,rpm2,fan1,fan2";for(int i=0;i<12;++i)f<<",sensor"<<i;f<<",error\n";
            uint64_t start=GetTickCount64();int valid=0;while(GetTickCount64()-start<seconds*1000ULL){auto s=b.sample();valid+=s.valid;f<<utf8(timestamp())<<','<<s.valid<<','<<s.rpm[0]<<','<<s.rpm[1]<<','<<s.level[0]<<','<<s.level[1];for(int t:s.raw)f<<','<<t;f<<','<<utf8(s.error)<<'\n';f.flush();Sleep(2000);}LocalFree(argv);return valid?0:ERROR_READ_FAULT;
        }
        auto cfgPath=dataDir()/L"profile.ini";Config c;if(fs::exists(cfgPath))c=loadConfig(cfgPath);else saveConfig(cfgPath,c);
        if constexpr(QUIET_STAGE==1){c.curve.leave={0,50,60,70,80};}
        auto image=argument(argc,argv,L"--preview");bool hidden=has(argc,argv,L"--hidden");bool trayOnly=has(argc,argv,L"--tray");
        State remoteState;Request query;
        auto installed=QUIET_STAGE==3&&!demo?installedExecutable():fs::path{};
        if(QUIET_STAGE==3&&!demo&&(trayOnly||remote(query,remoteState)||!installed.empty())){
            HANDLE clientGate=nullptr;
            if(!smoke){
                if(!installed.empty()&&_wcsicmp(installed.c_str(),exePath().c_str())){
                    std::wstring command=L"\""+installed.wstring()+L"\""+(trayOnly?L" --tray":L"");STARTUPINFOW startup{sizeof(startup)};PROCESS_INFORMATION process{};
                    bool ok=CreateProcessW(installed.c_str(),command.data(),nullptr,nullptr,FALSE,0,nullptr,installed.parent_path().c_str(),&startup,&process)!=FALSE;
                    DWORD launchError=ok?0:GetLastError();if(ok){CloseHandle(process.hThread);CloseHandle(process.hProcess);}LocalFree(argv);return launchError;
                }
                auto name=L"Local\\TPFCQuiet.Client.C."+userSid();clientGate=CreateMutexExW(nullptr,name.c_str(),0,SYNCHRONIZE);DWORD error=GetLastError();if(!clientGate)return error;
                if(error==ERROR_ALREADY_EXISTS){DWORD activationError=0;if(!trayOnly){HWND existing=nullptr;for(int i=0;i<10&&!existing;++i){existing=FindWindowW(ClientWindowClass,nullptr);if(!existing)Sleep(50);}if(!existing)activationError=ERROR_NOT_READY;else if(!PostMessageW(existing,WM_APP+2,0,0))activationError=GetLastError();}CloseHandle(clientGate);LocalFree(argv);return activationError;}
            }
            struct Gate{HANDLE handle;~Gate(){if(handle)CloseHandle(handle);}} gate{clientGate};
            if(!smoke){DWORD code=power_detail::recoverLegacyPower();if(code){MessageBoxW(nullptr,(L"旧版CPU限制尚未恢复，暂不启动面板。请重新运行升级程序重试。恢复记录已保留。错误码："+std::to_wstring(code)).c_str(),L"TPFC Quiet",MB_ICONWARNING);LocalFree(argv);return code;}}
            Window w(nullptr,c,false,smoke,image);LocalFree(argv);return w.run(hidden||trayOnly);
        }
        if(trayOnly&&!demo){MessageBoxW(nullptr,L"后台服务尚未就绪。可稍后重新打开程序。",L"TPFC Quiet",MB_ICONINFORMATION);LocalFree(argv);return ERROR_SERVICE_NOT_ACTIVE;}
        Engine engine(c,cfgPath,demo);if(!engine.start()){MessageBoxW(nullptr,L"已有控制引擎或原版程序运行。请从托盘打开现有程序，避免两个控制器同时运行。",L"TPFC Quiet",MB_ICONINFORMATION);LocalFree(argv);return ERROR_ALREADY_EXISTS;}
        Window window(&engine,c,demo,smoke,image);LocalFree(argv);int result=window.run(hidden);engine.stop();return result;
    }catch(const std::exception&e){MessageBoxW(nullptr,(L"启动未完成："+wide(e.what())+L"\n没有启用新的Smart控制。配置保留在本机。 ").c_str(),L"TPFC Quiet",MB_ICONERROR);LocalFree(argv);return ERROR_INVALID_DATA;}
}
