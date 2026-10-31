#include "config.h"

#include <algorithm>
#include <fstream>
#include <sstream>

#include <nlohmann/json.hpp>

namespace superior {

void Settings::Clamp() {
    if (language != "en" && language != "ar") language = "en";
    oledLevel = std::min(3, std::max(0, oledLevel));
    oledIntensity = std::min(1.0f, std::max(0.0f, oledIntensity));
    sharpness = std::min(1.0f, std::max(0.0f, sharpness));
    vibrance = std::min(1.0f, std::max(0.0f, vibrance));
}

std::string ToJson(const Settings& s) {
    nlohmann::json j;
    j["language"] = s.language;
    j["oled_level"] = s.oledLevel;
    j["oled_intensity"] = s.oledIntensity;
    j["sharpness"] = s.sharpness;
    j["frame_gen"] = s.frameGen;
    j["anti_alias"] = s.antiAlias;
    j["vibrance"] = s.vibrance;
    return j.dump(2);
}

bool FromJson(const std::string& text, Settings& out) {
    try {
        nlohmann::json j = nlohmann::json::parse(text);
        Settings s = out;
        s.language = j.value("language", s.language);
        s.oledLevel = j.value("oled_level", s.oledLevel);
        s.oledIntensity = j.value("oled_intensity", s.oledIntensity);
        s.sharpness = j.value("sharpness", s.sharpness);
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
    std::ifstream f(path);
    if (!f) return false;
    std::stringstream ss;
    ss << f.rdbuf();
    return FromJson(ss.str(), out);
}

bool SaveSettings(const std::string& path, const Settings& s) {
    std::ofstream f(path, std::ios::trunc);
    if (!f) return false;
    f << ToJson(s);
    return static_cast<bool>(f);
}

}  // namespace superior
