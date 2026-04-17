#pragma once

#include "frame.h"
#include "error.h"

#include <cstdint>
#include <memory>
#include <vector>
#include <string>

namespace capture {

constexpr uint32_t CAPTURE_API_VERSION_MAJOR = 0;
constexpr uint32_t CAPTURE_API_VERSION_MINOR = 2;
constexpr uint32_t CAPTURE_API_VERSION_PATCH = 0;

struct CaptureRect {
    int32_t x = 0;
    int32_t y = 0;
    uint32_t width = 0;
    uint32_t height = 0;
};

struct CaptureTarget {
    enum class Kind {
        Monitor,
        Window
    };
    
    Kind kind = Kind::Monitor;
    uint64_t id = 0;  // backend-specific handle or index

    // Metadata populated by list_targets().
    std::string name;
    CaptureRect bounds;
    bool has_bounds = false;

    // Optional capture region, relative to the target's top-left corner.
    CaptureRect region;
    bool has_region = false;
};

class ICaptureBackend {
public:
    virtual ~ICaptureBackend() = default;
    
    virtual Error list_targets(std::vector<CaptureTarget>& out_targets) = 0;
    virtual Error init(const CaptureTarget& target) = 0;
    virtual Error grab_frame(Frame& out_frame, int timeout_ms = 1000) = 0;
    virtual Error shutdown() = 0;
};

}  // namespace capture
