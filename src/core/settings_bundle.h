#pragma once
#include "core/config.h"
#include <filesystem>
namespace superior {
// Versioned interchange, not a replacement for the local config file.
std::string ExportSettingsBundle(const Settings& settings);
bool ImportSettingsBundle(const std::string& text, Settings& destination, std::string& error);
bool SaveSettingsBundle(const std::filesystem::path& path, const Settings& settings);
bool LoadSettingsBundle(const std::filesystem::path& path, Settings& destination, std::string& error);
}
