#pragma once

#include <cstdint>

namespace capture {

// Get monotonic time in nanoseconds
uint64_t monotonic_now_ns();

// Convert nanoseconds to milliseconds
inline uint64_t ns_to_ms(uint64_t ns) {
    return ns / 1000000;
}

// Convert milliseconds to nanoseconds
inline uint64_t ms_to_ns(uint64_t ms) {
    return ms * 1000000;
}

}  // namespace capture
