// Internal opt-in timing helpers. Thread CPU time distinguishes executing from blocked/descheduled wall time.
#pragma once
#include <time.h>

namespace fugue::rendergpu {
inline double threadCpuMs() {
#if defined(CLOCK_THREAD_CPUTIME_ID)
    timespec t{};
    if (clock_gettime(CLOCK_THREAD_CPUTIME_ID, &t) == 0) return double(t.tv_sec) * 1000 + double(t.tv_nsec) / 1e6;
#endif
    return -1; // unavailable; never substitute process CPU time
}
inline double cpuDelta(double a, double b) { return a < 0 || b < 0 ? -1 : b - a; }
} // namespace fugue::rendergpu
