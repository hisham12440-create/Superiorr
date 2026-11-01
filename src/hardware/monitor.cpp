#include "hardware/monitor.h"
#include <windows.h>
#include "core/log.h"
namespace superior::hardware {
void Monitor::Init(ID3D11Device* device) {
    Microsoft::WRL::ComPtr<IDXGIDevice> dxgi;
    if (FAILED(device->QueryInterface(IID_PPV_ARGS(&dxgi)))) return;
    const HRESULT priority = dxgi->SetGPUThreadPriority(-1);
    if (FAILED(priority)) SERR("GPU priority unavailable (0x%08X)", static_cast<unsigned>(priority));
    Microsoft::WRL::ComPtr<IDXGIAdapter> adapter;
    if (FAILED(dxgi->GetAdapter(&adapter))) return;
    DXGI_ADAPTER_DESC desc{};
    if (FAILED(adapter->GetDesc(&desc))) return;
    const int count = WideCharToMultiByte(CP_UTF8, 0, desc.Description, -1, nullptr, 0, nullptr, nullptr);
    if (count > 1) {
        snapshot_.adapter.resize(static_cast<size_t>(count));
        WideCharToMultiByte(CP_UTF8, 0, desc.Description, -1, snapshot_.adapter.data(), count, nullptr, nullptr);
        snapshot_.adapter.pop_back();
    }
    snapshot_.vendor = desc.VendorId == 0x10DE ? "NVIDIA" : desc.VendorId == 0x1002 ? "AMD" : desc.VendorId == 0x8086 ? "Intel" : "Other";
    snapshot_.dedicated = desc.DedicatedVideoMemory;
    snapshot_.shared = desc.SharedSystemMemory;
    snapshot_.tier = SelectTier(snapshot_.dedicated);
    if (FAILED(adapter.As(&adapter_))) SLOG("DXGI memory budget unavailable");
}
void Monitor::Poll(double nowMs, Mode requested, double displayInterval) {
    if (nowMs >= nextPoll_) {
        nextPoll_ = nowMs + 1000;
        SYSTEM_POWER_STATUS power{};
        snapshot_.powerKnown = GetSystemPowerStatus(&power) != FALSE;
        snapshot_.battery = snapshot_.powerKnown && power.ACLineStatus == 0;
        snapshot_.saver = snapshot_.powerKnown && power.SystemStatusFlag == 1;
        DXGI_QUERY_VIDEO_MEMORY_INFO info{};
        snapshot_.memoryKnown = adapter_ && SUCCEEDED(adapter_->QueryVideoMemoryInfo(0, DXGI_MEMORY_SEGMENT_GROUP_LOCAL, &info));
        if (snapshot_.memoryKnown) {
            snapshot_.budget = info.Budget;
            snapshot_.usage = info.CurrentUsage;
            snapshot_.available = AvailableBudget(info.Budget, info.CurrentUsage, snapshot_.tier);
        }
    }
    // Never loosen Comfort when on battery. Cap Max to Balanced on battery.
    const Mode effective = EffectiveMode(requested, snapshot_.battery, snapshot_.saver);
    snapshot_.targetMs = BudgetMs(effective, displayInterval);
    if (freshTiming_) {
        governor_.Update(snapshot_.gpuMs, snapshot_.targetMs, nowMs);
        freshTiming_ = false;
    }
    const bool memoryPressure = snapshot_.memoryKnown && snapshot_.available < 32 * MiB;
    snapshot_.allowGeneration = PowerAllowsGeneration(snapshot_.battery, snapshot_.saver) && !memoryPressure && governor_.Enabled();
    snapshot_.reason = snapshot_.battery || snapshot_.saver ? "Power policy: interpolation disabled" : memoryPressure ? "VRAM headroom below 32 MiB: interpolation disabled" : !governor_.Enabled() ? "GPU over budget; waiting for 5 seconds stable headroom" : "Optional interpolation allowed (quality unverified)";
}
}
