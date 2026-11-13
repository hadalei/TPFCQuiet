#pragma once
#include <algorithm>
#include <array>
#include <cstdint>
#include <deque>
#include <string>

namespace quiet {
enum class Profile { Quiet=0, Balanced=1, Cooling=2 };
enum class Mode { Bios=0, Shadow=1, Smart=2, Manual=3, Commission=4 };
// EC 0x31 is shared with unrelated firmware flags. Only bit 0 selects the fan.
constexpr int fanSelectionValue(int current,int fan){return (current&0xfe)|(fan&1);}
struct Curve {
    std::array<int,5> enter{0,60,70,80,90};
    std::array<int,5> leave{0,50,65,75,85};
    std::array<int,5> fan{0,1,2,4,7};
    bool valid() const {
        if(fan[0]!=0 || fan[4]!=7 || enter[4]>90 || enter[4]<80) return false;
        for(int i=1;i<5;++i) if(enter[i]<=enter[i-1] || leave[i]>=enter[i] || leave[i]<enter[i-1] || fan[i]<=fan[i-1]) return false;
        return true;
    }
};
struct Decision { int fan=128; int filtered=-1; std::wstring reason=L"BIOS控制"; };
class Policy {
    std::deque<int> recent;
    uint64_t sinceUp=0, sinceDown=0, last=0;
    int pendingUp=-1, pendingDown=-1, highCount=0;
    bool initialized=false;
public:
    Curve curve;
    Profile profile=Profile::Quiet;
    int index=0;
    void reset(){recent.clear(); sinceUp=sinceDown=last=0; pendingUp=pendingDown=-1;highCount=0;index=0;initialized=false;}
    Decision step(int raw, uint64_t now, bool valid=true){
        Decision d;
        if(!valid || raw<1 || raw>127 || !curve.valid()) { reset();d.reason=L"温度失效，交还BIOS";return d; }
        if(initialized && now<last){reset();}
        bool first=!initialized;
        initialized=true;last=now;
        recent.push_back(raw);if(recent.size()>3)recent.pop_front();
        auto sorted=recent;std::sort(sorted.begin(),sorted.end());
        d.filtered= profile==Profile::Cooling ? raw : sorted[sorted.size()/2];
        if(raw>=curve.enter[4]){
            index=4;pendingUp=pendingDown=-1;highCount=0;
            d.fan=7;d.reason=L"高温快速响应，直接请求7档";return d;
        }
        // A fresh window must never request zero at an already warm start.
        if(first){for(int i=1;i<5;++i)if(raw>=curve.enter[i])index=i;if(profile==Profile::Cooling)index=std::max(index,1);d.fan=curve.fan[index];d.reason=L"根据当前有效温度初始化档位";return d;}
        highCount=raw>=curve.enter[3]?highCount+1:0;
        if(highCount>=2 && index<3){
            index=3;pendingUp=pendingDown=-1;
            d.fan=curve.fan[index];d.reason=L"持续高温，快速升至4档";return d;
        }
        int target=0;for(int i=1;i<5;++i)if(d.filtered>=curve.enter[i])target=i;
        if(profile==Profile::Cooling)target=std::max(target,1);
        if(target>index){
            pendingDown=-1;
            if(pendingUp!=index+1){pendingUp=index+1;sinceUp=now;}
            auto wait=profile==Profile::Quiet?4000ULL:profile==Profile::Balanced?2000ULL:0ULL;
            if(now-sinceUp>=wait){index=profile==Profile::Cooling?target:index+1;pendingUp=-1;d.reason=L"持续升温，提升一个有效档位";}
            else d.reason=L"短时温度波动，确认持续升温";
        }else{
            pendingUp=-1;
            int lower=index;
            while(lower>0 && d.filtered<=curve.leave[lower])--lower;
            if(profile==Profile::Cooling)lower=std::max(lower,1);
            if(lower<index){
                if(pendingDown!=lower){pendingDown=lower;sinceDown=now;}
                uint64_t wait=profile==Profile::Cooling?10000:6000;
                if(now-sinceDown>=wait){index=lower;pendingDown=-1;d.reason=L"温度持续下降，降低风扇档位";}
                else d.reason=L"正在确认冷却，避免反复升降";
            }else{pendingDown=-1;d.reason=L"温度稳定，保持当前档位";}
        }
        d.fan=curve.fan[index];return d;
    }
};
inline int originalStep(int temp,int fan){
    int next=-1;const int t[]={50,60,70,80,90},f[]={0,1,2,4,7};
    for(int i=0;i<5;++i)if(temp>=t[i]&&f[i]>=fan){next=f[i];}
    if(next<0)for(int i=0;i<5;++i)if(temp<=t[i]&&f[i]<fan){next=f[i];break;}
    return next<0?fan:next;
}
inline bool twoFansMatch(int requested,int first,int second,bool firstRead,bool secondRead){
    return firstRead&&secondRead&&first==requested&&second==requested;
}
inline bool temperatureByteValid(int raw){return raw>0&&raw<128;}
}
