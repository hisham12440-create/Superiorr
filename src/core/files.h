#pragma once
#include <filesystem>
#include <string>
namespace superior {
bool ReadText(const std::filesystem::path& path, std::string& text, size_t maxBytes = 1024 * 1024);
bool WriteTextAtomic(const std::filesystem::path& path, const std::string& text);
}
