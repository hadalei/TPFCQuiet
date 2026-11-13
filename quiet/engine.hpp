#pragma once
#include "backend.hpp"
#include "config.hpp"
#include "vendor.hpp"
#include "recovery.hpp"
namespace quiet {
constexpr uint32_t Magic=0x51465054,Protocol=3;
struct Point { uint64_t at=0; int cpu=-1,control=-1,rpm1=-1,rpm2=-1,target=128; };
struct State {
    uint32_t magic=Magic,protocol=Protocol,stage=QUIET_STAGE;
    uint64_t at=0,sampleAt=0,manualUntil=0;
    int mode=0,profile=0,requested=128,suggested=128,filtered=-1,source=-1,cpu=-1,period=2000,display=0;
    int raw[12]{},rpm[2]{-1,-1},level[2]{-1,-1};
    int ready=0,verified=0,healthy=0,fault=0,demo=0,lid=0,eventCount=0,historyCount=0;
    int cpuSensor=0,autoSmart=0,enter[4]{60,70,80,90},leave[4]{50,65,75,85};
    int controlMask=1,offset[12]{},manualMinutes=10,manualExit=78,alive=1;
    int sampleDurationMs=0;
    int resumePending=0,closedLidWork=0,suspended=0,controlPaused=0;
    int commissionStatus=0,commissionStep=-1,commissionId=0,commissionRpm[4][2]{};
    VendorSample vendor;
    wchar_t reason[200]{},machine[200]{},events[24][200]{};
    Point history[120]{};
};
inline void textcopy(wchar_t*dst,size_t count,const std::wstring&s){wcsncpy_s(dst,count,s.c_str(),_TRUNCATE);}
class Journal {
    fs::path dir;
    std::deque<std::pair<uint64_t,std::string>> ring;
    uint64_t after=0;fs::path capture;
    static const char*columns(){return "time,sample_tick,mode,profile,source,display_ec_temp,verified,demo,display_sensor,filtered,requested,suggested,rpm1,rpm2,readback1,readback2,healthy,fault,ec78,ec79,ec7a,ec7b,ec7c,ec7d,ec7e,ec7f,ecc0,ecc1,ecc2,ecc3,reason,vantage_cpu,cpu_core_max,gpu,acpi_reference,battery,storage,vendor_fresh,vendor_age_ms,lid_closed,suspended,closed_lid_work,control_paused,resume_pending,commission_status";}
    void rotate(const fs::path&p){std::error_code ec;if(fs::file_size(p,ec)<10*1024*1024||ec)return;auto prefix=p.stem().wstring()+L"-";for(int i=4;i>=1;--i){fs::path from=dir/(prefix+std::to_wstring(i)+L".csv"),to=dir/(prefix+std::to_wstring(i+1)+L".csv");if(i==4)fs::remove(from,ec);else if(fs::exists(from))MoveFileExW(from.c_str(),to.c_str(),MOVEFILE_REPLACE_EXISTING);}MoveFileExW(p.c_str(),(dir/(prefix+L"1.csv")).c_str(),MOVEFILE_REPLACE_EXISTING);}
public:
    explicit Journal(fs::path d):dir(std::move(d)){std::error_code ec;fs::create_directories(dir,ec);}
    fs::path lastExport;
    bool failure(const std::string&line){
        auto path=dir/L"control-failures.jsonl";
        HANDLE f=CreateFileW(path.c_str(),FILE_APPEND_DATA|FILE_READ_ATTRIBUTES,FILE_SHARE_READ,nullptr,OPEN_ALWAYS,FILE_FLAG_OPEN_REPARSE_POINT,nullptr);
        if(f==INVALID_HANDLE_VALUE)return false;BY_HANDLE_FILE_INFORMATION info{};DWORD n=0;
        bool ok=GetFileInformationByHandle(f,&info)&&info.nNumberOfLinks==1&&!(info.dwFileAttributes&(FILE_ATTRIBUTE_REPARSE_POINT|FILE_ATTRIBUTE_DIRECTORY))&&!info.nFileSizeHigh&&info.nFileSizeLow+line.size()<=2*1024*1024;
        if(ok)ok=WriteFile(f,line.data(),static_cast<DWORD>(line.size()),&n,nullptr)&&n==line.size();CloseHandle(f);return ok;
    }
    void push(const State&s){
        std::ostringstream line;line<<utf8(timestamp())<<','<<s.sampleAt<<','<<s.mode<<','<<s.profile<<','<<s.source<<','<<s.cpu<<','<<s.verified<<','<<s.demo<<','<<s.cpuSensor<<','<<s.filtered<<','<<s.requested<<','<<s.suggested<<','<<s.rpm[0]<<','<<s.rpm[1]<<','<<s.level[0]<<','<<s.level[1]<<','<<s.healthy<<','<<s.fault;
        for(int raw:s.raw)line<<','<<raw;
        bool named=vendorFresh(s.vendor,GetTickCount64());auto now=GetTickCount64();int age=s.vendor.at&&now>=s.vendor.at?static_cast<int>(std::min<uint64_t>(now-s.vendor.at+static_cast<uint64_t>(std::max(0,s.vendor.sourceAgeMs)),INT_MAX)):-1;
        line<<",\""<<utf8(std::wstring(s.reason))<<"\","<<(named?s.vendor.cpu:-1)<<','<<(named?s.vendor.coreMax:-1)<<','<<(named?s.vendor.gpu:-1)<<','<<(named?s.vendor.acpi:-1)<<','<<(named?s.vendor.battery:-1)<<','<<(named?s.vendor.storage:-1)<<','<<named<<','<<age<<','<<s.lid<<','<<s.suspended<<','<<s.closedLidWork<<','<<s.controlPaused<<','<<s.resumePending<<','<<s.commissionStatus<<'\n';
        auto row=line.str();ring.emplace_back(GetTickCount64(),row);while(!ring.empty()&&GetTickCount64()-ring.front().first>1800000)ring.pop_front();
        auto p=dir/L"telemetry-v3.csv";rotate(p);bool header=!fs::exists(p);std::ofstream f(p,std::ios::app|std::ios::binary);if(header)f<<columns()<<'\n';f<<row;
        if(after){std::ofstream c(capture,std::ios::app|std::ios::binary);c<<row;if(GetTickCount64()>=after){lastExport=capture;after=0;}}
    }
    fs::path exportNow(){auto p=dir/(L"diagnostic-"+std::to_wstring(GetTickCount64())+L".csv");std::ofstream f(p,std::ios::binary);f<<columns()<<'\n';for(auto&r:ring)f<<r.second;lastExport=p;return p;}
    fs::path mark(){capture=dir/(L"noise-"+std::to_wstring(GetTickCount64())+L".csv");std::ofstream f(capture,std::ios::binary);f<<columns()<<'\n';uint64_t now=GetTickCount64();for(auto&r:ring)if(now-r.first<=300000)f<<r.second;after=now+300000;lastExport=capture;return capture;}
};
struct Command { int kind=0,value=0;Config config;bool admin=false; };
class Engine {
#ifdef QUIET_TESTING
    friend struct EngineTestAccess;
    const Sample* testSample=nullptr;
    const VendorSample* testVendor=nullptr;
    bool testThrowOnce=false;
#endif
    Backend backend;VendorReader vendorReader;Config config;fs::path cfgPath;Journal journal;
    State state;mutable std::mutex stateLock,queueLock;std::deque<Command> queue;std::thread worker;
    std::atomic<bool> running{false};std::mutex powerLock;std::deque<int> powerEvents;
    HANDLE stopEvent=nullptr,wakeEvent=nullptr,powerAck=nullptr,exitedEvent=nullptr,instance=nullptr,originalMutex=nullptr;
    Policy policy;SmartRecovery recovery;Mode mode=Mode::Bios,beforeLid=Mode::Bios;int manual=1,good=0,bad=0,stall=0,desired=128;
    bool demo=false,service=false,controlOwned=false,resuming=false,faultLatch=false,lidClosed=false,suspended=false,firmwareConfirmed=false;
    uint64_t startAt=0,lastHistory=0,startedFan=0,manualDeadline=0;std::wstring reason=L"启动后由BIOS控制";int legacySuggested=0;
    std::deque<std::wstring> events;std::deque<Point> history;
    uint64_t commissionDeadline=0,commissionSince=0,commissionLast=0;int commissionGood=0,commissionMin[2]{INT_MAX,INT_MAX},commissionMax[2]{};
    void resetCommissionSamples(){commissionGood=0;commissionLast=0;for(int f=0;f<2;++f){commissionMin[f]=INT_MAX;commissionMax[f]=0;}}
    bool paused()const{return suspended||(lidClosed&&!config.closedLidWork);}
    bool controlling()const{return mode==Mode::Smart||mode==Mode::Manual||mode==Mode::Commission;}
    DWORD samplingCadence()const{return mode==Mode::Commission?2000:static_cast<DWORD>(config.period);}
    void powerTransition(bool before){
        bool blocked=paused();state.lid=lidClosed;state.suspended=suspended;state.controlPaused=blocked;state.closedLidWork=config.closedLidWork;
        if(blocked){if(!before){beforeLid=mode;bios(suspended?L"进入睡眠":L"合盖暂停");}good=0;resuming=false;}
        else if(before){bios(L"工作状态恢复后确认");good=0;policy.reset();resuming=true;event(L"已恢复工作，等待连续采样和保护来源");}
    }
    void event(const std::wstring&s){reason=s;events.push_front(timestamp().substr(11)+L"  "+s);while(events.size()>24)events.pop_back();}
    bool apply(int fan){
        int previous=desired;
        if(demo){state.level[0]=state.level[1]=fan;controlOwned=fan!=128;desired=fan;if((previous==0||previous==128)&&fan>0&&fan!=128){startedFan=GetTickCount64();stall=rpmBad=0;}return true;}
        std::array<int,2> back{-1,-1};bool ok=backend.set(fan,back);state.level[0]=back[0];state.level[1]=back[1];
        if(ok){if((previous==0||previous==128)&&fan>0&&fan!=128){startedFan=GetTickCount64();stall=rpmBad=0;}desired=fan;controlOwned=fan!=128;}
        return ok;
    }
    bool bios(const std::wstring&why,bool force=false){
        recovery.cancel();
        if(mode==Mode::Commission&&state.commissionStatus==1)state.commissionStatus=-1;
        bool ok=true;if(controlOwned||backend.wrote||force)ok=apply(128);
        mode=Mode::Bios;policy.reset();manualDeadline=0;stall=rpmBad=0;legacySuggested=0;
        if(ok){controlOwned=false;desired=128;firmwareConfirmed=true;event(why+L"，BIOS控制");}
        else{firmwareConfirmed=false;faultLatch=true;event(L"交还BIOS失败，请检查驱动与风扇状态");}return ok;
    }
    enum class Fault { Sensor, Write, Readback, Stall, Hot };
    void controlFault(const std::wstring&why,Fault kind,uint64_t now,const Sample&sample){
        bool canRecover=mode==Mode::Smart&&(kind==Fault::Sensor||kind==Fault::Write);
        // Snapshot before BIOS handoff overwrites desired and backend.lastWrite.
        std::ostringstream out;out<<"{\"time\":"<<jsonQuoted(utf8(timestamp()))<<",\"tick\":"<<now<<",\"reason\":"<<jsonQuoted(utf8(why))<<",\"kind\":"<<static_cast<int>(kind)<<",\"mode_before\":"<<static_cast<int>(mode)<<",\"requested_before\":"<<desired<<",\"sample_valid\":"<<sample.valid<<",\"sample_error\":"<<jsonQuoted(utf8(sample.error))<<",\"sample_readback\":["<<sample.level[0]<<','<<sample.level[1]<<"],\"sample_rpm\":["<<sample.rpm[0]<<','<<sample.rpm[1]<<"],\"raw\":[";
        for(int i=0;i<12;++i){if(i)out<<',';out<<sample.raw[i];}
        out<<"],\"cpu\":"<<state.vendor.cpu<<",\"core_max\":"<<state.vendor.coreMax<<",\"gpu\":"<<state.vendor.gpu;
        if(kind==Fault::Write){const auto&w=backend.lastWrite;out<<",\"write_target\":"<<w.target<<",\"write_readback\":["<<w.back[0]<<','<<w.back[1]<<"],\"attempts\":"<<w.attempts<<",\"duration_ms\":"<<w.durationMs<<",\"step\":"<<jsonQuoted(utf8(w.step))<<",\"first_failure\":"<<jsonQuoted(utf8(w.firstFailure))<<",\"ec_status\":"<<w.status<<",\"selected\":["<<w.selected[0]<<','<<w.selected[1]<<"],\"written\":["<<w.written[0]<<','<<w.written[1]<<"],\"read_ok\":["<<w.read[0]<<','<<w.read[1]<<']';}
        faultLatch=true;bool handed=bios(why);
        bool armed=canRecover&&handed&&recovery.arm(kind==Fault::Write?SmartRecovery::Kind::Write:SmartRecovery::Kind::Sensor,now);
        if(armed)event(why+L"；BIOS代管，等待数据稳定30秒且温度不高于80°C后恢复Smart");
        else if(canRecover&&handed)event(why+L"；自动恢复次数已用完，保持BIOS，请检查事件");
        out<<",\"bios_confirmed\":"<<handed<<",\"recovery_pending\":"<<armed<<"}\n";
        try{if(!journal.failure(out.str()))event(reason+L"（故障详细记录未能保存）");}catch(...){event(reason+L"（故障详细记录未能保存）");}
    }
    int manualTemperature()const{int maximum=state.cpu;for(int i=0;i<12;++i)if((config.mask&(1<<i))&&temperatureByteValid(state.raw[i]))maximum=std::max(maximum,state.raw[i]);return std::max({maximum,state.vendor.coreMax,state.vendor.gpu});}
    void commissionTick(const Sample&sample,uint64_t now){
        constexpr int stages[]{7,4,2,1};int index=state.commissionStep;
        if(paused()||!state.healthy||!vendorFresh(state.vendor,demo?now:GetTickCount64())||now>=commissionDeadline||manualTemperature()>=90){bios(L"档位验证中止：工作状态、数据或温度保护",true);return;}
        if(index<0){index=state.commissionStep=0;commissionSince=now;resetCommissionSamples();if(!apply(stages[index])){bios(L"档位验证写入失败",true);return;}event(L"档位验证：7档短时降温");return;}
        int target=stages[index];
        if(sample.level[0]!=target||sample.level[1]!=target){bios(L"档位验证读回不一致",true);return;}
        if(now>=commissionSince+4000&&(sample.rpm[0]<=0||sample.rpm[1]<=0)){bios(L"档位验证转速无效",true);return;}
        if(index==0&&manualTemperature()>78){for(int&rpm:state.commissionRpm[0])rpm=0;resetCommissionSamples();if(reason!=L"7档降温，等待适合验证低档的温度")event(L"7档降温，等待适合验证低档的温度");return;}
        if(index>0&&manualTemperature()>78){bios(L"档位验证中止：温度超过低档验证上限",true);return;}
        if(now<commissionSince+4000||(commissionGood>0&&now<commissionLast+1500))return;
        if(sample.rpm[0]<=0||sample.rpm[1]<=0){bios(L"档位验证转速无效",true);return;}
        commissionLast=now;for(int f=0;f<2;++f){state.commissionRpm[index][f]+=sample.rpm[f];commissionMin[f]=std::min(commissionMin[f],sample.rpm[f]);commissionMax[f]=std::max(commissionMax[f],sample.rpm[f]);}
        if(++commissionGood<3)return;
        if(commissionMax[0]-commissionMin[0]>400||commissionMax[1]-commissionMin[1]>400){for(int&rpm:state.commissionRpm[index])rpm=0;resetCommissionSamples();event(L"等待该档转速稳定后重新取样");return;}
        for(int f=0;f<2;++f)state.commissionRpm[index][f]/=3;
        if(index==3){
            for(int f=0;f<2;++f)for(int step=0;step<3;++step)if(state.commissionRpm[step+1][f]>state.commissionRpm[step][f]+200){bios(L"档位响应顺序异常，未放行Smart",true);return;}
            for(int f=0;f<2;++f)if(state.commissionRpm[0][f]<state.commissionRpm[3][f]+150){bios(L"档位验证未确认高低速差异",true);return;}
            if(!bios(L"档位验证完成，先确认BIOS接管",true))return;
            Config next=config;next.verified=true;next.machine=machineId();
            if(!saveConfig(cfgPath,next)){state.commissionStatus=-1;event(L"验证记录保存失败，未放行Smart");return;}
            config=next;state.commissionStatus=2;event(L"7/4/2/1档验证通过；最低1档，停转未放行");return;
        }
        if(manualTemperature()>78){bios(L"当前温度不适合继续低档验证",true);return;}
        state.commissionStep=index+1;resetCommissionSamples();commissionSince=now;
        if(!apply(stages[index+1])){bios(L"档位验证写入失败",true);return;}event(L"档位验证："+std::to_wstring(stages[index+1])+L"档");
    }
    void command(const Command&c){
        if(mode==Mode::Commission&&c.kind!=4&&c.kind!=5&&c.kind!=11&&!(c.kind==2&&c.value==0)){event(L"档位验证进行中，可选择BIOS中止");return;}
        if(c.kind==1&&c.value>=0&&c.value<=2){config.profile=static_cast<Profile>(c.value);policy.profile=config.profile;policy.reset();saveConfig(cfgPath,config);event(L"策略切换为"+profileName(config.profile));}
        else if(c.kind==2&&c.value>=0&&c.value<=3){
            auto m=static_cast<Mode>(c.value);
            if(m==Mode::Bios){resuming=false;bios(L"用户暂停Smart",true);return;}
            if(paused()){event(L"睡眠或合盖暂停期间保持BIOS控制");return;}
            if(m==Mode::Shadow){resuming=false;if(bios(L"影子模式不执行调速")){mode=m;faultLatch=false;event(L"影子观察已开启，实际风扇仍由BIOS控制");}return;}
            if((!config.certified()&&!demo)||good<3||!state.ready){event(L"尚未完成实机验证或CPU/GPU保护输入不可用，保持当前模式");return;}
            if(!state.healthy){event(L"当前温度无效，不能启用控制");return;}
            if(m==Mode::Manual&&manualTemperature()>config.manualExit){event(L"当前温度偏高，未启用限时手动");return;}
            resuming=false;recovery.cancel();faultLatch=false;policy.reset();mode=m;if(m==Mode::Manual)manualDeadline=GetTickCount64()+config.manualMinutes*60000ULL;
            event(L"用户启用"+modeName(m)+L" / "+profileName(config.profile));
        }else if(c.kind==3&&(c.value==0||c.value==1||c.value==2||c.value==4||c.value==7)){manual=c.value;event(L"手动目标设为"+std::to_wstring(manual)+L"档，仍受温度保护");}
        else if(c.kind==7&&(c.value==0||c.value==1||c.value==2||c.value==4||c.value==7)){
            if(paused()||c.value==0||(!config.certified()&&!demo)||good<3||!state.healthy||!state.ready){event(L"尚未满足控制条件或停转未标定，未改变目标档位");return;}
            if(manualTemperature()>config.manualExit){event(L"当前温度偏高，未启用限时手动");return;}
            resuming=false;recovery.cancel();manual=c.value;faultLatch=false;mode=Mode::Manual;manualDeadline=GetTickCount64()+config.manualMinutes*60000ULL;event(L"启用限时手动，目标"+std::to_wstring(manual)+L"档");
        }else if(c.kind==8){
            state.commissionId=c.value;
            if(!c.admin||mode!=Mode::Bios||paused()||good<3||!state.healthy||!firmwareConfirmed||!vendorFresh(state.vendor,demo?state.at:GetTickCount64())){state.commissionStatus=-1;event(L"档位验证条件未满足，保持BIOS");return;}
            state.commissionStatus=1;state.commissionStep=-1;for(auto&pair:state.commissionRpm)for(int&rpm:pair)rpm=0;
            commissionDeadline=GetTickCount64()+90000;commissionSince=0;resetCommissionSamples();resuming=false;recovery.cancel();mode=Mode::Commission;event(L"开始受保护的双风扇档位验证");
        }else if(c.kind==11){if(mode==Mode::Commission&&state.commissionId==c.value)bios(L"本轮档位验证取消",true);
        }else if(c.kind==10&&(c.value==0||c.value==1)){
            Config next=config;next.autoSmart=c.value==1;if(saveConfig(cfgPath,next)){config=next;event(config.autoSmart?L"已启用验证通过后的开机恢复Smart":L"已关闭开机恢复Smart");}else event(L"开机恢复设置保存失败");
        }else if(c.kind==9&&(c.value==0||c.value==1)){
            Config next=config;next.closedLidWork=c.value==1;if(!saveConfig(cfgPath,next)){event(L"合盖工作设置保存失败");return;}bool before=paused();config=next;powerTransition(before);event(config.closedLidWork?L"已启用合盖工作，实际睡眠仍交还BIOS":L"已恢复合盖暂停控制");
        }else if(c.kind==4){auto p=journal.mark();event(L"已标记噪声，继续采集后5分钟："+p.wstring());}
        else if(c.kind==5){auto p=journal.exportNow();event(L"已导出："+p.wstring());}
        else if(c.kind==6&&c.config.valid()){
            // UI cannot certify a machine or change safety-critical mapping.
            Config next=config;next.profile=c.config.profile;next.display=c.config.display;next.autoSmart=c.config.autoSmart;next.closedLidWork=c.config.closedLidWork;next.curve=c.config.curve;
            if constexpr(QUIET_STAGE==1){next.curve=config.curve;next.profile=config.profile;}
            if(saveConfig(cfgPath,next)){bool before=paused();config=next;powerTransition(before);policy.curve=config.curve;policy.profile=config.profile;policy.reset();event(L"设置已保存，控制窗口已重置");}
            else event(L"设置保存失败，保留原设置");
        }
    }
    void tick(uint64_t now=GetTickCount64()){
#ifdef QUIET_TESTING
        if(testThrowOnce){testThrowOnce=false;throw std::runtime_error("injected engine failure");}
#endif
        std::deque<int> powers;{std::lock_guard<std::mutex>l(powerLock);powers.swap(powerEvents);}
        for(int power:powers){bool wasBlocked=paused();if(power==1)lidClosed=true;if(power==2)lidClosed=false;if(power==3)suspended=true;if(power==4)suspended=false;powerTransition(wasBlocked);if(paused())SetEvent(powerAck);}
        std::deque<Command> commands;{std::lock_guard<std::mutex>l(queueLock);commands.swap(queue);}for(auto&c:commands)command(c);
        Sample sample;auto samplingStart=GetTickCount64();
#ifdef QUIET_TESTING
        if(testSample){sample=*testSample;sample.at=now;}else
#endif
        if(demo){sample.valid=true;sample.at=now;sample.raw.fill(0);int wave=static_cast<int>((now-startAt)/2000)%40;sample.raw[0]=48+std::min(wave,40-wave);sample.raw[10]=57;sample.rpm={desired==0?0:2300,desired==0?0:2150};sample.level={desired,desired};}
        else sample=backend.sample(controlOwned?desired:-1);state.sampleDurationMs=static_cast<int>(GetTickCount64()-samplingStart);
        state.at=now;state.healthy=0;state.verified=config.certified()||demo;state.ready=0;state.demo=demo;state.period=config.period;state.display=config.display;state.cpuSensor=config.cpu;state.autoSmart=config.autoSmart;state.controlMask=config.mask;state.manualMinutes=config.manualMinutes;state.manualExit=config.manualExit;for(int i=0;i<12;++i)state.offset[i]=config.offset[i];
        state.lid=lidClosed;state.suspended=suspended;state.controlPaused=paused();state.closedLidWork=config.closedLidWork;
        state.vendor=vendorReader.snapshot();
        if(demo&&sample.valid){int t=sample.raw[config.cpu]-config.offset[config.cpu];state.vendor.cpu=state.vendor.coreMax=t;state.vendor.gpu=std::max(1,t-3);state.vendor.acpi=t;state.vendor.battery=31;state.vendor.storage=44;state.vendor.at=now;state.vendor.valid=1;state.vendor.error=0;}
#ifdef QUIET_TESTING
        if(testVendor)state.vendor=*testVendor;
#endif
        if(sample.readbackConfirmed)event(L"档位单次读回异常，只读复核已确认双风扇目标；继续Smart");
        bool namedFresh=vendorFresh(state.vendor,demo?now:GetTickCount64());
        if(!namedFresh&&controlling())controlFault(L"CPU或GPU保护数据中断",Fault::Sensor,now,sample);
        if(!sample.valid&&namedFresh&&(mode==Mode::Smart||mode==Mode::Manual)&&std::max(state.vendor.coreMax,state.vendor.gpu)>=90)controlFault(L"EC采集中断且部件高温，优先交还BIOS",Fault::Hot,now,sample);
        for(int i=0;i<4;++i){state.enter[i]=config.curve.enter[i+1];state.leave[i]=config.curve.leave[i+1];}
        if(sample.valid){
            bool recovered=bad>0;bad=0;state.sampleAt=sample.at;int max=-1,source=-1,rawMax=-1,rawSource=-1;
            for(int i=0;i<12;++i){state.raw[i]=sample.raw[i];int temp=sample.raw[i]-config.offset[i];if((config.mask&(1<<i))&&temperatureByteValid(sample.raw[i])){if(temp>max&&temp<128){max=temp;source=i;}if(sample.raw[i]>rawMax){rawMax=sample.raw[i];rawSource=i;}}}
            if(namedFresh){if(state.vendor.coreMax>max){max=state.vendor.coreMax;source=12;}if(state.vendor.gpu>max){max=state.vendor.gpu;source=13;}if(state.vendor.coreMax>rawMax){rawMax=state.vendor.coreMax;rawSource=12;}if(state.vendor.gpu>rawMax){rawMax=state.vendor.gpu;rawSource=13;}}
            state.cpu=temperatureByteValid(sample.raw[config.cpu])?sample.raw[config.cpu]-config.offset[config.cpu]:-1;
            state.rpm[0]=sample.rpm[0];state.rpm[1]=sample.rpm[1];state.level[0]=sample.level[0];state.level[1]=sample.level[1];state.source=source;
            bool critical=temperatureByteValid(sample.raw[config.cpu])&&state.cpu>0&&state.cpu<128;
            // Any enabled sensor that previously existed and disappears must not silently leave control.
            for(int i=0;i<12;++i)if((config.mask&(1<<i))&&seen[i]&&!temperatureByteValid(sample.raw[i]))critical=false;
            for(int i=0;i<12;++i)if(temperatureByteValid(sample.raw[i]))seen[i]=true;
            if(critical&&max>=1){++good;state.healthy=1;}else good=0;
            if(recovered&&state.healthy&&mode==Mode::Bios&&!faultLatch)event(L"EC采样已恢复，BIOS控制");
            state.ready=good>=3&&state.verified&&state.healthy&&!paused()&&namedFresh&&firmwareConfirmed;
            if(resuming&&good>=3){
                if(faultLatch||(beforeLid!=Mode::Smart&&beforeLid!=Mode::Shadow))resuming=false;
                else if((beforeLid==Mode::Smart&&state.ready)||(beforeLid==Mode::Shadow&&state.healthy&&!paused())){
                    resuming=false;mode=beforeLid;event(L"连续采样与保护输入恢复，恢复此前策略");
                }
            }
            if(!state.healthy&&controlling())controlFault(L"关键温度失效",Fault::Sensor,now,sample);
            bool safeRecovery=mode==Mode::Bios&&state.ready&&max<=80&&rawMax<=80&&sample.rpm[0]>=0&&sample.rpm[1]>=0&&sample.level[0]==128&&sample.level[1]==128;
            if(recovery.observe(now,safeRecovery)){faultLatch=false;mode=Mode::Smart;policy.reset();event(L"保护数据稳定30秒，自动尝试恢复Smart；仍受高温与写入保护");}
            if(mode==Mode::Commission){state.filtered=max;commissionTick(sample,now);state.suggested=desired;}
            else if(mode==Mode::Smart||mode==Mode::Shadow||mode==Mode::Manual){
                int policyInput=rawMax>=config.curve.enter[4]?std::max(max,rawMax):max;
                if(rawMax>=config.curve.enter[4])state.source=rawSource;
                auto d=policy.step(policyInput,now,state.healthy!=0);state.filtered=d.filtered;state.suggested=d.fan;
                int next=d.fan;
                if constexpr(QUIET_STAGE==1){if(mode!=Mode::Manual){legacySuggested=originalStep(policyInput,legacySuggested);next=legacySuggested;d.reason=L"A版原Smart曲线";state.suggested=next;}}
                if(mode==Mode::Manual){
                    if(std::max(max,rawMax)>config.manualExit||now>=manualDeadline){mode=Mode::Smart;policy.reset();d=policy.step(policyInput,now,true);next=d.fan;event(L"手动时间或温度条件达到，恢复Smart");}
                    else next=max>=90?7:manual;
                }
                next=std::max(1,next);state.suggested=std::max(1,state.suggested);
                if(mode==Mode::Shadow){if(reason!=L"影子建议："+d.reason)event(L"影子建议："+d.reason);}
                else if(controlOwned&&(sample.level[0]!=desired||sample.level[1]!=desired))controlFault(L"硬件读回与请求不一致",Fault::Readback,now,sample);
                else if(next!=desired){
                    if(apply(next)){event(d.reason+L"，目标"+std::to_wstring(next)+L"档");}
                    else controlFault(L"双风扇设置未同时确认",Fault::Write,now,sample);
                }else reason=d.reason;
                if((mode==Mode::Smart||mode==Mode::Manual)&&desired>0&&desired!=128&&now>startedFan+10000){
                    if(sample.rpm[0]<0||sample.rpm[1]<0){++rpmBad;}else rpmBad=0;
                    stall=(sample.rpm[0]==0||sample.rpm[1]==0)?stall+1:0;
                    if(stall>=3||rpmBad>=3)controlFault(L"风扇转速无效或未响应",Fault::Stall,now,sample);
                }
            }else{state.filtered=max;state.suggested=128;}
            Point p{sample.at,state.cpu,max,state.rpm[0],state.rpm[1],desired};
            if(now-lastHistory>=15000||history.empty()){history.push_back(p);lastHistory=now;while(history.size()>120)history.pop_front();}
        }else{
            recovery.observe(now,false);
            if(mode==Mode::Commission)bios(L"档位验证采样失败",true);
            ++bad;good=0;state.cpu=state.filtered=-1;state.rpm[0]=state.rpm[1]=-1;state.level[0]=state.level[1]=-1;for(int&i:state.raw)i=-1;
            if(bad==1)event(sample.error.empty()?L"采样失败，显示数据已失效":sample.error);
            if(bad>=3&&(mode==Mode::Smart||mode==Mode::Manual))controlFault(L"连续3次采样失败",Fault::Sensor,now,sample);
        }
        state.mode=static_cast<int>(mode);state.profile=static_cast<int>(config.profile);state.requested=desired;state.fault=faultLatch;state.manualUntil=manualDeadline;state.resumePending=resuming||recovery.pending();
        textcopy(state.reason,200,reason);textcopy(state.machine,200,machineId());state.eventCount=static_cast<int>(events.size());
        for(int i=0;i<state.eventCount;++i)textcopy(state.events[i],200,events[i]);
        state.historyCount=static_cast<int>(history.size());for(int i=0;i<state.historyCount;++i)state.history[i]=history[i];
        try{journal.push(state);}catch(...){event(L"诊断日志写入失败，控制引擎继续运行");textcopy(state.reason,200,reason);}
        {std::lock_guard<std::mutex>l(stateLock);published=state;}
    }
    bool seen[12]{};int rpmBad=0;State published;
    void loop(){
        startAt=GetTickCount64();bool startup=true;
        try{if(demo)event(L"演示模式：数据为模拟，不访问硬件");else if(bios(L"启动时确认固件接管",true))event(L"默认静音办公，启动后保持BIOS至验证通过");else faultLatch=true;while(running){uint64_t start=GetTickCount64();tick();if(startup&&state.ready){startup=false;if(config.autoSmart&&config.certified()&&!faultLatch)submit({2,2});}
            DWORD elapsed=static_cast<DWORD>(GetTickCount64()-start);DWORD cadence=samplingCadence();DWORD wait=elapsed>=cadence?100:cadence-elapsed;HANDLE waits[]{stopEvent,wakeEvent};WaitForMultipleObjects(2,waits,FALSE,wait);}
        }catch(...){faultLatch=true;try{bios(L"引擎异常，尝试交还BIOS");textcopy(state.reason,200,reason);state.fault=1;state.mode=0;state.healthy=state.ready=0;state.cpu=state.filtered=-1;std::lock_guard<std::mutex>l(stateLock);published=state;}catch(...){}running=false;}
        if(controlOwned||backend.wrote)try{bios(L"控制引擎停止");}catch(...){}
        {std::lock_guard<std::mutex>l(stateLock);published.alive=0;published.ready=0;}if(exitedEvent)SetEvent(exitedEvent);
    }
public:
    Engine(Config c,fs::path path,bool demoMode=false,bool svc=false,fs::path journalDir={}):config(std::move(c)),cfgPath(std::move(path)),journal(journalDir.empty()?(demoMode?dataDir(svc)/L"demo":dataDir(svc)):journalDir),demo(demoMode),service(svc),firmwareConfirmed(demoMode){policy.curve=config.curve;policy.profile=config.profile;for(int&i:state.raw)i=-1;published=state;}
    bool start(){
        auto mutexName=demo?L"Local\\TPFCQuiet.Demo."+std::to_wstring(GetCurrentProcessId()):std::wstring(L"Global\\TPFCQuiet.Engine");
        instance=CreateMutexW(nullptr,TRUE,mutexName.c_str());if(!instance||GetLastError()==ERROR_ALREADY_EXISTS)return false;
        if(!demo){originalMutex=CreateMutexW(nullptr,FALSE,L"TPFanControlMutex01");if(!originalMutex)return false;DWORD r=WaitForSingleObject(originalMutex,0);if(r!=WAIT_OBJECT_0&&r!=WAIT_ABANDONED)return false;}
        stopEvent=CreateEventW(nullptr,TRUE,FALSE,nullptr);wakeEvent=CreateEventW(nullptr,FALSE,FALSE,nullptr);powerAck=CreateEventW(nullptr,TRUE,FALSE,nullptr);exitedEvent=CreateEventW(nullptr,TRUE,FALSE,nullptr);if(!stopEvent||!wakeEvent||!powerAck||!exitedEvent)return false;if(!demo)vendorReader.start(service);running=true;worker=std::thread([this]{loop();});return true;
    }
    void stop(){running=false;if(stopEvent)SetEvent(stopEvent);if(worker.joinable())worker.join();vendorReader.stop();if(instance){ReleaseMutex(instance);CloseHandle(instance);instance=nullptr;}if(originalMutex){ReleaseMutex(originalMutex);CloseHandle(originalMutex);originalMutex=nullptr;}}
    ~Engine(){stop();if(stopEvent)CloseHandle(stopEvent);if(wakeEvent)CloseHandle(wakeEvent);if(powerAck)CloseHandle(powerAck);if(exitedEvent)CloseHandle(exitedEvent);if(instance){ReleaseMutex(instance);CloseHandle(instance);}if(originalMutex){ReleaseMutex(originalMutex);CloseHandle(originalMutex);}}
    HANDLE exited()const{return exitedEvent;}
    State snapshot()const{std::lock_guard<std::mutex>l(stateLock);return published;}
    void submit(Command c){std::lock_guard<std::mutex>l(queueLock);if(queue.size()<32)queue.push_back(std::move(c));if(wakeEvent)SetEvent(wakeEvent);}
    void power(bool closing,bool lid=false){if(!running)return;if(closing)ResetEvent(powerAck);{std::lock_guard<std::mutex>l(powerLock);int e=lid?(closing?1:2):(closing?3:4);if(powerEvents.empty()||powerEvents.back()!=e)powerEvents.push_back(e);}SetEvent(wakeEvent);if(closing&&!lid)WaitForSingleObject(powerAck,2000);}
};
}
