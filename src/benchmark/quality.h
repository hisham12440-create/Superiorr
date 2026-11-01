#pragma once
#include <filesystem>
#include <vector>
#include <string>
#include <functional>
#include <cstddef>
namespace superior::benchmark {
struct Image { int width=0, height=0; std::vector<float> pixels; };
constexpr size_t kMaxImagePixels = 16 * 1024 * 1024;
using CancelCheck = std::function<bool()>;
struct VerifyControl {
    CancelCheck cancelled;
    std::function<void(size_t completed, size_t total)> progress;
};
Image ReadPgm(const std::filesystem::path& path, const CancelCheck& cancelled = {});
void WritePgm(const std::filesystem::path& path, const Image& image);
struct Metrics { double ssim=0, edgeError=0, psnr=0, flaggedPercent=0; bool exact=false; };
struct Thresholds { double ssim=.95, edgeError=.05, flaggedPercent=1.0; double targetPercent=99; };
Metrics Compare(const Image& truth,const Image& generated,const Image& a,const Image& b, const CancelCheck& cancelled = {});
bool Pass(const Metrics& metrics,const Thresholds& thresholds);
int Verify(const std::filesystem::path& manifest,const std::filesystem::path& report, const VerifyControl& control = {});
}
