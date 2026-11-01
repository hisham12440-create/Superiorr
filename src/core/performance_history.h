#pragma once
#include <array>
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <optional>
namespace superior {
enum class GpuScope { CaptureProcessing=0, FinalRendering=1 };
struct GpuSample { uint64_t sequence=0; GpuScope scope=GpuScope::CaptureProcessing; double milliseconds=0; };
class PerformanceHistory {
public:
    static constexpr size_t Capacity=128;
    struct Entry { double milliseconds=0, collectedMs=0; uint64_t sequence=0; };
    struct Summary { size_t count=0; double mean=0, maximum=0, p95=0, ageMs=0; bool stale=false; };
    bool Add(const GpuSample& sample,double collectedMs) {
        const auto scope=static_cast<size_t>(sample.scope);
        if(scope>=2 || !std::isfinite(sample.milliseconds) || sample.milliseconds<0 ||
            !std::isfinite(collectedMs) || collectedMs<0 || sample.sequence==0 ||
            (lastSequence_ && sample.sequence<=lastSequence_) || (haveTime_ && collectedMs<lastTime_)) return false;
        auto& ring=rings_[scope];
        ring.entries[ring.next]={sample.milliseconds,collectedMs,sample.sequence};
        ring.next=(ring.next+1)%Capacity; ring.count=std::min(Capacity,ring.count+1);
        lastSequence_=sample.sequence; lastTime_=collectedMs; haveTime_=true; return true;
    }
    size_t Count(GpuScope scope) const { return rings_.at(static_cast<size_t>(scope)).count; }
    Entry At(GpuScope scope,size_t chronologicalIndex) const {
        const auto& ring=rings_.at(static_cast<size_t>(scope));
        if(chronologicalIndex>=ring.count) return {};
        return ring.entries[(ring.next+Capacity-ring.count+chronologicalIndex)%Capacity];
    }
    std::optional<Summary> Describe(GpuScope scope,double nowMs) const {
        if(!std::isfinite(nowMs)) return std::nullopt;
        const auto count=Count(scope); if(!count) return std::nullopt;
        std::array<double,Capacity> sorted{}; Summary result; result.count=count;
        for(size_t i=0;i<count;++i) { sorted[i]=At(scope,i).milliseconds; result.mean+=(sorted[i]-result.mean)/static_cast<double>(i+1); }
        std::sort(sorted.begin(),sorted.begin()+count);
        result.maximum=sorted[count-1]; result.p95=sorted[static_cast<size_t>(std::ceil(.95*count))-1];
        result.ageMs=std::max(0.,nowMs-At(scope,count-1).collectedMs); result.stale=result.ageMs>2000;
        return result;
    }
    void Clear() { *this=PerformanceHistory{}; }
private:
    struct Ring { std::array<Entry,Capacity> entries{}; size_t next=0,count=0; };
    std::array<Ring,2> rings_{};
    uint64_t lastSequence_=0; double lastTime_=0; bool haveTime_=false;
};
}
