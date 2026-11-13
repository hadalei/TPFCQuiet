#pragma once
#include <cstdint>
#include <deque>
namespace quiet {
// No hardware access. Attempts are shared across fault episodes and manual
// re-enables; cancelling an intent never replenishes the rolling budget.
class SmartRecovery {
public:
    enum class Kind { Sensor, Write };
    static constexpr uint64_t StableMs=30000,WindowMs=3600000,MaxGapMs=15000;
private:
    struct Attempt {uint64_t at;Kind kind;};
    std::deque<Attempt> attempts;
    bool waiting=false,stable=false;Kind kind=Kind::Sensor;
    uint64_t armedAt=0,stableAt=0,last=0;
    void prune(uint64_t now){while(!attempts.empty()&&now>=attempts.front().at&&now-attempts.front().at>=WindowMs)attempts.pop_front();}
    bool available(Kind k)const{if(attempts.size()>=3)return false;if(k==Kind::Write)for(auto&a:attempts)if(a.kind==Kind::Write)return false;return true;}
public:
    bool pending()const{return waiting;}
    void cancel(){waiting=stable=false;}
    bool arm(Kind k,uint64_t now){cancel();prune(now);if(now<last||!available(k))return false;kind=k;armedAt=last=now;waiting=true;return true;}
    bool observe(uint64_t now,bool safe){
        if(!waiting)return false;
        if(now<last){cancel();return false;}if(now-last>MaxGapMs)stable=false;last=now;prune(now);
        if(!safe){stable=false;return false;}
        if(!stable){stable=true;stableAt=now;}
        if(now-armedAt<StableMs||now-stableAt<StableMs)return false;
        if(!available(kind)){cancel();return false;}
        attempts.push_back({now,kind});cancel();return true;
    }
};
}
