#pragma once

#include "capture/frame.h"
#include "capture/error.h"

#include <string>

namespace capture {

// Write a Frame to PNG file
// Expects BGRA8 format, converts to RGB for PNG output
Error write_frame_to_png(const Frame& frame, const std::string& filepath);

}  // namespace capture
