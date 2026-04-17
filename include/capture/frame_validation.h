#pragma once

#include "backend.h"
#include "error.h"
#include "frame.h"

#include <cstdint>

namespace capture {

class FrameConformanceChecker {
public:
    FrameConformanceChecker() = default;
    explicit FrameConformanceChecker(const CaptureTarget& target);

    void set_expected_target(const CaptureTarget& target);
    Error validate(const Frame& frame);

private:
    bool has_expected_size_ = false;
    uint32_t expected_width_ = 0;
    uint32_t expected_height_ = 0;
    bool has_previous_timestamp_ = false;
    uint64_t previous_timestamp_ns_ = 0;
};

Error validate_frame_contract(const Frame& frame);

}  // namespace capture
