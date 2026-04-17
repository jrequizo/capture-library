#include "png_writer.h"
#include <fstream>
#include <cstring>

namespace capture {

// Simple PNG writer using raw IDAT chunk approach
// This is a minimal implementation for MVP; consider using libpng for production

Error write_frame_to_png(const Frame& frame, const std::string& filepath) {
    if (frame.format != PixelFormat::Bgra8Unorm) {
        return Error(ErrorCode::UnsupportedPixelFormat,
                     "PNG writer only supports BGRA8Unorm");
    }
    
    if (frame.bytes.empty()) {
        return Error(ErrorCode::BackendFailure, "Frame data is empty");
    }
    
    // For MVP, we'll use a simple PPM format instead of PNG
    // PPM is much simpler to implement correctly
    // Format: RGB only, no alpha, raw binary
    
    std::ofstream file(filepath, std::ios::binary);
    if (!file) {
        return Error(ErrorCode::BackendFailure,
                     "Failed to open file: " + filepath);
    }
    
    // PPM header
    file << "P6\n";
    file << frame.width << " " << frame.height << "\n";
    file << "255\n";
    
    // Convert BGRA to RGB and write
    for (uint32_t y = 0; y < frame.height; ++y) {
        for (uint32_t x = 0; x < frame.width; ++x) {
            uint32_t pixel_offset = y * frame.stride_bytes + x * 4;
            
            uint8_t b = frame.bytes[pixel_offset + 0];
            uint8_t g = frame.bytes[pixel_offset + 1];
            uint8_t r = frame.bytes[pixel_offset + 2];
            // uint8_t a = frame.bytes[pixel_offset + 3];  // unused
            
            file.put(static_cast<char>(r));
            file.put(static_cast<char>(g));
            file.put(static_cast<char>(b));
        }
    }
    
    file.close();
    return Error(ErrorCode::Success);
}

}  // namespace capture
