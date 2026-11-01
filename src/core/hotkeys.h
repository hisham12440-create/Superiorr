#pragma once
#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
namespace superior {
struct Hotkey {
    uint32_t key = 0;       // Windows virtual key; zero disables the action.
    uint32_t modifiers = 3; // Alt=1, Ctrl=2, Shift=4, Win=8.
    bool operator==(const Hotkey& other) const { return key == other.key && modifiers == other.modifiers; }
};
constexpr size_t kHotkeyCount = 12;
using Hotkeys = std::array<Hotkey, kHotkeyCount>;
inline constexpr std::array<const char*, kHotkeyCount> kHotkeyNames = {
    "Start / stop", "OLED mode", "Frame generation", "OLED intensity up",
    "OLED intensity down", "Sharpness up", "Sharpness down", "Tray language",
    "Quit", "Anti-aliasing", "Vibrance up", "Vibrance down"
};
inline Hotkeys DefaultHotkeys() {
    return {{{'S',3}, {'O',3}, {'G',3}, {0x26,3}, {0x28,3}, {0x27,3},
             {0x25,3}, {'L',3}, {'Q',3}, {'A',3}, {0x21,3}, {0x22,3}}};
}
inline bool SupportedKey(uint32_t key) {
    return key == 0 || (key >= 'A' && key <= 'Z') || (key >= '0' && key <= '9') ||
        (key >= 0x70 && key <= 0x7A) || (key >= 0x21 && key <= 0x28);
}
inline std::string KeyName(uint32_t key) {
    if (key == 0) return "Disabled";
    if ((key >= 'A' && key <= 'Z') || (key >= '0' && key <= '9')) return std::string(1, static_cast<char>(key));
    if (key >= 0x70 && key <= 0x7A) return "F" + std::to_string(key - 0x6F);
    constexpr std::array<const char*,8> names = {"Page Up", "Page Down", "End", "Home", "Left", "Up", "Right", "Down"};
    if (key >= 0x21 && key <= 0x28) return names[key - 0x21];
    return "Unsupported";
}
inline bool ValidateHotkeys(const Hotkeys& keys, std::string& error) {
    for (size_t i = 0; i < keys.size(); ++i) {
        const auto& key = keys[i];
        if (!SupportedKey(key.key) || key.modifiers > 15 || (key.key && !key.modifiers)) {
            error = std::string("Invalid key/modifier for ") + kHotkeyNames[i];
            return false;
        }
        if (!key.key) continue;
        for (size_t j = 0; j < i; ++j) {
            if (key == keys[j]) {
                error = std::string("Duplicate shortcut: ") + kHotkeyNames[j] + " / " + kHotkeyNames[i];
                return false;
            }
        }
    }
    error.clear();
    return true;
}
}
