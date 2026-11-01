#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>
namespace superior::hardware {
constexpr uint64_t MiB = 1024ull * 1024;
enum class Tier { Low, SixGB, EightGB, High };
inline const char* TierName(Tier tier) {
    switch (tier) {
        case Tier::Low: return "Low";
        case Tier::SixGB: return "6 GB";
        case Tier::EightGB: return "8 GB";
        default: return "High";
    }
}
inline Tier SelectTier(uint64_t dedicatedBytes) {
    if (dedicatedBytes < 5 * 1024 * MiB) return Tier::Low;
    if (dedicatedBytes < 7 * 1024 * MiB) return Tier::SixGB;
    if (dedicatedBytes < 11 * 1024 * MiB) return Tier::EightGB;
    return Tier::High;
}
inline uint64_t TierCap(Tier tier) {
    switch (tier) {
        case Tier::Low: return 256 * MiB;
        case Tier::SixGB: return 512 * MiB;
        case Tier::EightGB: return 768 * MiB;
        default: return 1024 * MiB;
    }
}
inline uint64_t AvailableBudget(uint64_t budget, uint64_t usage, Tier tier) {
    const uint64_t free = budget > usage ? budget - usage : 0;
    return std::min(TierCap(tier), free > 400 * MiB ? free - 400 * MiB : 0);
}
enum class Mode { Comfort, Balanced, Max };
inline Mode EffectiveMode(Mode requested, bool battery, bool saver) {
    return (battery || saver) && requested == Mode::Max ? Mode::Balanced : requested;
}
inline bool PowerAllowsGeneration(bool battery, bool saver) { return !battery && !saver; }
inline double BudgetMs(Mode mode, double interval) {
    if (!std::isfinite(interval) || interval <= 0) return 0;
    return interval * (mode == Mode::Comfort ? .25 : mode == Mode::Balanced ? .40 : .65);
}
// Conservative hysteresis controlling optional interpolation, not image quality claims.
class Governor {
public:
    void Update(double gpuMs, double budgetMs, double nowMs) {
        if (!std::isfinite(gpuMs) || !std::isfinite(nowMs) || budgetMs <= 0 || gpuMs < 0) return;
        if (gpuMs > budgetMs) { enabled_ = false; headroomSince_ = -1; return; }
        if (gpuMs > budgetMs * .70) { headroomSince_ = -1; return; }
        if (headroomSince_ < 0 || nowMs < headroomSince_) headroomSince_ = nowMs;
        if (nowMs - headroomSince_ >= 5000) enabled_ = true;
    }
    bool Enabled() const { return enabled_; }
    void Reset() { enabled_ = true; headroomSince_ = -1; }
private:
    bool enabled_ = true;
    double headroomSince_ = -1;
};
}
