#pragma once
#include "core/config.h"
#include <algorithm>
namespace superior::filters {
// Neutral parameters implement independent bypass without erasing slider values.
struct Parameters {
    float sharpness, contrast, saturation, brightness, gamma, vibrance, temperature, aaStrength;
};
inline Parameters Effective(const Settings& s) {
    return {s.sharpnessEnabled?s.sharpness:0, s.contrastEnabled?s.contrast:1,
        s.saturationEnabled?s.saturation:1, s.brightnessEnabled?s.brightness:0,
        s.gammaEnabled?s.gamma:1, s.vibranceEnabled?s.vibrance:0,
        s.temperatureEnabled?s.temperature:0, s.antiAlias?s.aaStrength:0};
}
struct Rgb { float r,g,b; };
// Reference for the shader's final artistic warm/cool gain, not a colour-temperature calibration.
inline Rgb Temperature(Rgb c,float amount) {
    const float t=std::clamp(amount,-1.0f,1.0f);
    return {std::clamp(c.r*(1+.12f*t),0.f,1.f),std::clamp(c.g,0.f,1.f),std::clamp(c.b*(1-.12f*t),0.f,1.f)};
}
}
