#pragma once
#include "filters/parameters.h"
#include <array>
#include <cmath>
#include <stdexcept>
namespace superior::filters {
constexpr int kLutEdge=17;
constexpr int kLookCount=4;
struct LutVoxel { float r,g,b,a; };
using ColorLut=std::array<LutVoxel,kLutEdge*kLutEdge*kLutEdge>;
inline size_t LutIndex(int r,int g,int b) {
    return static_cast<size_t>((b*kLutEdge+g)*kLutEdge+r);
}
// Original SDR display-referred artistic looks, not HDR or colour calibration.
inline Rgb LookReference(Rgb c,int look) {
    const float y=.299f*c.r+.587f*c.g+.114f*c.b;
    switch(look) {
        case 0: break;
        case 1: c={c.r*1.06f,c.g,c.b*.94f}; break;
        case 2: c={c.r*.94f,c.g*1.01f,c.b*1.07f}; break;
        case 3: c={.025f+.94f*(y+(c.r-y)*.75f),.025f+.94f*(y+(c.g-y)*.75f),.025f+.94f*(y+(c.b-y)*.75f)}; break;
        case 4: c={y,y,y}; break;
        default: throw std::invalid_argument("Unknown colour look");
    }
    return {std::clamp(c.r,0.f,1.f),std::clamp(c.g,0.f,1.f),std::clamp(c.b,0.f,1.f)};
}
inline ColorLut BuildLook(int look) {
    if(look<0 || look>kLookCount) throw std::invalid_argument("Unknown colour look");
    ColorLut result{};
    for(int b=0;b<kLutEdge;++b) for(int g=0;g<kLutEdge;++g) for(int r=0;r<kLutEdge;++r) {
        const auto c=LookReference({r/float(kLutEdge-1),g/float(kLutEdge-1),b/float(kLutEdge-1)},look);
        result[LutIndex(r,g,b)]={c.r,c.g,c.b,1};
    }
    return result;
}
// CPU trilinear reference. GPU uses normalized coordinates (c*(N-1)+.5)/N.
inline Rgb SampleLook(const ColorLut& lut,Rgb c,float strength) {
    const float amount=std::clamp(strength,0.f,1.f);
    if(amount==0) return c;
    const float x=std::clamp(c.r,0.f,1.f)*(kLutEdge-1);
    const float y=std::clamp(c.g,0.f,1.f)*(kLutEdge-1);
    const float z=std::clamp(c.b,0.f,1.f)*(kLutEdge-1);
    const int ix=static_cast<int>(x),iy=static_cast<int>(y),iz=static_cast<int>(z);
    const float fx=x-ix,fy=y-iy,fz=z-iz;
    Rgb result{};
    for(int b=0;b<2;++b) for(int g=0;g<2;++g) for(int r=0;r<2;++r) {
        const float weight=(r?fx:1-fx)*(g?fy:1-fy)*(b?fz:1-fz);
        const auto& v=lut[LutIndex(std::min(ix+r,kLutEdge-1),std::min(iy+g,kLutEdge-1),std::min(iz+b,kLutEdge-1))];
        result.r+=v.r*weight; result.g+=v.g*weight; result.b+=v.b*weight;
    }
    return {c.r+(result.r-c.r)*amount,c.g+(result.g-c.g)*amount,c.b+(result.b-c.b)*amount};
}
}
