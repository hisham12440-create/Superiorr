#pragma once
#include "core/config.h"
#include "core/performance_history.h"
#include "present/session_stats.h"
#include <optional>
#include <filesystem>
namespace superior {
enum class FailureStage { None, StartCapture, Runtime, UserInterface };
struct DiagnosticsSnapshot {
    std::string adapter, vendor;
    uint64_t dedicatedBytes=0, sharedBytes=0;
    std::optional<uint64_t> usageBytes, budgetBytes, allowanceBytes;
    std::optional<double> gpuGroupMs;
    std::optional<bool> onBattery, batterySaver, captureSupported;
    std::optional<int32_t> deviceRemovedReason;
    uint32_t featureLevel=0;
    bool running=false, paused=false, captureActive=false, safeMode=false, generationAllowed=false;
    FailureStage lastFailure=FailureStage::None;
    PerformanceHistory performance;
    double collectedNowMs=0;
    Settings settings;
    present::SessionSnapshot session;
};
struct DiagnosticOptions { bool includeSettings=false, includeSession=false; };
const char* BuildRevision();
const char* FailureStageName(FailureStage stage);
std::string DiagnosticsJson(const DiagnosticsSnapshot& snapshot, DiagnosticOptions options = {});
bool SaveDiagnosticsReport(const std::filesystem::path& path,const DiagnosticsSnapshot& snapshot,DiagnosticOptions options = {});
}
