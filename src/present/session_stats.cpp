#include "session_stats.h"
#include "core/files.h"
#include <nlohmann/json.hpp>
#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <sstream>
namespace superior::present {
void Measurements::Add(double value) {
    if(!std::isfinite(value) || value<0) return;
    if(!samples) minimum=maximum=value;
    else { minimum=std::min(minimum,value); maximum=std::max(maximum,value); }
    ++samples; sum+=value;
}
bool SessionSnapshot::Consistent() const {
    return real.Consistent() && generated.Consistent() && received==accepted+ignoredWhilePaused &&
        real.discarded+generated.discarded==replaced+droppedOnPause+droppedOnResize+droppedOnStop;
}
void SessionStats::Start(double nowMs,double displayIntervalMs) {
    if(!std::isfinite(nowMs) || !std::isfinite(displayIntervalMs) || displayIntervalMs<=0)
        throw std::invalid_argument("Invalid monotonic session time or display interval");
    state_={}; state_.available=state_.running=true; state_.displayIntervalMs=displayIntervalMs;
    start_=lastStateChange_=nowMs; pauseStart_=pausedTotal_=stoppedAt_=lastReturn_=0; haveReturn_=false;
}
void SessionStats::Receive(bool accepted) {
    if(!state_.running) return;
    ++state_.received;
    if(accepted) ++state_.accepted;
    else ++state_.ignoredWhilePaused;
}
void SessionStats::Plan(int multiplier) { if(state_.running) state_.plannedMultiplier=std::clamp(multiplier,1,4); }
bool SessionStats::Schedule(bool generated) {
    if(!state_.running || state_.paused) return false;
    auto& counts=generated?state_.generated:state_.real;
    ++counts.scheduled; ++counts.pending; return true;
}
bool SessionStats::Complete(bool generated,Outcome result,double dueMs,double beginMs,double endMs) {
    auto& counts=generated?state_.generated:state_.real;
    if(!state_.running || !counts.pending) return false;
    if(result!=Outcome::Submitted && result!=Outcome::Occluded && result!=Outcome::OtherStatus && result!=Outcome::Failed) return false;
    --counts.pending;
    switch(result) {
        case Outcome::Submitted: ++counts.submitted; break;
        case Outcome::Occluded: ++counts.occluded; break;
        case Outcome::OtherStatus: ++counts.otherStatus; break;
        case Outcome::Failed: ++counts.failed; break;
    }
    if(std::isfinite(beginMs) && std::isfinite(endMs) && beginMs>=start_ && endMs>=beginMs) state_.callWallMs.Add(endMs-beginMs);
    if(std::isfinite(dueMs) && std::isfinite(beginMs) && beginMs>=start_) state_.lateStartMs.Add(std::max(0.0,beginMs-dueMs));
    if(result==Outcome::Submitted && std::isfinite(endMs) && endMs>=start_ && (!haveReturn_ || endMs>=lastReturn_)) {
        if(haveReturn_ && endMs>=lastReturn_) state_.returnIntervalMs.Add(endMs-lastReturn_);
        lastReturn_=endMs; haveReturn_=true;
    }
    return true;
}
bool SessionStats::Discard(bool generated,DropReason reason) {
    auto& counts=generated?state_.generated:state_.real;
    if(!state_.running || !counts.pending) return false;
    --counts.pending; ++counts.discarded;
    switch(reason) {
        case DropReason::NewCapture: ++state_.replaced; break;
        case DropReason::Pause: ++state_.droppedOnPause; break;
        case DropReason::Resize: ++state_.droppedOnResize; break;
        case DropReason::Stop: ++state_.droppedOnStop; break;
    }
    return true;
}
void SessionStats::Pause(bool paused,double nowMs) {
    if(!state_.running || state_.paused==paused || !std::isfinite(nowMs) || nowMs<lastStateChange_) return;
    if(paused) { pauseStart_=nowMs; ++state_.pauses; }
    else pausedTotal_+=std::max(0.0,nowMs-pauseStart_);
    state_.paused=paused; haveReturn_=false; lastStateChange_=nowMs;
}
void SessionStats::Stop(double nowMs) {
    if(!state_.running || !std::isfinite(nowMs) || nowMs<lastStateChange_) return;
    // Unfinished work is classified as stopped even if a caller forgets to drain.
    const auto pending=state_.real.pending+state_.generated.pending;
    state_.real.discarded+=state_.real.pending; state_.real.pending=0;
    state_.generated.discarded+=state_.generated.pending; state_.generated.pending=0;
    state_.droppedOnStop+=pending;
    if(state_.paused) pausedTotal_+=std::max(0.0,nowMs-pauseStart_);
    state_.paused=false; state_.running=false; stoppedAt_=lastStateChange_=nowMs; haveReturn_=false;
}
SessionSnapshot SessionStats::Snapshot(double nowMs) const {
    auto result=state_;
    if(!result.available) return result;
    const double end=state_.running?(std::isfinite(nowMs)?std::max(lastStateChange_,nowMs):lastStateChange_):stoppedAt_;
    result.elapsedMs=std::max(0.0,end-start_);
    result.pausedMs=std::min(result.elapsedMs,pausedTotal_+(state_.running && state_.paused?std::max(0.0,end-pauseStart_):0));
    result.activeMs=result.elapsedMs-result.pausedMs;
    return result;
}
namespace {
nlohmann::json Counts(const FrameCounts& c) {
    return {{"scheduled",c.scheduled},{"submitted_s_ok",c.submitted},{"occluded",c.occluded},{"other_status",c.otherStatus},
        {"failed",c.failed},{"discarded",c.discarded},{"pending",c.pending}};
}
nlohmann::json Times(const Measurements& m) {
    if(!m.samples) return nullptr;
    return {{"samples",m.samples},{"mean",m.Mean()},{"minimum",m.minimum},{"maximum",m.maximum}};
}
}
std::string SessionJson(const SessionSnapshot& s) {
    if(!s.available || !s.Consistent() || !std::isfinite(s.elapsedMs) || !std::isfinite(s.activeMs) ||
       !std::isfinite(s.pausedMs) || s.elapsedMs<0 || s.activeMs<0 || s.pausedMs<0) throw std::invalid_argument("No valid session snapshot");
    return nlohmann::json({{"schema",1},{"scope","API-side session observations; not scan-out, game FPS, GPU total time or quality"},
        {"running",s.running},{"paused",s.paused},{"elapsed_ms",s.elapsedMs},{"active_ms",s.activeMs},{"paused_ms",s.pausedMs},
        {"display_interval_ms",s.displayIntervalMs},{"delivered_capture_frames",s.received},{"accepted_for_processing",s.accepted},
        {"ignored_while_paused",s.ignoredWhilePaused},{"pause_count",s.pauses},{"last_planned_multiplier",s.plannedMultiplier},
        {"real",Counts(s.real)},{"generated_path",Counts(s.generated)},
        {"discards",{{"new_capture",s.replaced},{"pause",s.droppedOnPause},{"resize",s.droppedOnResize},{"stop",s.droppedOnStop}}},
        {"render_and_present_call_wall_ms",Times(s.callWallMs)},{"start_lateness_ms",Times(s.lateStartMs)},
        {"between_s_ok_returns_ms",Times(s.returnIntervalMs)},{"scanout_fps",nullptr},{"ground_truth_pass_rate",nullptr}}).dump(2);
}
bool SaveSessionReport(const std::filesystem::path& path,const SessionSnapshot& snapshot) {
    try { return WriteTextAtomic(path,SessionJson(snapshot)); } catch(const std::exception&) { return false; }
}
}
