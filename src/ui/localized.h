#pragma once
#include <d3d11.h>
#include <string>
#include "imgui.h"
namespace superior::localized {
void BeginFrame(ID3D11Device* device, bool arabic);
void Shutdown();
std::string Value(const char* text);
void Text(const char* format, ...);
void TextDisabled(const char* format, ...);
void TextWrapped(const char* format, ...);
void TextColored(const ImVec4& color, const char* format, ...);
void SeparatorText(const char* label);
bool Checkbox(const char* label, bool* value);
bool CheckboxFlags(const char* label, int* flags, int mask);
bool Button(const char* label);
bool SliderFloat(const char* label, float* value, float lo, float hi, const char* format);
bool SliderInt(const char* label, int* value, int lo, int hi);
bool CollapsingHeader(const char* label);
bool BeginCombo(const char* label, const char* preview);
bool Selectable(const char* label, bool selected = false);
bool Combo(const char* label, int* current, const char* const* items, int count);
}
