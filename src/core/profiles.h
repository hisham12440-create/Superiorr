#pragma once
#include <filesystem>
#include "core/config.h"
namespace superior {
// Image controls only; leaves session policy, language, hotkeys and preview intact.
void CopyImageSettings(Settings& destination, const Settings& source);
// Only image/session preferences are copied; global hotkeys and language stay intact.
void CopyGameSettings(Settings& destination, const Settings& source);
std::string ProfileKey(const std::string& executable);
bool LoadGameProfile(const std::filesystem::path& directory, const std::string& executable, Settings& settings);
bool SaveGameProfile(const std::filesystem::path& directory, const std::string& executable, const Settings& settings);
}
