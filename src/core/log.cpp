#include "log.h"

#include <cstdarg>
#include <cstdio>
#include <ctime>
#include <mutex>

namespace superior {
namespace {
std::mutex g_mutex;
FILE* g_file = nullptr;
std::string g_path;
constexpr long kMaxBytes = 1024 * 1024;

void Rotate() {
    if (g_file) {
        std::fclose(g_file);
        g_file = nullptr;
    }
    std::string old = g_path + ".1";
    std::remove(old.c_str());
    std::rename(g_path.c_str(), old.c_str());
    g_file = std::fopen(g_path.c_str(), "a");
}
}  // namespace

void Log::Open(const std::string& path) {
    std::lock_guard<std::mutex> lock(g_mutex);
    g_path = path;
    if (g_file) std::fclose(g_file);
    g_file = std::fopen(path.c_str(), "a");
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
