#include "panel.h"
#include "localized.h"
#include <array>
namespace superior {
void Panel::DrawPerformance() {
    localized::TextWrapped("Nonblocking GPU timestamp observations. Each graph holds at most 128 completed query samples; the horizontal axis is sample order, not seconds or frames.");
    localized::TextWrapped("Capture processing covers source copy and optional flow. Final rendering covers optional midpoint generation, AA, upscale and final color rendering, excluding Present and scan-out. Do not sum percentiles or convert these values to FPS.");
    const char* labels[]={"Capture processing group","Final rendering group"};
    for(int index=0;index<2;++index) {
        ImGui::PushID(index);
        const auto scope=static_cast<GpuScope>(index);
        localized::SeparatorText(labels[index]);
        const auto summary=performance_.Describe(scope,performanceNow_);
        if(!summary) {
            localized::TextWrapped("No completed samples for this group");
        } else {
            localized::Text("Samples %u | Mean %.2f ms | P95 %.2f ms | Max %.2f ms",static_cast<unsigned>(summary->count),summary->mean,summary->p95,summary->maximum);
            localized::Text("Last sample collected %.2f seconds ago",summary->ageMs/1000.);
            if(summary->stale) localized::TextColored(ImVec4(1,.65f,.2f,1),"STALE: historical samples, not current GPU load");
            std::array<float,PerformanceHistory::Capacity> values{};
            for(size_t i=0;i<summary->count;++i) values[i]=static_cast<float>(performance_.At(scope,i).milliseconds);
            ImGui::PlotLines("##GroupTime",values.data(),static_cast<int>(summary->count),0,nullptr,0,
                static_cast<float>(std::max(1.,summary->maximum*1.15)),ImVec2(-1,120));
        }
        ImGui::PopID();
    }
    localized::TextWrapped("Age starts when the CPU collects a completed query, not when the GPU finished. Samples older than 2 seconds are marked stale. Query-ring pressure, disjoint clocks and failed queries can leave gaps.");
    localized::TextWrapped("History resets when a new capture session starts. Previous-session pending queries are excluded. Stopping capture retains history for inspection; this does not prove current device performance.");
    if(localized::Button("Open diagnostics report")) page_=6;
}
}
