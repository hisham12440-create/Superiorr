#pragma once
#include <algorithm>

// CPU reference of the OLED luma curve. shaders/render.hlsl uses the SAME formulas;
// the unit tests prove this curve is monotonic and never clips.
namespace superior {

constexpr float kOledBlack[4] = {0.0f, 0.02f, 0.04f, 0.07f};
constexpr float kOledSCurve[4] = {0.0f, 0.15f, 0.30f, 0.45f};
constexpr float kOledSat[4] = {0.0f, 0.08f, 0.15f, 0.25f};
constexpr float kOledSpec[4] = {0.0f, 0.03f, 0.06f, 0.10f};
constexpr float kOledLocal[4] = {0.0f, 0.10f, 0.20f, 0.30f};

inline float OledCurve(float y, int level, float intensity) {
    if (level <= 0) return y;
    level = std::min(level, 3);
    const float a = std::min(1.0f, std::max(0.0f, intensity));
    y = std::min(1.0f, std::max(0.0f, y));
    const float k = std::max(kOledBlack[level] * a, 1e-4f);
    const float y2 = y * y * (1.0f + k) / (y + k);            // smooth black crush
    const float s = kOledSCurve[level] * a;
    const float y3 = y2 + (y2 * y2 * (3.0f - 2.0f * y2) - y2) * s;  // S-curve, midtones protected by s
    const float t = std::min(1.0f, std::max(0.0f, (y3 - 0.75f) / 0.25f));
    const float ss = t * t * (3.0f - 2.0f * t);
    return y3 + kOledSpec[level] * a * ss * (1.0f - y3);      // highlight boost, never exceeds 1
}

}  // namespace superior
