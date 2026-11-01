#pragma once
#include <chrono>
namespace superior {
// Monotonic elapsed time: unaffected by wall-clock corrections.
class SteadyTimer {
public:
    using Clock = std::chrono::steady_clock;
    void Reset() { start_ = Clock::now(); }
    double ElapsedMs() const {
        return std::chrono::duration<double, std::milli>(Clock::now() - start_).count();
    }
private:
    Clock::time_point start_ = Clock::now();
};
}
