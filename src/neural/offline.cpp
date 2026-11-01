#include "offline.h"
#include "benchmark/quality.h"
#include "core/files.h"
#include <windows.h>
#include <onnxruntime_c_api.h>
#include <nlohmann/json.hpp>
#include <array>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <stdexcept>
namespace superior::neural {
namespace {
struct Runtime {
    HMODULE library=nullptr;
    const OrtApi* api=nullptr;
    OrtEnv* env=nullptr;
    OrtSessionOptions* options=nullptr;
    OrtSession* session=nullptr;
    OrtMemoryInfo* memory=nullptr;
    OrtValue* input=nullptr;
    OrtValue* output=nullptr;
    OrtTensorTypeAndShapeInfo* shape=nullptr;
    OrtTypeInfo* inputType=nullptr;
    OrtAllocator* allocator=nullptr;
    char* inputName=nullptr;
    char* outputName=nullptr;
    ~Runtime() {
        if(api) {
            if(allocator) { if(inputName) allocator->Free(allocator,inputName); if(outputName) allocator->Free(allocator,outputName); }
            if(shape) api->ReleaseTensorTypeAndShapeInfo(shape);
            if(inputType) api->ReleaseTypeInfo(inputType);
            if(input) api->ReleaseValue(input);
            if(output) api->ReleaseValue(output);
            if(memory) api->ReleaseMemoryInfo(memory);
            if(session) api->ReleaseSession(session);
            if(options) api->ReleaseSessionOptions(options);
            if(env) api->ReleaseEnv(env);
        }
        if(library) FreeLibrary(library);
    }
    void Check(OrtStatus* status) {
        if(!status) return;
        const std::string message=api->GetErrorMessage(status);
        api->ReleaseStatus(status);
        throw std::runtime_error(message);
    }
};
}
int RunOffline(const std::filesystem::path& runtimeDirectory,const std::filesystem::path& input,
               const std::filesystem::path& output,const std::filesystem::path& model) {
    auto report=output; report += ".report.json";
    bool mayWriteError = false;
    try {
        auto licensePath=model; licensePath += ".license.txt";
        for(const auto& protectedPath:{input,model,licensePath})
            if(SameFile(protectedPath,output) || SameFile(protectedPath,report))
                throw std::runtime_error("Output/report must not overwrite input, model or license");
        mayWriteError = true;
        auto source=benchmark::ReadPgm(input);
        if(source.width>1024 || source.height>1024) throw std::runtime_error("Offline neural input is limited to 1024x1024; tiled inference is not implemented");
        auto license=model; license += ".license.txt";
        std::string licenseText;
        if(!std::filesystem::is_regular_file(model) || !ReadText(license,licenseText) || licenseText.empty())
            throw std::runtime_error("Supply a trusted luma ONNX model and a nonempty model.onnx.license.txt; no weights or license rights are bundled");
        Runtime r;
        const auto dll=std::filesystem::absolute(runtimeDirectory / L"onnxruntime.dll");
        r.library=LoadLibraryExW(dll.c_str(),nullptr,LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_SYSTEM32);
        if(!r.library) throw std::runtime_error("Optional onnxruntime.dll / DirectML dependencies missing or unloadable in Superior/ beside the exe");
        using GetApiBase=const OrtApiBase*(ORT_API_CALL*)();
        const auto getApi=reinterpret_cast<GetApiBase>(GetProcAddress(r.library,"OrtGetApiBase"));
        if(!getApi) throw std::runtime_error("Invalid ONNX Runtime DLL");
        r.api=getApi()->GetApi(ORT_API_VERSION);
        if(!r.api) throw std::runtime_error("ONNX Runtime API 20 is required (1.20.1 or compatible newer runtime)");
        using AppendDml=OrtStatus*(ORT_API_CALL*)(OrtSessionOptions*,int);
        const auto appendDml=reinterpret_cast<AppendDml>(GetProcAddress(r.library,"OrtSessionOptionsAppendExecutionProvider_DML"));
        if(!appendDml) throw std::runtime_error("This ONNX Runtime distribution does not expose DirectML");
        r.Check(r.api->CreateEnv(ORT_LOGGING_LEVEL_WARNING,"SuperiorOffline",&r.env));
        r.Check(r.api->DisableTelemetryEvents(r.env));
        r.Check(r.api->CreateSessionOptions(&r.options));
        r.Check(r.api->SetSessionExecutionMode(r.options,ORT_SEQUENTIAL));
        r.Check(r.api->DisableMemPattern(r.options));
        r.Check(r.api->SetIntraOpNumThreads(r.options,1));
        r.Check(r.api->SetInterOpNumThreads(r.options,1));
        // Do not silently fall back to CPU inference when an operator is unsupported.
        r.Check(r.api->AddSessionConfigEntry(r.options,"session.disable_cpu_ep_fallback","1"));
        r.Check(appendDml(r.options,0));
        const auto sessionStart=std::chrono::steady_clock::now();
        r.Check(r.api->CreateSession(r.env,model.c_str(),r.options,&r.session));
        const auto sessionEnd=std::chrono::steady_clock::now();
        size_t count=0;
        r.Check(r.api->SessionGetInputCount(r.session,&count));
        if(count!=1) throw std::runtime_error("Model must have one NCHW luma input");
        r.Check(r.api->SessionGetOutputCount(r.session,&count));
        if(count!=1) throw std::runtime_error("Model must have one NCHW luma output");
        r.Check(r.api->SessionGetInputTypeInfo(r.session,0,&r.inputType));
        const OrtTensorTypeAndShapeInfo* inputShape=nullptr;
        r.Check(r.api->CastTypeInfoToTensorInfo(r.inputType,&inputShape));
        if(!inputShape) throw std::runtime_error("Input must be a tensor");
        ONNXTensorElementDataType type{};
        r.Check(r.api->GetTensorElementType(inputShape,&type));
        r.Check(r.api->GetDimensionsCount(inputShape,&count));
        if(type!=ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT || count!=4) throw std::runtime_error("Expected float32 [1,1,H,W] model input");
        std::array<int64_t,4> dimensions{};
        r.Check(r.api->GetDimensions(inputShape,dimensions.data(),dimensions.size()));
        const std::array<int64_t,4> actual{1,1,source.height,source.width};
        for(size_t i=0;i<4;++i) if(dimensions[i]>0 && dimensions[i]!=actual[i]) throw std::runtime_error("Model fixed dimensions do not match input image");
        r.Check(r.api->GetAllocatorWithDefaultOptions(&r.allocator));
        r.Check(r.api->SessionGetInputName(r.session,0,r.allocator,&r.inputName));
        r.Check(r.api->SessionGetOutputName(r.session,0,r.allocator,&r.outputName));
        r.Check(r.api->CreateCpuMemoryInfo(OrtArenaAllocator,OrtMemTypeDefault,&r.memory));
        r.Check(r.api->CreateTensorWithDataAsOrtValue(r.memory,source.pixels.data(),source.pixels.size()*sizeof(float),actual.data(),actual.size(),ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT,&r.input));
        const OrtValue* inputs[]={r.input}; const char* inputNames[]={r.inputName}; const char* outputNames[]={r.outputName};
        const auto runStart=std::chrono::steady_clock::now();
        r.Check(r.api->Run(r.session,nullptr,inputNames,inputs,1,outputNames,1,&r.output));
        const auto runEnd=std::chrono::steady_clock::now();
        r.Check(r.api->GetTensorTypeAndShape(r.output,&r.shape));
        r.Check(r.api->GetTensorElementType(r.shape,&type));
        r.Check(r.api->GetDimensionsCount(r.shape,&count));
        if(type!=ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT || count!=4) throw std::runtime_error("Expected float32 NCHW luma output");
        r.Check(r.api->GetDimensions(r.shape,dimensions.data(),dimensions.size()));
        if(dimensions[0]!=1||dimensions[1]!=1 || dimensions[2]<source.height || dimensions[3]<source.width ||
           dimensions[2]>source.height*4 || dimensions[3]>source.width*4 || dimensions[2]*source.width!=dimensions[3]*source.height)
            throw std::runtime_error("Output must be a same-aspect 1x..4x luma image");
        float* data=nullptr;
        r.Check(r.api->GetTensorMutableData(r.output,reinterpret_cast<void**>(&data)));
        benchmark::Image result;
        result.height=static_cast<int>(dimensions[2]); result.width=static_cast<int>(dimensions[3]);
        const size_t elements=static_cast<size_t>(result.width)*result.height;
        result.pixels.assign(data,data+elements);
        for(auto& value:result.pixels) { if(!std::isfinite(value)) throw std::runtime_error("Neural model returned nonfinite pixels"); value=std::clamp(value,0.f,1.f); }
        benchmark::WritePgm(output,result);
        const auto modelUtf8 = model.u8string();
        const std::string modelName(reinterpret_cast<const char*>(modelUtf8.data()),modelUtf8.size());
        const nlohmann::json metadata={{"success",true},{"provider","DirectML; CPU EP fallback disabled"},{"scope","offline luma inference, not live capture"},
            {"runtime_version",getApi()->GetVersionString()},{"model",modelName},{"model_license_verified",false},
            {"input",{source.width,source.height}},{"output",{result.width,result.height}},
            {"wall_session_ms",std::chrono::duration<double,std::milli>(sessionEnd-sessionStart).count()},
            {"wall_run_ms",std::chrono::duration<double,std::milli>(runEnd-runStart).count()}};
        if(!WriteTextAtomic(report,metadata.dump(2))) throw std::runtime_error("Could not save neural report");
        return 0;
    } catch(const std::exception& e) {
        if(mayWriteError) WriteTextAtomic(report,nlohmann::json({{"success",false},{"error",e.what()},{"live_spatial_fallback",true}}).dump(2));
        return 1;
    }
}
}
