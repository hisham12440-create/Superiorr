#pragma once
#include <string>

namespace superior {

// Rotating UTF-8 log, flushed on every line. Not a crash-dump facility.
class Log {
public:
    static bool Open(const std::string& path);
    static void Close();
    static void Write(const char* level, const char* fmt, ...);
};

}  // namespace superior

#define SLOG(...) ::superior::Log::Write("INFO", __VA_ARGS__)
#define SERR(...) ::superior::Log::Write("ERROR", __VA_ARGS__)
