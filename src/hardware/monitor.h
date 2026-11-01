#pragma once
#include <d3d11.h>
#include <dxgi1_4.h>
#include <wrl/client.h>
#include <string>
#include "hardware/policy.h"
namespace superior::hardware {
struct Snapshot {
    std::string adapter;
    std::string vendor;
    std::string reason = "No GPU timing sample yet";
    Tier tier = Tier::Low;
    uint64_t dedicated = 0, shared = 0, usage = 0, budget = 0, available = 0;
    bool memoryKnown = false, powerKnown = false, battery = false, saver = false;
    bool timingKnown = false;
    double gpuMs = 0, targetMs = 0;
    bool allowGeneration = true;
};
class Monitor {
public:
    void Init(ID3D11Device* device);
    void Poll(double nowMs, Mode requested, double displayInterval);
    const Snapshot& Data() const { return snapshot_; }
    void RecordGpuTime(double ms) { snapshot_.gpuMs = ms; snapshot_.timingKnown = true; freshTiming_ = true; }
private:
    Microsoft::WRL::ComPtr<IDXGIAdapter3> adapter_;
    Snapshot snapshot_;
    Governor governor_;
    double nextPoll_ = 0;
    bool freshTiming_ = false;
};
}
