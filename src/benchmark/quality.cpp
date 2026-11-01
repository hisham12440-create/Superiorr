#include "quality.h"
#include "core/files.h"
#include <nlohmann/json.hpp>
#include <fstream>
#include <algorithm>
#include <cmath>
#include <limits>
#include <sstream>
#include <stdexcept>
#include <map>
#include <array>
namespace superior::benchmark {
namespace {
struct Cancelled : std::exception { const char* what() const noexcept override { return "Verification cancelled"; } };
void CheckCancel(const CancelCheck& cancelled) { if(cancelled && cancelled()) throw Cancelled{}; }
void Validate(const Image& image) {
    if(image.width<=0 || image.height<=0 || image.width>8192 || image.height>8192 ||
       static_cast<size_t>(image.width)*image.height > kMaxImagePixels ||
       image.pixels.size()!=static_cast<size_t>(image.width)*image.height) throw std::runtime_error("Invalid image dimensions");
    for(float value:image.pixels) if(!std::isfinite(value) || value<0 || value>1) throw std::runtime_error("Image must contain finite normalized luma");
}
void Match(const Image& a,const Image& b) { Validate(a); Validate(b); if(a.width!=b.width || a.height!=b.height) throw std::runtime_error("Image dimensions differ"); }
std::string Token(std::istream& input) {
    std::string value;
    while(input>>std::ws && input.peek()=='#') { std::string comment; std::getline(input,comment); }
    if(!(input>>value)) throw std::runtime_error("Truncated PGM header");
    return value;
}
int Integer(const std::string& token) { size_t used=0; int value=std::stoi(token,&used); if(used!=token.size()) throw std::runtime_error("Invalid PGM number"); return value; }
double EdgeAt(const Image& image, int x, int y) {
    auto at=[&](int px,int py) { return image.pixels[static_cast<size_t>(std::clamp(py,0,image.height-1))*image.width+std::clamp(px,0,image.width-1)]; };
    const double gx=-at(x-1,y-1)+at(x+1,y-1)-2*at(x-1,y)+2*at(x+1,y)-at(x-1,y+1)+at(x+1,y+1);
    const double gy=-at(x-1,y-1)-2*at(x,y-1)-at(x+1,y-1)+at(x-1,y+1)+2*at(x,y+1)+at(x+1,y+1);
    return std::min(1.0,std::hypot(gx,gy)/4.0);
}
void ValidateThresholds(const Thresholds& t) {
    if(!std::isfinite(t.ssim)||t.ssim<0||t.ssim>1 || !std::isfinite(t.edgeError)||t.edgeError<0||t.edgeError>1 ||
       !std::isfinite(t.flaggedPercent)||t.flaggedPercent<0||t.flaggedPercent>100 ||
       !std::isfinite(t.targetPercent)||t.targetPercent<0||t.targetPercent>100) throw std::runtime_error("Invalid quality thresholds");
}
}
Image ReadPgm(const std::filesystem::path& path, const CancelCheck& cancelled) {
    CheckCancel(cancelled);
    std::ifstream input(path,std::ios::binary);
    if(!input) throw std::runtime_error("Cannot open PGM image");
    const std::string magic=Token(input);
    if(magic!="P5" && magic!="P2") throw std::runtime_error("Only P5/P2 8-bit PGM is supported");
    Image image; image.width=Integer(Token(input)); image.height=Integer(Token(input));
    if(image.width<=0||image.height<=0||image.width>8192||image.height>8192 || static_cast<size_t>(image.width)*image.height>kMaxImagePixels) throw std::runtime_error("PGM dimensions exceed limit");
    const int max=Integer(Token(input));
    if(max<=0||max>255) throw std::runtime_error("PGM maximum must be 1..255");
    image.pixels.resize(static_cast<size_t>(image.width)*image.height);
    if(magic=="P5") {
        char separator=0; input.get(separator);
        if(separator=='\r' && input.peek()=='\n') input.get();
        if(separator!=' ' && separator!='\n' && separator!='\r' && separator!='\t') throw std::runtime_error("Missing PGM separator");
        // Read one row at a time: bounded scratch storage and prompt cancellation.
        std::array<unsigned char,8192> row{};
        for(int y=0;y<image.height;++y) {
            CheckCancel(cancelled);
            if(!input.read(reinterpret_cast<char*>(row.data()),image.width)) throw std::runtime_error("Truncated PGM pixels");
            for(int x=0;x<image.width;++x) {
                if(row[x]>max) throw std::runtime_error("PGM sample exceeds maximum");
                image.pixels[static_cast<size_t>(y)*image.width+x]=static_cast<float>(row[x])/max;
            }
        }
    } else {
        size_t index=0;
        for(auto& value:image.pixels) {
            if((index++ & 4095)==0) CheckCancel(cancelled);
            int pixel=Integer(Token(input));
            if(pixel<0||pixel>max) throw std::runtime_error("Invalid PGM sample");
            value=static_cast<float>(pixel)/max;
        }
    }
    return image;
}
void WritePgm(const std::filesystem::path& path,const Image& image) {
    Validate(image);
    std::string output="P5\n"+std::to_string(image.width)+" "+std::to_string(image.height)+"\n255\n";
    for(float value:image.pixels) output.push_back(static_cast<char>(std::lround(value*255)));
    if(!WriteTextAtomic(path,output)) throw std::runtime_error("Cannot save PGM output");
}
Metrics Compare(const Image& truth,const Image& generated,const Image& a,const Image& b, const CancelCheck& cancelled) {
    CheckCancel(cancelled);
    Match(truth,generated); Match(truth,a); Match(truth,b);
    Metrics result;
    double mse=0;
    for(size_t i=0;i<truth.pixels.size();++i) { if((i & 65535)==0) CheckCancel(cancelled); const double d=truth.pixels[i]-generated.pixels[i]; mse+=d*d; }
    mse/=truth.pixels.size(); result.exact=mse==0;
    result.psnr=result.exact?std::numeric_limits<double>::infinity():10*std::log10(1.0/mse);
    // Local SSIM: non-overlapping 8x8 box windows, population variance,
    // normalized range L=1, K1=.01, K2=.03. Edge windows use their actual size.
    size_t windows=0;
    for(int y=0;y<truth.height;y+=8) for(int x=0;x<truth.width;x+=8) {
        if(x==0) CheckCancel(cancelled);
        double ma=0,mb=0,va=0,vb=0,cov=0,n=0;
        for(int j=y;j<std::min(y+8,truth.height);++j) for(int i=x;i<std::min(x+8,truth.width);++i) { size_t k=static_cast<size_t>(j)*truth.width+i; ma+=truth.pixels[k]; mb+=generated.pixels[k]; ++n; }
        ma/=n; mb/=n;
        for(int j=y;j<std::min(y+8,truth.height);++j) for(int i=x;i<std::min(x+8,truth.width);++i) { size_t k=static_cast<size_t>(j)*truth.width+i; double da=truth.pixels[k]-ma,db=generated.pixels[k]-mb; va+=da*da; vb+=db*db; cov+=da*db; }
        va/=n; vb/=n; cov/=n;
        result.ssim+=((2*ma*mb+.0001)*(2*cov+.0009))/((ma*ma+mb*mb+.0001)*(va+vb+.0009)); ++windows;
    }
    result.ssim/=windows;
    size_t flagged=0;
    for(int y=0;y<truth.height;++y) {
        CheckCancel(cancelled);
        for(int x=0;x<truth.width;++x) {
            const size_t i=static_cast<size_t>(y)*truth.width+x;
            const double et=EdgeAt(truth,x,y), eg=EdgeAt(generated,x,y);
            const double ea=EdgeAt(a,x,y), eb=EdgeAt(b,x,y);
            result.edgeError+=std::abs(et-eg);
            // Proxy only: valid motion/disocclusion may also trigger it.
            if(std::min(std::abs(eg-ea),std::abs(eg-eb))>.15 &&
               std::min(std::abs(generated.pixels[i]-a.pixels[i]),std::abs(generated.pixels[i]-b.pixels[i]))>.08) ++flagged;
        }
    }
    result.edgeError/=truth.pixels.size(); result.flaggedPercent=100.0*flagged/truth.pixels.size();
    return result;
}
bool Pass(const Metrics& m,const Thresholds& t) {
    ValidateThresholds(t);
    return std::isfinite(m.ssim)&&std::isfinite(m.edgeError)&&std::isfinite(m.flaggedPercent)&&
        m.edgeError>=0 && m.edgeError<=1 && m.flaggedPercent>=0 && m.flaggedPercent<=100 &&
        m.ssim>=t.ssim && m.edgeError<=t.edgeError && m.flaggedPercent<=t.flaggedPercent;
}
int Verify(const std::filesystem::path& manifest,const std::filesystem::path& report, const VerifyControl& control) {
    size_t completed=0, total=0;
    bool mayWriteError = false;
    try {
        auto readable = report; readable += ".txt";
        if(SameFile(manifest,report) || SameFile(manifest,readable)) throw std::runtime_error("Report must not overwrite manifest");
        CheckCancel(control.cancelled);
        std::string text;
        if(!ReadText(manifest,text,8*1024*1024)) throw std::runtime_error("Cannot read manifest (limit 8 MiB)");
        const auto input=nlohmann::json::parse(text);
        const auto& frames=input.at("frames");
        if(!frames.is_array() || frames.size()>10000) throw std::runtime_error("Manifest needs 1..10000 frames");
        auto path=[&](const nlohmann::json& j,const char* name) { return manifest.parent_path()/std::filesystem::u8path(j.at(name).get<std::string>()); };
        // No error/cancellation report may be written until all declared input
        // paths are known not to alias either output. Preflight failures leave
        // existing output files untouched; the exit code remains authoritative.
        for(const auto& frame:frames) for(const char* field:{"truth","generated","source_a","source_b"}) {
            CheckCancel(control.cancelled);
            const auto imagePath=path(frame,field);
            if(SameFile(imagePath,report) || SameFile(imagePath,readable))
                throw std::runtime_error("Report must not overwrite an input image");
        }
        mayWriteError = true;
        if(frames.empty()) throw std::runtime_error("Manifest needs 1..10000 frames");
        Thresholds t;
        if(input.contains("thresholds")) { const auto& j=input.at("thresholds"); t.ssim=j.value("ssim",t.ssim); t.edgeError=j.value("edge_error",t.edgeError); t.flaggedPercent=j.value("flagged_percent",t.flaggedPercent); t.targetPercent=j.value("target_percent",t.targetPercent); }
        ValidateThresholds(t);
        const double baseline=input.value("baseline_percent",0.0);
        if(!std::isfinite(baseline)||baseline<0||baseline>100) throw std::runtime_error("Invalid baseline_percent");
        total=frames.size();
        if(control.progress) control.progress(0,total);
        struct ClassResult { size_t count=0, passed=0; double ssim=0, edge=0; };
        std::map<std::string,ClassResult> classes;
        nlohmann::json results=nlohmann::json::array(); size_t passed=0;
        for(const auto& frame:frames) {
            CheckCancel(control.cancelled);
            const auto content=frame.value("content_class",std::string("unclassified"));
            if(content.empty() || content.size()>64) throw std::runtime_error("content_class must be 1..64 UTF-8 bytes");
            const auto truth=ReadPgm(path(frame,"truth"),control.cancelled);
            const auto generated=ReadPgm(path(frame,"generated"),control.cancelled);
            const auto a=ReadPgm(path(frame,"source_a"),control.cancelled);
            const auto b=ReadPgm(path(frame,"source_b"),control.cancelled);
            const auto m=Compare(truth,generated,a,b,control.cancelled);
            const bool pass=Pass(m,t); if(pass) ++passed;
            auto& group=classes[content]; ++group.count; if(pass) ++group.passed;
            group.ssim+=m.ssim; group.edge+=m.edgeError;
            ++completed;
            if(control.progress) control.progress(completed,total);
            results.push_back({{"content_class",content},{"generated",frame.at("generated")},{"ssim",m.ssim},{"edge_error",m.edgeError},{"proxy_flagged_percent",m.flaggedPercent},{"psnr_db",m.exact?nlohmann::json(nullptr):nlohmann::json(m.psnr)},{"exact_match",m.exact},{"passed",pass}});
        }
        const double rate=100.0*passed/frames.size();
        CheckCancel(control.cancelled);
        nlohmann::json grouped=nlohmann::json::object();
        for(const auto& item:classes) grouped[item.first]={{"frame_count",item.second.count},{"passed",item.second.passed},
            {"pass_percent",100.0*item.second.passed/item.second.count},{"mean_ssim",item.second.ssim/item.second.count},{"mean_edge_error",item.second.edge/item.second.count}};
        const bool baselineMet=rate>=baseline, accepted=rate>=t.targetPercent && baselineMet;
        nlohmann::json output={{"schema",2},{"scope","offline supplied reference corpus; not runtime validation"},{"ssim_definition","8x8 box windows, population variance, L=1, K1=.01, K2=.03"},
            {"frame_count",frames.size()},{"passed",passed},{"pass_percent",rate},{"target_percent",t.targetPercent},{"target_achieved",rate>=t.targetPercent},
            {"accepted",accepted},{"baseline_percent",baseline},{"baseline_met",baselineMet},{"content_classes",grouped},
            {"thresholds",{{"ssim",t.ssim},{"edge_error",t.edgeError},{"flagged_percent",t.flaggedPercent}}},{"frames",results}};
        if(!WriteTextAtomic(report,output.dump(2))) throw std::runtime_error("Cannot write report");
        std::ostringstream summary; summary<<"Superior offline reference verification\nFrames: "<<frames.size()<<"\nPassed: "<<passed<<"\nPass rate: "<<rate<<"%\nTarget: "<<t.targetPercent<<"%\nGround truth SSIM and edge error; ghost flags are a source-comparison proxy.\nNo claim about other games or GPU execution.\n";
        summary<<"Baseline: "<<baseline<<"% ("<<(baselineMet?"met":"regression")<<")\n";
        for(const auto& item:classes) summary<<"Class "<<item.first<<": "<<item.second.passed<<"/"<<item.second.count<<" passed\n";
        if(!WriteTextAtomic(readable,summary.str())) throw std::runtime_error("Cannot write text report");
        return accepted?0:2;
    } catch(const std::exception& e) {
        const bool cancelled=dynamic_cast<const Cancelled*>(&e)!=nullptr;
        const nlohmann::json error={{"error",e.what()},{"cancelled",cancelled},{"completed_count",completed},{"frame_count",total},{"target_achieved",false},{"accepted",false}};
        if(mayWriteError) {
            WriteTextAtomic(report,error.dump(2));
            auto readable=report; readable += ".txt";
            WriteTextAtomic(readable,std::string("Superior verification did not complete: ")+e.what()+"\nNo pass rate is reported.\n");
        }
        return cancelled?3:1;
    }
}
}
