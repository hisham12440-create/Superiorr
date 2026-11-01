#include "panel.h"
#include "localized.h"
#include "filters/presets.h"
#include "imgui.h"
namespace superior {
bool Panel::DrawFilters() {
    bool changed = false;
    localized::TextWrapped("Eight independent filters. Bypass keeps your saved slider values.");
    changed |= DrawComparison();
    const char* names[] = {"Clean", "Vivid", "Cinematic", "Competitive"};
    if (localized::BeginCombo("Apply image preset", "Choose preset")) {
        for (int i=0;i<4;++i) if (localized::Selectable(names[i])) {
            filters::Apply(static_cast<filters::Preset>(i),*settings_);
            changed = true;
        }
        ImGui::EndCombo();
    }
    localized::TextWrapped("Image presets leave OLED and anti-aliasing preferences unchanged.");
    struct Control { const char* name; bool* enabled; float* value; float lo, hi; };
    Control controls[] = {
        {"Anti-aliasing", &settings_->antiAlias, &settings_->aaStrength, 0, 1},
        {"Sharpness", &settings_->sharpnessEnabled, &settings_->sharpness, 0, 1},
        {"Contrast", &settings_->contrastEnabled, &settings_->contrast, .5f, 2},
        {"Saturation", &settings_->saturationEnabled, &settings_->saturation, 0, 2},
        {"Brightness", &settings_->brightnessEnabled, &settings_->brightness, -.25f, .25f},
        {"Gamma", &settings_->gammaEnabled, &settings_->gamma, .5f, 2},
        {"Vibrance", &settings_->vibranceEnabled, &settings_->vibrance, 0, 1},
        {"Color temperature", &settings_->temperatureEnabled, &settings_->temperature, -1, 1}
    };
    const int columns = ImGui::GetContentRegionAvail().x >= 620 ? 2 : 1;
    if (ImGui::BeginTable("FilterCards", columns, ImGuiTableFlags_SizingStretchSame)) {
        for (int i=0;i<8;++i) {
            ImGui::TableNextColumn();
            ImGui::PushID(i);
            ImGui::BeginChild("Filter", ImVec2(0,170), ImGuiChildFlags_Borders);
            auto& c=controls[i];
            localized::TextColored(ImVec4(.2f,.65f,1,1), "%s", localized::Value(c.name).c_str());
            changed |= localized::Checkbox("Enabled",c.enabled);
            ImGui::BeginDisabled(!*c.enabled);
            // Hide the native English label so narrow cards never clip it.
            localized::Text(i==7 ? "Cool (-1) / neutral (0) / warm (+1)" : "Amount");
            ImGui::SetNextItemWidth(-1);
            changed |= ImGui::SliderFloat("##Amount",c.value,c.lo,c.hi,"%.2f");
            ImGui::EndDisabled();
            ImGui::EndChild();
            ImGui::PopID();
        }
        ImGui::EndTable();
    }
    localized::SeparatorText("3D LUT color looks");
    localized::TextWrapped("SDR artistic looks processed on the GPU, after filters and OLED. Not HDR or calibrated color management.");
    changed |= localized::Checkbox("Enable color look",&settings_->colorLookEnabled);
    const char* looks[]={"Off","Warm","Cool","Muted","Monochrome"};
    changed |= localized::Combo("Color look",&settings_->colorLook,looks,5);
    ImGui::BeginDisabled(!settings_->colorLookEnabled || settings_->colorLook==0);
    changed |= localized::SliderFloat("Color look strength",&settings_->colorLookStrength,0,1,"%.2f");
    ImGui::EndDisabled();
    if(localized::Button("Reset color look only")) {
        settings_->colorLook=0; settings_->colorLookStrength=1; settings_->colorLookEnabled=true; changed=true;
    }
    localized::TextWrapped("Image presets and the eight-filter reset leave the color look unchanged.");
    localized::TextWrapped("Temperature is an artistic RGB adjustment, not calibrated Kelvin or HDR conversion.");
    if (localized::Button("Reset filters only")) {
        filters::Apply(filters::Preset::Clean,*settings_);
        settings_->sharpness=.5f; settings_->vibrance=.25f;
        settings_->antiAlias=true; settings_->aaStrength=1;
        changed=true;
    }
    return changed;
}
bool Panel::DrawOled() {
    bool changed=false;
    localized::TextWrapped("Three OLED emulation levels, independent of the image presets.");
    if (settings_->oledLevel>0) lastOledLevel_=settings_->oledLevel;
    bool enabled=settings_->oledLevel>0;
    if (localized::Checkbox("Enable OLED",&enabled)) {
        settings_->oledLevel=enabled ? lastOledLevel_ : 0;
        changed=true;
    }
    const char* levels[]={"Light","Medium","Strong"};
    const char* descriptions[]={"Gentle black shaping","Balanced depth and contrast","Stronger black shaping and highlight roll-off"};
    for(int i=0;i<3;++i) {
        ImGui::PushID(i);
        if(localized::Selectable(levels[i],settings_->oledLevel==i+1)) {
            settings_->oledLevel=lastOledLevel_=i+1;
            changed=true;
        }
        localized::TextWrapped("%s",localized::Value(descriptions[i]).c_str());
        ImGui::Separator();
        ImGui::PopID();
    }
    ImGui::BeginDisabled(settings_->oledLevel==0);
    changed |= localized::SliderFloat("OLED intensity",&settings_->oledIntensity,0,1,"%.2f");
    ImGui::EndDisabled();
    localized::TextWrapped("Select a level to enable OLED. Zero intensity bypasses OLED processing.");
    localized::TextWrapped("OLED is an emulation; LCD black levels cannot be changed.");
    if(localized::Button("Reset OLED only")) {
        settings_->oledLevel=0; settings_->oledIntensity=1; lastOledLevel_=2; changed=true;
    }
    return changed;
}
}
