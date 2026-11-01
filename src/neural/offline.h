#pragma once
#include <filesystem>
namespace superior::neural {
// Explicit offline command; never runs in the live GPU capture path.
int RunOffline(const std::filesystem::path& runtimeDirectory, const std::filesystem::path& input,
               const std::filesystem::path& output, const std::filesystem::path& model);
}
