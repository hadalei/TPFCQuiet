#pragma once
#include "platform.hpp"
namespace quiet {
struct Sample {
    uint64_t at=0;std::array<int,12> raw{};std::array<int,2> rpm{-1,-1},level{-1,-1};
    bool valid=false;std::wstring error;int firstLevel[2]{-1,-1};bool readbackConfirmed=false;
    Sample(){raw.fill(-1);}
};
struct FanWriteEvidence {
    int target=-1,attempts=0,back[2]{-1,-1};
    bool selected[2]{},written[2]{},read[2]{};
    DWORD durationMs=0;std::wstring step=L"not_started",firstFailure;int status=-1;
};
// EC address conventions are inherited from Shuzhengz/TPFanCtrl2 tag 2.3.3.
// Profile certification is still required before any fan-control write.
class Backend {
#ifdef QUIET_TESTING
    friend struct BackendTestAccess;
#endif
    HMODULE dll=nullptr;HANDLE lock=nullptr;WORD ctrl=0x1604,data=0x1600;
    using Open=BOOL(__stdcall*)();using Close=void(__stdcall*)();using Read=UCHAR(__stdcall*)(USHORT);using Write=void(__stdcall*)(USHORT,UCHAR);
    Open openPort=nullptr;Close closePort=nullptr;Read readPort=nullptr;Write writePort=nullptr;
    bool opened=false,locked=false,validatedPort=false;uint64_t deadline=0;std::wstring operation,lastFailure;int lastStatus=-1;
    bool flags(int mask,bool set=false){while(GetTickCount64()<deadline){lastStatus=readPort(ctrl);if(((lastStatus&mask)!=0)==set)return true;Sleep(2);}if(lastFailure.empty())lastFailure=operation;return false;}
    bool read(int offset,int&out){operation=L"read_"+std::to_wstring(offset);if(!flags(3))return false;writePort(ctrl,0x80);if(!flags(2))return false;writePort(data,static_cast<UCHAR>(offset));if(!flags(2)||!flags(1,true))return false;out=readPort(data);return true;}
    bool write(int offset,int value){operation=L"write_"+std::to_wstring(offset);if(!flags(3))return false;writePort(ctrl,0x81);if(!flags(2))return false;writePort(data,static_cast<UCHAR>(offset));if(!flags(2))return false;writePort(data,static_cast<UCHAR>(value));return flags(2);}
    bool select(int fan){int current=-1;return read(0x31,current)&&write(0x31,fanSelectionValue(current,fan));}
    bool acquire(){if(!opened)return false;DWORD r=WaitForSingleObject(lock,150);locked=r==WAIT_OBJECT_0||r==WAIT_ABANDONED;deadline=GetTickCount64()+1200;return locked;}
    void release(){if(locked){ReleaseMutex(lock);locked=false;}}
    Sample frame(){
        Sample s;bool ok=true;
        for(int f=1;f>=0;--f){int lo=-1,hi=-1;ok=ok&&select(f)&&read(0x2f,s.level[f])&&read(0x84,lo)&&read(0x85,hi);if(ok){int rpm=(hi<<8)|lo;s.rpm[f]=rpm<=8191?rpm:-1;}}
        for(int i=0;i<12&&ok;++i)ok=read(i<8?0x78+i:0xc0+i-8,s.raw[i]);
        bool restored=select(0);s.valid=ok&&restored;return s;
    }
public:
    std::wstring error;bool wrote=false;FanWriteEvidence lastWrite;
    bool connect(){
        if(opened)return true;
        if(originalRunning()){error=L"原版TPFanControl仍在运行，避免并行访问";return false;}
        wchar_t win[MAX_PATH];GetWindowsDirectoryW(win,MAX_PATH);
        fs::path p=fs::path(win)/L"System"/L"TVicPort.dll";
        if(!fs::exists(p)){error=L"未找到系统TVicPort.dll，未更改驱动";return false;}
        dll=LoadLibraryExW(p.c_str(),nullptr,LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR|LOAD_LIBRARY_SEARCH_SYSTEM32);
        if(!dll){error=L"TVicPort加载失败";return false;}
        openPort=reinterpret_cast<Open>(GetProcAddress(dll,"OpenTVicPort"));closePort=reinterpret_cast<Close>(GetProcAddress(dll,"CloseTVicPort"));readPort=reinterpret_cast<Read>(GetProcAddress(dll,"ReadPort"));writePort=reinterpret_cast<Write>(GetProcAddress(dll,"WritePort"));
        if(!openPort||!closePort||!readPort||!writePort||!openPort()){error=L"无法打开EC驱动，需要管理员权限或检查驱动";FreeLibrary(dll);dll=nullptr;return false;}
        opened=true;lock=CreateMutexW(nullptr,FALSE,L"Access_Thinkpad_EC");if(!lock){error=L"EC互斥创建失败";closePort();FreeLibrary(dll);dll=nullptr;opened=false;return false;}return true;
    }
    Sample sample(int expected=-1){
        Sample s;if(!connect()){s.error=error;return s;}if(originalRunning()){s.error=L"检测到原版控制器，暂停采集";return s;}
        if(!acquire()){s.error=L"EC访问忙";return s;}
        uint64_t totalDeadline=deadline;
        // Retry a transient busy transaction on the same port, without extending
        // the original 1200 ms sampling budget or publishing a partial frame.
        for(int attempt=0;attempt<2&&GetTickCount64()<totalDeadline;++attempt){
            deadline=attempt?totalDeadline:std::min<uint64_t>(totalDeadline,GetTickCount64()+200);
            s=frame();
            // Recheck an isolated control mismatch without rewriting a fan. Any
            // firmware/high-bit indication is preserved for immediate handoff.
            if(s.valid&&expected>=0&&(s.level[0]!=expected||s.level[1]!=expected)&&s.level[0]<128&&s.level[1]<128){
                s.firstLevel[0]=s.level[0];s.firstLevel[1]=s.level[1];
                deadline=totalDeadline;std::array<int,2> confirm{-1,-1};
                bool ok=select(1)&&read(0x2f,confirm[1])&&select(0)&&read(0x2f,confirm[0]);
                bool restored=select(0);
                if(ok&&restored&&confirm[0]==expected&&confirm[1]==expected){s.level=confirm;s.readbackConfirmed=true;}
                // A persistent mismatch remains visible; no write or takeover retry.
            }
            if(s.valid){validatedPort=true;break;}
            if(!attempt&&GetTickCount64()+20<totalDeadline)Sleep(20);
        }
        release();s.at=GetTickCount64();if(!s.valid){s.error=L"EC采集超时或读取失败（端口"+std::wstring(ctrl==0x1604?L"0x1604":L"0x66")+L"）";
            // Port fallback is initial discovery only. A transient runtime error
            // must not move sampling or the subsequent BIOS handoff to another port.
            if(!validatedPort){ctrl=ctrl==0x1604?0x66:0x1604;data=ctrl==0x1604?0x1600:0x62;}}
        return s;
    }
    bool set(int target,std::array<int,2>&back){
        auto started=GetTickCount64();lastWrite=FanWriteEvidence{};lastWrite.target=target;back={-1,-1};
        auto finish=[&](bool ok){lastWrite.durationMs=static_cast<DWORD>(GetTickCount64()-started);return ok;};
        if(!(target==0||target==1||target==2||target==4||target==7||target==128)){lastWrite.step=L"invalid_target";return finish(false);}
        if(originalRunning()){lastWrite.step=L"other_controller";return finish(false);}
        if(!connect()){lastWrite.step=L"connect";return finish(false);}
        if(!acquire()){lastWrite.step=L"mutex_busy";return finish(false);}
        bool ok=false;uint64_t totalDeadline=deadline;lastFailure.clear();
        // Reserve 250 ms for selector cleanup even when a fan operation stalls.
        for(int attempt=0;attempt<2&&GetTickCount64()+250<totalDeadline;++attempt){
            deadline=std::min<uint64_t>(totalDeadline-250,GetTickCount64()+400);
            bool good[2]{false,false};lastWrite.attempts=attempt+1;
            for(int fan=0;fan<2;++fan){lastWrite.selected[fan]=select(fan);lastWrite.written[fan]=lastWrite.selected[fan]&&write(0x2f,target);wrote=wrote||lastWrite.written[fan];if(lastWrite.written[fan])Sleep(50);good[fan]=lastWrite.read[fan]=lastWrite.written[fan]&&read(0x2f,back[fan]);lastWrite.back[fan]=good[fan]?back[fan]:-1;}
            ok=twoFansMatch(target,back[0],back[1],good[0],good[1]);if(ok)break;
        }
        deadline=totalDeadline;bool restored=select(0);lastWrite.firstFailure=lastFailure;lastWrite.status=lastStatus;lastWrite.step=!restored?L"restore_selector":ok?L"confirmed":L"write_or_readback";release();return finish(ok&&restored);
    }
    ~Backend(){release();if(opened)closePort();if(lock)CloseHandle(lock);if(dll)FreeLibrary(dll);}
};
}
