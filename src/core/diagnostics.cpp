#include "diagnostics.h"
#include "core/files.h"
#include "core/settings_bundle.h"
#include <nlohmann/json.hpp>
#include <cmath>
#include <stdexcept>
#ifndef SUPERIOR_BUILD_REVISION
#define SUPERIOR_BUILD_REVISION "unknown"
#endif
namespace superior {
const char* BuildRevision() { return SUPERIOR_BUILD_REVISION; }
const char* FailureStageName(FailureStage stage) {
    switch(stage) {
        case FailureStage::None:return "none";
        case FailureStage::StartCapture:return "capture_start";
        case FailureStage::Runtime:return "runtime";
        case FailureStage::UserInterface:return "ui";
    }
    return "unknown";
}
namespace {
template<class T> nlohmann::json Value(const std::optional<T>& value) {
    return value ? nlohmann::json(*value) : nlohmann::json(nullptr);
}
}
std::string DiagnosticsJson(const DiagnosticsSnapshot& s,DiagnosticOptions options) {
    using nlohmann::json;
    json groupMs=nullptr;
    if(s.gpuGroupMs && std::isfinite(*s.gpuGroupMs) && *s.gpuGroupMs>=0) groupMs=*s.gpuGroupMs;
    json session=nullptr;
    std::string sessionStatus=options.includeSession ? "unavailable" : "not_requested";
    if(options.includeSession && s.session.available) {
        try { session=json::parse(present::SessionJson(s.session)); sessionStatus="included"; }
        catch(const std::invalid_argument&) { sessionStatus="invalid_snapshot_omitted"; }
    }
    json windows=json::object();
    for(int index=0;index<2;++index) {
        const auto summary=s.performance.Describe(static_cast<GpuScope>(index),s.collectedNowMs);
        const char* key=index==0 ? "capture_processing" : "final_rendering";
        windows[key]=summary ? json{{"samples",summary->count},{"mean_ms",summary->mean},{"p95_ms",summary->p95},
            {"maximum_ms",summary->maximum},{"collected_age_ms",summary->ageMs},{"stale",summary->stale}} : json(nullptr);
    }
    json result={
        {"format","superior-diagnostics"},{"schema",1},{"build_revision",BuildRevision()},
        {"privacy",{{"contains_images",false},{"contains_game_paths",false},{"contains_logs",false},{"uploaded_automatically",false}}},
        {"runtime",{{"running",s.running},{"paused",s.paused},{"capture_active",s.captureActive},
            {"wgc_supported_probe",Value(s.captureSupported)},{"safe_mode",s.safeMode},
            {"generation_policy_allows",s.generationAllowed},{"last_failure_stage",FailureStageName(s.lastFailure)}}},
        {"gpu",{{"adapter",s.adapter},{"vendor",s.vendor},{"feature_level",s.featureLevel ? json(s.featureLevel) : json(nullptr)},
            {"device_removed_reason_hresult",Value(s.deviceRemovedReason)},
            {"dedicated_bytes",s.dedicatedBytes},{"shared_bytes",s.sharedBytes},
            {"process_local_usage_bytes",Value(s.usageBytes)},{"local_budget_bytes",Value(s.budgetBytes)},
            {"extra_allowance_policy_bytes",Value(s.allowanceBytes)},{"last_completed_group_ms",groupMs},
            {"driver_version",nullptr},{"whole_frame_gpu_ms",nullptr},{"temperature_c",nullptr},{"watts",nullptr}}},
        {"gpu_group_sample_windows",windows},
        {"power",{{"on_battery",Value(s.onBattery)},{"battery_saver",Value(s.batterySaver)}}},
        {"settings_bundle",options.includeSettings ? json::parse(ExportSettingsBundle(s.settings)) : json(nullptr)},
        {"session_status",sessionStatus},{"session",session},
        {"limits","Hardware identity is included. WGC support is not game compatibility or anti-cheat certification. GPU group time is not whole-frame time. No automatic device-loss recovery. No scan-out FPS, quality score or latency measurement."}
    };
    return result.dump(2);
}
bool SaveDiagnosticsReport(const std::filesystem::path& path,const DiagnosticsSnapshot& s,DiagnosticOptions options) {
    try { return WriteTextAtomic(path,DiagnosticsJson(s,options)); }
    catch(const std::exception&) { return false; }
}
}
