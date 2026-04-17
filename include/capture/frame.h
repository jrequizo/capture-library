#pragma once

#include <cstdint>
#include <vector>

namespace capture {

enum class PixelFormat {
    Bgra8Unorm  // 8-bit BGRA, tightly packed
};

struct Frame {
    PixelFormat format;
    uint32_t width;
    uint32_t height;
    uint32_t stride_bytes;   // tightly packed in exported frame (width * 4)
    uint64_t timestamp_ns;   // monotonic nanosecond timestamp
    std::vector<uint8_t> bytes;
};

}  // namespace capture
