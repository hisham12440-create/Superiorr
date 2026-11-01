#include "panel.h"
#include "localized.h"
#include <commdlg.h>
#include <algorithm>
#include <vector>
namespace superior {
void Panel::DrawDiagnostics(bool running,bool paused,const hardware::Snapshot& hw,bool safeMode) {
    DiagnosticsSnapshot snapshot;
    snapshot.adapter=hw.adapter; snapshot.vendor=hw.vendor;
    snapshot.dedicatedBytes=hw.dedicated; snapshot.sharedBytes=hw.shared;
    if(hw.memoryKnown) {
        snapshot.usageBytes=hw.usage; snapshot.budgetBytes=hw.budget; snapshot.allowanceBytes=hw.available;
    }
    if(hw.powerKnown) snapshot.batterySaver=hw.saver;
    if(hw.acKnown) snapshot.onBattery=hw.battery;
    if(hw.timingKnown) snapshot.gpuGroupMs=hw.gpuMs;
    snapshot.running=running; snapshot.paused=paused; snapshot.captureActive=captureActive_;
    snapshot.captureSupported=captureSupported_; snapshot.safeMode=safeMode; snapshot.generationAllowed=hw.allowGeneration;
    snapshot.lastFailure=lastFailure_; snapshot.settings=*settings_; snapshot.session=session_;
    Microsoft::WRL::ComPtr<ID3D11Device> device;
    context_->GetDevice(&device);
    if(device) {
        snapshot.featureLevel=static_cast<uint32_t>(device->GetFeatureLevel());
        snapshot.deviceRemovedReason=static_cast<int32_t>(device->GetDeviceRemovedReason());
    }
    localized::TextWrapped("Local diagnostics only. No report is uploaded automatically. Adapter identity is included; game paths, screenshots and raw logs are excluded.");
    localized::TextWrapped("Build revision: %s",BuildRevision());
    localized::TextWrapped("%s | %s",hw.adapter.c_str(),hw.vendor.c_str());
    if(snapshot.deviceRemovedReason) {
        localized::Text("Device HRESULT: 0x%08X",static_cast<unsigned>(*snapshot.deviceRemovedReason));
        localized::TextWrapped(*snapshot.deviceRemovedReason==0 ? "Device reports S_OK; this is not a full stability test" : "Device reported a failure; stop capture and restart Superior");
    }
    if(captureSupported_.has_value()) localized::TextWrapped(*captureSupported_ ? "WGC API support: yes (not a game compatibility guarantee)" : "WGC API support: no");
    else localized::TextWrapped("WGC API support: unknown (probe failed)");
    localized::TextWrapped(captureActive_ ? "Capture session: active" : "Capture session: inactive");
    localized::TextWrapped("Last failure stage: %s",FailureStageName(lastFailure_));
    localized::TextWrapped("Driver version, total GPU time, temperature, watts and scan-out FPS are not measured. Missing report values are null.");
    localized::SeparatorText("Report contents");
    localized::Checkbox("Include portable image and session settings",&diagnosticOptions_.includeSettings);
    localized::Checkbox("Include session observations",&diagnosticOptions_.includeSession);
    localized::TextWrapped("Preview the report before sharing. Settings exclude language, hotkeys and local comparison layout. Session observations are API counters, not displayed FPS or quality scores.");
    const auto text=DiagnosticsJson(snapshot,diagnosticOptions_);
    if(localized::CollapsingHeader("Preview diagnostic JSON")) {
        std::vector<char> buffer(text.begin(),text.end()); buffer.push_back('\0');
        ImGui::InputTextMultiline("##DiagnosticPreview",buffer.data(),buffer.size(),ImVec2(-1,260),ImGuiInputTextFlags_ReadOnly);
    }
    if(localized::Button("Copy diagnostic JSON")) {
        ImGui::SetClipboardText(text.c_str()); diagnosticsStatus_="Diagnostic JSON copied to clipboard";
    }
    ImGui::BeginDisabled(OfflineBusy());
    if(localized::Button("Save diagnostic JSON")) ExportDiagnostics(snapshot,diagnosticOptions_);
    ImGui::EndDisabled();
    if(!diagnosticsStatus_.empty()) localized::TextWrapped("%s",localized::Value(diagnosticsStatus_.c_str()).c_str());
}
void Panel::ExportDiagnostics(const DiagnosticsSnapshot& snapshot,DiagnosticOptions options) {
    if(OfflineBusy()) return;
    toolDialogOpen_=true; cancelDialog_=false;
    struct Scope { bool& open; ~Scope(){open=false;} } scope{toolDialogOpen_};
    // Caller passed a frozen copy: nested modal messages cannot change this report.
    const auto frozen=snapshot;
    std::vector<wchar_t> file(32768);
    const std::wstring name=L"Superior-diagnostics.json";
    std::copy(name.begin(),name.end(),file.begin());
    OPENFILENAMEW dialog{};
    dialog.lStructSize=sizeof dialog; dialog.hwndOwner=window_;
    dialog.lpstrFile=file.data(); dialog.nMaxFile=static_cast<DWORD>(file.size());
    dialog.lpstrFilter=L"JSON diagnostics (*.json)\0*.json\0\0"; dialog.lpstrDefExt=L"json";
    dialog.Flags=OFN_OVERWRITEPROMPT | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR | OFN_EXPLORER;
    dialog.lpstrTitle=settings_->language=="ar" ? L"حفظ تقرير التشخيص" : L"Save diagnostics report";
    if(!GetSaveFileNameW(&dialog) || cancelDialog_) {
        if(CommDlgExtendedError()) diagnosticsStatus_="Diagnostics file dialog failed";
        return;
    }
    diagnosticsStatus_=SaveDiagnosticsReport(std::filesystem::path(file.data()),frozen,options) ?
        "Diagnostic report saved" : "Diagnostic report save failed";
}
}
