#pragma once
#include "core/config.h"
namespace superior::filters {
enum class Preset { Clean, Vivid, Cinematic, Competitive, Oled };
inline void Apply(Preset preset, Settings& s) {
    s.contrast = 1; s.saturation = 1; s.brightness = 0; s.gamma = 1;
    s.sharpness = .5f; s.vibrance = .25f; s.oledLevel = 0; s.oledIntensity = 1;
    switch (preset) {
        case Preset::Clean: s.sharpness = .2f; s.vibrance = 0; break;
        case Preset::Vivid: s.contrast = 1.08f; s.saturation = 1.08f; s.vibrance = .5f; break;
        case Preset::Cinematic: s.contrast = 1.08f; s.saturation = .9f; s.sharpness = .2f; break;
        case Preset::Competitive: s.gamma = 1.1f; s.sharpness = .65f; s.vibrance = .15f; break;
        case Preset::Oled: s.oledLevel = 2; s.oledIntensity = .7f; break;
    }
    s.Clamp();
}
}
