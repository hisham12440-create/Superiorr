#include "panel.h"
#include "localized.h"
#include "core/settings_bundle.h"
#include <commdlg.h>
#include <vector>
#include <algorithm>
namespace superior {
void Panel::DrawSetup(const hardware::Snapshot& hw,bool safeMode) {
    localized::TextWrapped("Start here: capture-only operation, without injection, drivers or administrator rights. Anti-cheat acceptance is not guaranteed.");
    localized::SeparatorText("Readiness observations");
    localized::TextWrapped("DirectX 11 UI renderer: initialized");
    localized::TextWrapped(hw.memoryKnown ? "DXGI memory budget: available" : "DXGI memory budget: unavailable; fallback policy applies");
    localized::TextWrapped(safeMode ? "Safe mode: frame generation blocked" : "Normal mode: frame generation follows your settings and runtime policy");
    localized::TextWrapped("These observations are not a capture compatibility test or a performance certification.");
    localized::SeparatorText("1. Prepare your game");
    localized::TextWrapped("Use windowed or borderless mode. Start with Comfort and a maximum multiplier of x2. Set the game FPS cap yourself; Superior does not change game settings.");
    localized::SeparatorText("2. Start capture");
    localized::TextWrapped("Use the configured Start / stop shortcut while the game is foreground, or the tray Start command with its 3-second delay. Switch back to the game before the delay ends.");
    localized::SeparatorText("3. Compare the image");
    localized::TextWrapped("Use Image filters and Before / after split. Change one control at a time. OLED is optional emulation, not HDR or a physical panel upgrade.");
    localized::SeparatorText("4. Check the session");
    localized::TextWrapped("Check Overview for session observations and export JSON when reporting an issue. S_OK counts are not displayed FPS. Stop capture before importing shared settings or running offline verification.");
    localized::SeparatorText("Troubleshooting");
    localized::TextWrapped("No capture: check the foreground game and try borderless mode. Protected content may be unavailable. If graphics fail, stop capture and restart Superior; automatic device-loss recovery is not implemented yet.");
    localized::TextWrapped("Artifacts or excess load: reduce multiplier, turn off frame generation, then compare filters individually. Do not infer quality from file size or scheduled frame counts.");
    if(localized::Button("Open image filters")) page_=1;
    if(localized::Button("Open session overview")) page_=0;
}
void Panel::DrawSettingsTransfer(bool running) {
    localized::SeparatorText("Share image and session settings");
    localized::TextWrapped("Export a versioned JSON bundle, or import one from another device. Language, shortcuts and automatic profile preferences stay local. Imported values replace image and session preferences; stop capture first.");
    ImGui::BeginDisabled(running || OfflineBusy());
    if(localized::Button("Import settings bundle")) ChooseSettingsBundle(true);
    if(localized::Button("Export settings bundle")) ChooseSettingsBundle(false);
    ImGui::EndDisabled();
    if(running) localized::TextWrapped("Stop capture before transferring settings");
    if(!transferStatus_.empty()) localized::TextWrapped("%s",localized::Value(transferStatus_.c_str()).c_str());
}
void Panel::ChooseSettingsBundle(bool importing) {
    if(OfflineBusy()) return;
    toolDialogOpen_=true; cancelDialog_=false;
    struct Scope { bool& open; ~Scope(){open=false;} } scope{toolDialogOpen_};
    std::vector<wchar_t> file(32768);
    if(!importing) {
        const std::wstring name=L"Superior-settings.json";
        std::copy(name.begin(),name.end(),file.begin());
    }
    OPENFILENAMEW dialog{};
    dialog.lStructSize=sizeof dialog; dialog.hwndOwner=window_;
    dialog.lpstrFile=file.data(); dialog.nMaxFile=static_cast<DWORD>(file.size());
    dialog.lpstrFilter=L"Superior settings (*.json)\0*.json\0\0";
    dialog.lpstrDefExt=L"json";
    dialog.Flags=OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR | OFN_EXPLORER |
        (importing ? OFN_FILEMUSTEXIST : OFN_OVERWRITEPROMPT);
    dialog.lpstrTitle=importing ? L"Import Superior settings" : L"Export Superior settings";
    const Settings snapshot=*settings_;
    const bool selected=importing ? GetOpenFileNameW(&dialog)!=FALSE : GetSaveFileNameW(&dialog)!=FALSE;
    if(!selected || cancelDialog_) {
        if(CommDlgExtendedError()) transferStatus_="Settings file dialog failed";
        return;
    }
    if(importing) {
        Settings candidate=*settings_;
        std::string error;
        if(!LoadSettingsBundle(std::filesystem::path(file.data()),candidate,error)) {
            transferStatus_=error; return;
        }
        *settings_=candidate;
        save_();
        transferStatus_="Settings imported; local language and shortcuts preserved";
    } else {
        transferStatus_=SaveSettingsBundle(std::filesystem::path(file.data()),snapshot) ?
            "Settings bundle exported" : "Settings bundle export failed";
    }
}
}
