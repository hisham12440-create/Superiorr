#pragma once
#include <cstdint>

namespace superior {

// Exponential moving average of the interval between captured frames.
class IntervalEma {
public:
    void Add(double intervalMs) {
        if (intervalMs <= 0.0) return;
        value_ = (count_ == 0) ? intervalMs : value_ * 0.9 + intervalMs * 0.1;
        ++count_;
    }
    double Value() const { return value_; }
    int Count() const { return count_; }
    void Reset() { value_ = 0.0; count_ = 0; }

private:
    double value_ = 0.0;
    int count_ = 0;
};

// One generated frame is inserted between two real frames (x2). That only paces
// evenly when the display refreshes about twice per captured game frame
// (e.g. 30 fps on 60 Hz, 60 fps on 120 Hz, 72 fps on 144 Hz).
inline bool ShouldGenerate(double baseIntervalMs, double displayIntervalMs, int samples) {
    if (samples < 10 || baseIntervalMs <= 0.0 || displayIntervalMs <= 0.0) return false;
    const double ratio = baseIntervalMs / displayIntervalMs;
    return ratio >= 1.6 && ratio <= 2.5;
}

// Suggested game FPS cap so the x2 pacing is exact. Only a recommendation.
inline int RecommendedGameCap(double displayHz) {
    return static_cast<int>(displayHz / 2.0 + 0.5);
}

// A scene cut (or a failed flow estimate) is when most blocks have no good match.
inline bool IsSceneCut(uint32_t badBlocks, uint32_t totalBlocks) {
    if (totalBlocks == 0) return true;
    return static_cast<double>(badBlocks) / totalBlocks > 0.55;
}

// Almost every block unchanged: the captured frame is a duplicate.
inline bool IsStaticFrame(uint32_t staticBlocks, uint32_t totalBlocks) {
    if (totalBlocks == 0) return false;
    return static_cast<double>(staticBlocks) / totalBlocks >= 0.995;
}

}  // namespace superior
