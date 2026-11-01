#pragma once
#include <string>
#include "core/hotkeys.h"

namespace superior {

struct Settings {
    Hotkeys hotkeys = DefaultHotkeys();
    bool autoProfiles = true;
    std::string language = "en";   // "en" or "ar"
    int comfortMode = 0;          // 0 Comfort, 1 Balanced, 2 Max
    int oledLevel = 0;             // 0 = off, 1 = light, 2 = medium, 3 = strong
    float oledIntensity = 1.0f;    // 0..1
    float sharpness = 0.5f;        // 0..1
    bool frameGen = true;
    int maxMultiplier = 2;        // 2..4; Comfort always caps at 2
    bool antiAlias = true;
    float contrast = 1.0f;       // 0.5..2
    float saturation = 1.0f;     // 0..2
    float brightness = 0.0f;     // -0.25..0.25
    float gamma = 1.0f;          // 0.5..2
    bool splitPreview = false;  // left: basic source, right: enhanced
    float vibrance = 0.25f;        // 0..1

    // Forces every field into its valid range.
    void Clamp();
};

std::string ToJson(const Settings& s);
// Returns false (and leaves `out` untouched) if the text is not valid JSON.
bool FromJson(const std::string& text, Settings& out);
bool LoadSettings(const std::string& path, Settings& out);
bool SaveSettings(const std::string& path, const Settings& s);

}  // namespace superior
