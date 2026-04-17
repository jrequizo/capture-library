#pragma once

#include "frame.h"
#include "error.h"

#include <cstdint>
#include <memory>
#include <vector>
#include <string>

namespace capture {

struct CaptureTarget {
    enum class Kind {
        Monitor,
        Window
    };
    
    Kind kind;
    uint64_t id;  // backend-specific handle or index
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
