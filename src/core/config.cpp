#include "config.h"
#include "files.h"

#include <algorithm>
#include <cmath>
#include <filesystem>

#include <nlohmann/json.hpp>

namespace superior {

void Settings::Clamp() {
    if (language != "en" && language != "ar") language = "en";
    maxMultiplier = std::clamp(maxMultiplier, 2, 4);
    comfortMode = std::min(2, std::max(0, comfortMode));
    oledLevel = std::min(3, std::max(0, oledLevel));
    oledIntensity = std::min(1.0f, std::max(0.0f, oledIntensity));
    sharpness = std::min(1.0f, std::max(0.0f, sharpness));
    contrast = std::min(2.0f, std::max(0.5f, contrast));
    saturation = std::min(2.0f, std::max(0.0f, saturation));
    brightness = std::min(0.25f, std::max(-0.25f, brightness));
    gamma = std::min(2.0f, std::max(0.5f, gamma));
    previewPosition = std::isfinite(previewPosition) ? std::clamp(previewPosition,0.f,1.f) : .5f;
    previewAxis = std::clamp(previewAxis,0,1);
    colorLook = std::clamp(colorLook, 0, 4);
    colorLookStrength = std::clamp(colorLookStrength, 0.0f, 1.0f);
    aaStrength = std::clamp(aaStrength, 0.0f, 1.0f);
    temperature = std::clamp(temperature, -1.0f, 1.0f);
    vibrance = std::min(1.0f, std::max(0.0f, vibrance));
}

std::string ToJson(const Settings& s) {
    nlohmann::json j;
    j["auto_profiles"] = s.autoProfiles;
    j["hotkeys"] = nlohmann::json::array();
    for (const auto& key : s.hotkeys)
        j["hotkeys"].push_back({{"key",key.key}, {"modifiers",key.modifiers}});
    j["sharpness_enabled"] = s.sharpnessEnabled;
    j["contrast_enabled"] = s.contrastEnabled;
    j["saturation_enabled"] = s.saturationEnabled;
    j["brightness_enabled"] = s.brightnessEnabled;
    j["gamma_enabled"] = s.gammaEnabled;
    j["vibrance_enabled"] = s.vibranceEnabled;
    j["temperature_enabled"] = s.temperatureEnabled;
    j["aa_strength"] = s.aaStrength;
    j["color_look"] = s.colorLook;
    j["color_look_enabled"] = s.colorLookEnabled;
    j["color_look_strength"] = s.colorLookStrength;
    j["temperature"] = s.temperature;
    j["contrast"] = s.contrast;
    j["saturation"] = s.saturation;
    j["brightness"] = s.brightness;
    j["gamma"] = s.gamma;
    j["preview_position"] = s.previewPosition;
    j["preview_axis"] = s.previewAxis;
    j["preview_reverse"] = s.previewReverse;
    j["preview_line"] = s.previewLine;
    j["split_preview"] = s.splitPreview;
    j["comfort_mode"] = s.comfortMode;
    j["language"] = s.language;
    j["oled_level"] = s.oledLevel;
    j["oled_intensity"] = s.oledIntensity;
    j["sharpness"] = s.sharpness;
    j["max_multiplier"] = s.maxMultiplier;
    j["frame_gen"] = s.frameGen;
    j["anti_alias"] = s.antiAlias;
    j["vibrance"] = s.vibrance;
    return j.dump(2);
}

bool FromJson(const std::string& text, Settings& out) {
    try {
        nlohmann::json j = nlohmann::json::parse(text);
        Settings s = out;
        s.autoProfiles = j.value("auto_profiles", s.autoProfiles);
        if (j.contains("hotkeys")) {
            const auto& keys = j.at("hotkeys");
            if (!keys.is_array() || keys.size() != kHotkeyCount) return false;
            for (size_t i = 0; i < kHotkeyCount; ++i) {
                if (!keys[i].at("key").is_number_integer() || !keys[i].at("modifiers").is_number_integer()) return false;
                const auto key = keys[i].at("key").get<int64_t>();
                const auto modifiers = keys[i].at("modifiers").get<int64_t>();
                if (key < 0 || key > 255 || modifiers < 0 || modifiers > 15) return false;
                s.hotkeys[i] = {static_cast<uint32_t>(key), static_cast<uint32_t>(modifiers)};
            }
            std::string error;
            if (!ValidateHotkeys(s.hotkeys, error)) return false;
        }
        s.sharpnessEnabled = j.value("sharpness_enabled", s.sharpnessEnabled);
        s.contrastEnabled = j.value("contrast_enabled", s.contrastEnabled);
        s.saturationEnabled = j.value("saturation_enabled", s.saturationEnabled);
        s.brightnessEnabled = j.value("brightness_enabled", s.brightnessEnabled);
        s.gammaEnabled = j.value("gamma_enabled", s.gammaEnabled);
        s.vibranceEnabled = j.value("vibrance_enabled", s.vibranceEnabled);
        s.temperatureEnabled = j.value("temperature_enabled", s.temperatureEnabled);
        s.aaStrength = j.value("aa_strength", s.aaStrength);
        s.colorLook = j.value("color_look", s.colorLook);
        s.colorLookEnabled = j.value("color_look_enabled", s.colorLookEnabled);
        s.colorLookStrength = j.value("color_look_strength", s.colorLookStrength);
        s.temperature = j.value("temperature", s.temperature);
        s.contrast = j.value("contrast", s.contrast);
        s.saturation = j.value("saturation", s.saturation);
        s.brightness = j.value("brightness", s.brightness);
        s.gamma = j.value("gamma", s.gamma);
        s.previewPosition = j.value("preview_position",s.previewPosition);
        s.previewAxis = j.value("preview_axis",s.previewAxis);
        s.previewReverse = j.value("preview_reverse",s.previewReverse);
        s.previewLine = j.value("preview_line",s.previewLine);
        s.splitPreview = j.value("split_preview", s.splitPreview);
        s.comfortMode = j.value("comfort_mode", s.comfortMode);
        s.language = j.value("language", s.language);
        s.oledLevel = j.value("oled_level", s.oledLevel);
        s.oledIntensity = j.value("oled_intensity", s.oledIntensity);
        s.sharpness = j.value("sharpness", s.sharpness);
        s.maxMultiplier = j.value("max_multiplier", s.maxMultiplier);
        s.frameGen = j.value("frame_gen", s.frameGen);
        s.antiAlias = j.value("anti_alias", s.antiAlias);
        s.vibrance = j.value("vibrance", s.vibrance);
        s.Clamp();
        out = s;
        return true;
    } catch (...) {
        return false;
    }
}

bool LoadSettings(const std::string& path, Settings& out) {
    std::string text;
    return ReadText(std::filesystem::u8path(path), text) && FromJson(text, out);
}

bool SaveSettings(const std::string& path, const Settings& s) {
    try { return WriteTextAtomic(std::filesystem::u8path(path), ToJson(s)); }
    catch (const std::exception&) { return false; }
}

}  // namespace superior
