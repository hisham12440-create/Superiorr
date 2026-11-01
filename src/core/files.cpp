#include "files.h"
#include <fstream>
#include <stdexcept>
#include <utility>
#ifdef _WIN32
#include <windows.h>
#endif
namespace superior {
bool SameFile(const std::filesystem::path& a, const std::filesystem::path& b) {
    std::error_code error;
    if (std::filesystem::equivalent(a,b,error)) return true;
    return std::filesystem::absolute(a).lexically_normal() == std::filesystem::absolute(b).lexically_normal();
}
bool ReadText(const std::filesystem::path& path, std::string& text, size_t maxBytes) {
    try {
        std::ifstream input(path, std::ios::binary | std::ios::ate);
        if (!input) return false;
        const auto length = input.tellg();
        if (length < 0 || static_cast<uintmax_t>(length) > maxBytes) return false;
        std::string result(static_cast<size_t>(length), '\0');
        input.seekg(0);
        if (!result.empty() && !input.read(result.data(), static_cast<std::streamsize>(result.size()))) return false;
        text = std::move(result);
        return true;
    } catch (const std::exception&) { return false; }
}
bool WriteTextAtomic(const std::filesystem::path& path, const std::string& text) {
    auto temporary = path;
    temporary += ".tmp";
    try {
        std::ofstream output(temporary, std::ios::binary | std::ios::trunc);
        if (!output) return false;
        output.write(text.data(), static_cast<std::streamsize>(text.size()));
        output.flush();
        const bool written = static_cast<bool>(output);
        output.close();
        if (!written || output.fail()) throw std::runtime_error("Incomplete write");
#ifdef _WIN32
        if (!MoveFileExW(temporary.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
            throw std::runtime_error("Atomic replacement failed");
#else
        std::filesystem::rename(temporary, path);
#endif
        return true;
    } catch (const std::exception&) {
        std::error_code error;
        std::filesystem::remove(temporary, error);
        return false;
    }
}
}
