#include "monotonic_time.h"

#ifdef _WIN32
#include <windows.h>

namespace capture {

uint64_t monotonic_now_ns() {
    LARGE_INTEGER freq, counter;
    QueryPerformanceFrequency(&freq);
    QueryPerformanceCounter(&counter);
    
    // Convert to nanoseconds
    return (counter.QuadPart * 1000000000) / freq.QuadPart;
}

}  // namespace capture

#else  // POSIX
#include <chrono>

namespace capture {

uint64_t monotonic_now_ns() {
    auto now = std::chrono::steady_clock::now();
    auto duration = now.time_since_epoch();
    return std::chrono::duration_cast<std::chrono::nanoseconds>(duration).count();
}

}  // namespace capture
#endif
