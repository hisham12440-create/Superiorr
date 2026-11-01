#include "panel.h"
#include "localized.h"
namespace superior {
bool Panel::DrawComparison() {
    if(!localized::CollapsingHeader("Comparison lab")) return false;
    bool changed=false;
    changed |= localized::Checkbox("Enable before / after comparison",&settings_->splitPreview);
    ImGui::BeginDisabled(!settings_->splitPreview);
    const char* axes[]={"Vertical divider","Horizontal divider"};
    changed |= localized::Combo("Divider direction",&settings_->previewAxis,axes,2);
    changed |= localized::SliderFloat("Divider position",&settings_->previewPosition,0,1,"%.2f");
    changed |= localized::Checkbox("Reverse original side",&settings_->previewReverse);
    changed |= localized::Checkbox("Show divider line",&settings_->previewLine);
    if(localized::Button("Center divider")) { settings_->previewPosition=.5f; changed=true; }
    ImGui::EndDisabled();
    localized::TextWrapped("Original means the latest captured real source with basic scaling, not a synchronized ground-truth frame. Moving scenes can differ in time from the generated side.");
    localized::SeparatorText("A / B image settings");
    localized::TextWrapped("Store two image settings in memory and apply either to the current game. These are settings, not frozen frames. Session policy, shortcuts, language and comparison layout do not change. Slots are lost when Superior exits.");
    if(localized::Button("Store current as A")) { comparisonSlots_.Capture(0,*settings_); comparisonStatus_="Image settings stored in A"; }
    ImGui::BeginDisabled(!comparisonSlots_.Has(0));
    if(localized::Button("Apply A")) { changed|=comparisonSlots_.Apply(0,*settings_); comparisonStatus_="A applied to current image settings"; }
    ImGui::EndDisabled();
    if(localized::Button("Store current as B")) { comparisonSlots_.Capture(1,*settings_); comparisonStatus_="Image settings stored in B"; }
    ImGui::BeginDisabled(!comparisonSlots_.Has(1));
    if(localized::Button("Apply B")) { changed|=comparisonSlots_.Apply(1,*settings_); comparisonStatus_="B applied to current image settings"; }
    ImGui::EndDisabled();
    if(localized::Button("Clear A / B slots")) { comparisonSlots_.Clear(); comparisonStatus_="A / B slots cleared"; }
    if(!comparisonStatus_.empty()) localized::TextWrapped("%s",localized::Value(comparisonStatus_.c_str()).c_str());
    return changed;
}
}
