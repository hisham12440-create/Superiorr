#include "log.h"

#include <cstdarg>
#include <cstdio>
#include <ctime>
#include <mutex>
#include <filesystem>

namespace superior {
namespace {
std::mutex g_mutex;
FILE* g_file = nullptr;
std::filesystem::path g_path;
constexpr long kMaxBytes = 1024 * 1024;

FILE* OpenFile(const std::filesystem::path& path) {
#ifdef _WIN32
    FILE* file = nullptr;
    return _wfopen_s(&file, path.c_str(), L"a") == 0 ? file : nullptr;
#else
    return std::fopen(path.c_str(), "a");
#endif
}
void Rotate() {
    if (g_file) { std::fclose(g_file); g_file = nullptr; }
    auto old = g_path;
    old += ".1";
    std::error_code error;
    std::filesystem::remove(old, error);
    if (!error) std::filesystem::rename(g_path, old, error);
    g_file = OpenFile(g_path);
}
}  // namespace

bool Log::Open(const std::string& path) {
    std::lock_guard<std::mutex> lock(g_mutex);
    try {
        g_path = std::filesystem::u8path(path);
        if (g_file) { std::fclose(g_file); g_file = nullptr; }
        g_file = OpenFile(g_path);
        return g_file != nullptr;
    } catch (const std::exception&) { return false; }
}

void Log::Close() {
    std::lock_guard<std::mutex> lock(g_mutex);
    if (g_file) { std::fclose(g_file); g_file = nullptr; }
}

void Log::Write(const char* level, const char* fmt, ...) {
    std::lock_guard<std::mutex> lock(g_mutex);
    if (!g_file) return;
    if (std::ftell(g_file) > kMaxBytes) Rotate();
    if (!g_file) return;

    std::time_t t = std::time(nullptr);
    char stamp[32] = {};
    std::tm tmv{};
#ifdef _WIN32
    localtime_s(&tmv, &t);
#else
    localtime_r(&t, &tmv);
#endif
    std::strftime(stamp, sizeof stamp, "%Y-%m-%d %H:%M:%S", &tmv);
    std::fprintf(g_file, "%s [%s] ", stamp, level);
    va_list ap;
    va_start(ap, fmt);
    std::vfprintf(g_file, fmt, ap);
    va_end(ap);
    std::fputc('\n', g_file);
    std::fflush(g_file);
}

}  // namespace superior
