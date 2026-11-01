#pragma once
#include "core/config.h"
#include "core/profiles.h"
#include <array>
#include <optional>
#include <cmath>
namespace superior {
// Reference of the region selection only, not a GPU image-quality validator.
inline bool PreviewOriginalAt(const Settings& s,float x,float y) {
    if(!s.splitPreview) return false;
    const bool first=(s.previewAxis==1 ? y : x)<s.previewPosition;
    return s.previewReverse ? !first : first;
}
inline bool PreviewGuideAt(const Settings& s,int pixelX,int pixelY,int width,int height) {
    if(!s.splitPreview || !s.previewLine || s.previewPosition<=0 || s.previewPosition>=1 || width<=0 || height<=0) return false;
    const int pixel=s.previewAxis==1 ? pixelY : pixelX;
    const int extent=s.previewAxis==1 ? height : width;
    return pixel>=0 && pixel<extent && pixel==static_cast<int>(std::floor(s.previewPosition*extent));
}
class ImageComparisonSlots {
public:
    bool Has(size_t index) const { return index<slots_.size() && slots_[index].has_value(); }
    bool Capture(size_t index,const Settings& source) {
        if(index>=slots_.size()) return false;
        slots_[index]=source; return true;
    }
    bool Apply(size_t index,Settings& destination) const {
        if(!Has(index)) return false;
        CopyImageSettings(destination,*slots_[index]); return true;
    }
    void Clear() { for(auto& slot:slots_) slot.reset(); }
private:
    std::array<std::optional<Settings>,2> slots_;
};
}
