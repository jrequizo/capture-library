#pragma once

#include <cstdint>
#include <vector>

namespace capture {

enum class PixelFormat {
    Bgr8Unorm  // 8-bit BGR, tightly packed for OpenCV CV_8UC3
};

struct Frame {
    PixelFormat format;
    uint32_t width;
    uint32_t height;
    uint32_t stride_bytes;   // tightly packed in exported frame (width * 3)
    uint64_t timestamp_ns;   // monotonic nanosecond timestamp
    std::vector<uint8_t> bytes;
};

}  // namespace capture
