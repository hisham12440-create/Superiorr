#pragma once
#include <d3d11.h>
#include <dxgi1_4.h>
#include <wrl/client.h>
#include <string>
#include "hardware/policy.h"
#include "core/performance_history.h"
namespace superior::hardware {
struct Snapshot {
    std::string adapter;
    std::string vendor;
    std::string reason = "No GPU timing sample yet";
    Tier tier = Tier::Low;
    uint64_t dedicated = 0, shared = 0, usage = 0, budget = 0, available = 0;
    bool memoryKnown = false, powerKnown = false, acKnown = false, battery = false, saver = false;
    bool timingKnown = false;
    double gpuMs = 0, targetMs = 0;
    bool allowGeneration = true;
};
class Monitor {
public:
    void Init(ID3D11Device* device);
    void Poll(double nowMs, Mode requested, double displayInterval);
    const Snapshot& Data() const { return snapshot_; }
    void RecordGpuTime(const GpuSample& sample,double collectedMs) {
        if(!history_.Add(sample,collectedMs)) return;
        pendingMaximum_=freshTiming_ ? std::max(pendingMaximum_,sample.milliseconds) : sample.milliseconds;
        snapshot_.gpuMs=pendingMaximum_; snapshot_.timingKnown=true; freshTiming_=true;
    }
    const PerformanceHistory& History() const { return history_; }
    void ResetTimings() { history_.Clear(); governor_.Reset(); freshTiming_=false; pendingMaximum_=0; snapshot_.gpuMs=0; snapshot_.timingKnown=false; }
private:
    Microsoft::WRL::ComPtr<IDXGIAdapter3> adapter_;
    Snapshot snapshot_;
    Governor governor_;
    double nextPoll_ = 0;
    bool freshTiming_ = false;
    double pendingMaximum_=0;
    PerformanceHistory history_;
};
}
