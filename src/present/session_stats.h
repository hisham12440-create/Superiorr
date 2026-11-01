#pragma once
#include <cstdint>
#include <filesystem>
#include <string>
namespace superior::present {
enum class Outcome { Submitted, Occluded, OtherStatus, Failed };
enum class DropReason { NewCapture, Pause, Resize, Stop };
struct FrameCounts {
    uint64_t scheduled=0, submitted=0, occluded=0, otherStatus=0, failed=0, discarded=0, pending=0;
    bool Consistent() const { return scheduled==submitted+occluded+otherStatus+failed+discarded+pending; }
};
struct Measurements {
    uint64_t samples=0;
    double sum=0, minimum=0, maximum=0;
    void Add(double value);
    double Mean() const { return samples ? sum/static_cast<double>(samples) : 0; }
};
struct SessionSnapshot {
    bool available=false, running=false, paused=false;
    double elapsedMs=0, activeMs=0, pausedMs=0, displayIntervalMs=0;
    uint64_t received=0, accepted=0, ignoredWhilePaused=0, pauses=0;
    uint64_t replaced=0, droppedOnPause=0, droppedOnResize=0, droppedOnStop=0;
    int plannedMultiplier=1;
    FrameCounts real, generated;
    Measurements callWallMs, lateStartMs, returnIntervalMs;
    bool Consistent() const;
};
// Main-thread-only bookkeeping. Measures API observations, never display scan-out,
// game-engine FPS, end-to-end latency or ghost-free generated-frame counts.
class SessionStats {
public:
    void Start(double nowMs, double displayIntervalMs);
    void Receive(bool accepted);
    void Plan(int multiplier);
    bool Schedule(bool generated);
    bool Complete(bool generated, Outcome result, double dueMs, double beginMs, double endMs);
    bool Discard(bool generated, DropReason reason);
    void Pause(bool paused, double nowMs);
    void Stop(double nowMs);
    SessionSnapshot Snapshot(double nowMs) const;
private:
    SessionSnapshot state_;
    double start_=0, pauseStart_=0, pausedTotal_=0, stoppedAt_=0, lastReturn_=0, lastStateChange_=0;
    bool haveReturn_=false;
};
std::string SessionJson(const SessionSnapshot& snapshot);
bool SaveSessionReport(const std::filesystem::path& path, const SessionSnapshot& snapshot);
}
