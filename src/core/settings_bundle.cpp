#include "settings_bundle.h"
#include "core/files.h"
#include "core/profiles.h"
#include <nlohmann/json.hpp>
#include <cmath>
#include <limits>
#include <stdexcept>
namespace superior {
namespace {
constexpr size_t kMaxBundleBytes=64*1024;
nlohmann::json ImageSettings(const Settings& s) {
    auto j=nlohmann::json::parse(ToJson(s));
    // Never transfer machine-local preferences, key bindings or identity.
    j.erase("language"); j.erase("hotkeys"); j.erase("auto_profiles");
    return j;
}
}
std::string ExportSettingsBundle(const Settings& settings) {
    Settings copy=settings; copy.Clamp();
    return nlohmann::json{{"format","superior-settings"},{"version",2},{"settings",ImageSettings(copy)}}.dump(2);
}
bool ImportSettingsBundle(const std::string& text, Settings& destination, std::string& error) {
    error.clear();
    if(text.size()>kMaxBundleBytes) { error="Settings bundle exceeds 64 KiB"; return false; }
    try {
        const auto root=nlohmann::json::parse(text, [](int depth, nlohmann::json::parse_event_t, nlohmann::json&) {
            if(depth>8) throw std::invalid_argument("Bundle nesting limit");
            return true;
        });
        if(!root.is_object() || root.size()!=3 || root.value("format",std::string{})!="superior-settings" ||
            !root.at("version").is_number_integer() || (root.at("version")!=1 && root.at("version")!=2)) {
            error="Unsupported settings bundle format or version"; return false;
        }
        const auto& values=root.at("settings");
        auto schema=ImageSettings(Settings{});
        const bool legacy=root.at("version")==1;
        if(legacy) {
            schema.erase("color_look"); schema.erase("color_look_enabled"); schema.erase("color_look_strength");
        }
        if(!values.is_object() || values.size()!=schema.size()) {
            error="Incomplete or unknown settings in bundle"; return false;
        }
        for(auto it=schema.begin();it!=schema.end();++it) {
            const auto& value=values.at(it.key());
            const auto& expected=it.value();
            const bool valid=expected.is_boolean() ? value.is_boolean() :
                expected.is_number_integer() ? value.is_number_integer() : value.is_number();
            if(value.is_number_integer() && expected.is_number_integer() &&
                (value.get<double>() < std::numeric_limits<int>::min() || value.get<double>() > std::numeric_limits<int>::max())) {
                error="Invalid settings value in bundle"; return false;
            }
            if(!valid || (value.is_number() && !std::isfinite(value.get<double>()))) {
                error="Invalid settings value in bundle"; return false;
            }
        }
        Settings candidate=destination;
        if(legacy) {
            candidate.colorLook=0; candidate.colorLookEnabled=true; candidate.colorLookStrength=1;
        }
        if(!FromJson(values.dump(),candidate)) { error="Invalid settings value in bundle"; return false; }
        Settings result=destination;
        CopyGameSettings(result,candidate);
        result.splitPreview=candidate.splitPreview;
        destination=result;
        return true;
    } catch(const std::exception&) { error="Invalid or incomplete settings bundle"; return false; }
}
bool SaveSettingsBundle(const std::filesystem::path& path,const Settings& settings) {
    try { return WriteTextAtomic(path,ExportSettingsBundle(settings)); }
    catch(const std::exception&) { return false; }
}
bool LoadSettingsBundle(const std::filesystem::path& path,Settings& destination,std::string& error) {
    std::string text;
    if(!ReadText(path,text,kMaxBundleBytes)) { error="Cannot read settings bundle (64 KiB maximum)"; return false; }
    return ImportSettingsBundle(text,destination,error);
}
}
