#pragma once
#include <algorithm>
#include <cmath>
#include <vector>
namespace superior::interpolate {
inline int ChooseMultiplier(double sourceMs, double displayMs, int samples, int maximum) {
    if (samples < 10 || !std::isfinite(sourceMs) || !std::isfinite(displayMs) || sourceMs <= 0 || displayMs <= 0) return 1;
    const double ratio = sourceMs / displayMs;
    const int multiplier = std::clamp(static_cast<int>(std::min(ratio, 4.0) + .5), 1, std::clamp(maximum, 1, 4));
    // Do not offer multipliers that would exceed display refresh or bunch frames.
    return multiplier >= 2 && std::abs(ratio - multiplier) <= .15 ? multiplier : 1;
}
struct Sample { float phase; double dueMs; };
inline std::vector<Sample> Schedule(int multiplier, double nowMs, double displayMs) {
    std::vector<Sample> result;
    if (multiplier < 1 || multiplier > 4 || !std::isfinite(nowMs) || !std::isfinite(displayMs) || displayMs <= 0) return result;
    for (int i = 1; i <= multiplier; ++i)
        result.push_back({static_cast<float>(i) / static_cast<float>(multiplier), nowMs + (i-1) * displayMs});
    return result;
}
inline double ForwardBackwardError(double forwardX, double forwardY, double backwardX, double backwardY) {
    return std::hypot(forwardX + backwardX, forwardY + backwardY);
}
}
