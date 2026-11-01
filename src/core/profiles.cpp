#include "profiles.h"
#include "files.h"
#include <nlohmann/json.hpp>
#include <iomanip>
#include <sstream>
namespace superior {
void CopyGameSettings(Settings& d, const Settings& s) {
    d.comfortMode = s.comfortMode; d.frameGen = s.frameGen; d.antiAlias = s.antiAlias;
    d.sharpness = s.sharpness; d.vibrance = s.vibrance; d.oledLevel = s.oledLevel;
    d.oledIntensity = s.oledIntensity; d.contrast = s.contrast; d.saturation = s.saturation;
    d.brightness = s.brightness; d.gamma = s.gamma;
    d.Clamp();
}
std::string ProfileKey(const std::string& executable) {
    uint64_t hash = 14695981039346656037ull;
    for (unsigned char c : executable) { hash ^= c; hash *= 1099511628211ull; }
    std::ostringstream output;
    output << std::hex << std::setfill('0') << std::setw(16) << hash;
    return output.str();
}
bool LoadGameProfile(const std::filesystem::path& directory, const std::string& executable, Settings& settings) {
    if (executable.empty()) return false;
    try {
        std::string text;
        if (!ReadText(directory / (ProfileKey(executable) + ".json"), text)) return false;
        const auto json = nlohmann::json::parse(text);
        if (json.at("version").get<int>() != 1 || json.at("executable").get<std::string>() != executable) return false;
        Settings candidate;
        if (!FromJson(json.at("settings").dump(), candidate)) return false;
        CopyGameSettings(settings, candidate);
        return true;
    } catch (const std::exception&) { return false; }
}
bool SaveGameProfile(const std::filesystem::path& directory, const std::string& executable, const Settings& settings) {
    if (executable.empty()) return false;
    try {
        std::filesystem::create_directories(directory);
        Settings image;
        CopyGameSettings(image, settings);
        auto values = nlohmann::json::parse(ToJson(image));
        for (const char* key : {"language", "hotkeys", "auto_profiles", "split_preview"}) values.erase(key);
        const nlohmann::json json = {{"version",1}, {"executable",executable}, {"settings",values}};
        return WriteTextAtomic(directory / (ProfileKey(executable) + ".json"), json.dump(2));
    } catch (const std::exception&) { return false; }
}
}
