#pragma once
#include <windows.h>
#include "core/hotkeys.h"
#include "core/log.h"
namespace superior {
// Owns only this application's registrations; replacement rolls back on conflicts.
class HotkeyManager {
public:
    ~HotkeyManager() { Clear(); }
    void Clear() {
        for (size_t i = 0; i < active_.size(); ++i) {
            if (active_[i] && !UnregisterHotKey(window_, static_cast<int>(i + 1)))
                SERR("UnregisterHotKey %zu failed (%lu)", i + 1, GetLastError());
            active_[i] = false;
        }
    }
    bool Apply(HWND window, const Hotkeys& requested, std::string& error) {
        if (!ValidateHotkeys(requested, error)) return false;
        const auto previous = keys_;
        const bool hadPrevious = installed_;
        Clear();
        installed_ = false;
        window_ = window;
        if (Register(requested, error)) { keys_ = requested; installed_ = true; return true; }
        Clear();
        if (hadPrevious) {
            std::string rollbackError;
            if (Register(previous, rollbackError)) { keys_ = previous; installed_ = true; }
            else {
                Clear();
                error += ". Previous shortcuts could not be restored; use the tray menu. " + rollbackError;
            }
        }
        return false;
    }
private:
    bool Register(const Hotkeys& keys, std::string& error) {
        for (size_t i = 0; i < keys.size(); ++i) {
            if (!keys[i].key) continue;
            if (!RegisterHotKey(window_, static_cast<int>(i + 1), keys[i].modifiers | MOD_NOREPEAT, keys[i].key)) {
                error = std::string(kHotkeyNames[i]) + " is unavailable (Windows error " + std::to_string(GetLastError()) + ")";
                return false;
            }
            active_[i] = true;
        }
        return true;
    }
    HWND window_ = nullptr;
    Hotkeys keys_{};
    std::array<bool, kHotkeyCount> active_{};
    bool installed_ = false;
};
}
