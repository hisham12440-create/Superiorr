#pragma once
#include <filesystem>
#include "core/config.h"
namespace superior {
// Only image/session preferences are copied; global hotkeys and language stay intact.
void CopyGameSettings(Settings& destination, const Settings& source);
std::string ProfileKey(const std::string& executable);
bool LoadGameProfile(const std::filesystem::path& directory, const std::string& executable, Settings& settings);
bool SaveGameProfile(const std::filesystem::path& directory, const std::string& executable, const Settings& settings);
}
