#pragma once
#include <string>

namespace superior {

// Crash-safe rotating log: flushed on every line, rotated at ~1 MB.
class Log {
public:
    static void Open(const std::string& path);
    static void Write(const char* level, const char* fmt, ...);
};

}  // namespace superior

#define SLOG(...) ::superior::Log::Write("INFO", __VA_ARGS__)
#define SERR(...) ::superior::Log::Write("ERROR", __VA_ARGS__)
