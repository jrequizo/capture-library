#include "capture/frame_validation.h"

#include <cstddef>
#include <limits>
#include <string>

namespace capture {

namespace {

Error validate_expected_size(const Frame& frame, uint32_t width, uint32_t height) {
    if (frame.width != width || frame.height != height) {
        return Error(ErrorCode::BackendFailure,
                     "Frame dimensions " + std::to_string(frame.width) + "x" +
                         std::to_string(frame.height) +
                         " do not match expected target dimensions " +
                         std::to_string(width) + "x" + std::to_string(height));
    }
    return Error(ErrorCode::Success);
}

}  // namespace

FrameConformanceChecker::FrameConformanceChecker(const CaptureTarget& target) {
    set_expected_target(target);
}

void FrameConformanceChecker::set_expected_target(const CaptureTarget& target) {
    has_previous_timestamp_ = false;
    previous_timestamp_ns_ = 0;

    if (target.has_region) {
        has_expected_size_ = true;
        expected_width_ = target.region.width;
        expected_height_ = target.region.height;
        return;
    }

    if (target.has_bounds) {
        has_expected_size_ = true;
        expected_width_ = target.bounds.width;
        expected_height_ = target.bounds.height;
        return;
    }

    has_expected_size_ = false;
    expected_width_ = 0;
    expected_height_ = 0;
}

Error FrameConformanceChecker::validate(const Frame& frame) {
    Error err = validate_frame_contract(frame);
    if (err.is_error()) {
        return err;
    }

    if (has_expected_size_) {
        err = validate_expected_size(frame, expected_width_, expected_height_);
        if (err.is_error()) {
            return err;
        }
    }

    if (has_previous_timestamp_ && frame.timestamp_ns < previous_timestamp_ns_) {
        return Error(ErrorCode::BackendFailure,
                     "Frame timestamp regressed from " +
                         std::to_string(previous_timestamp_ns_) + " ns to " +
                         std::to_string(frame.timestamp_ns) + " ns");
    }

    has_previous_timestamp_ = true;
    previous_timestamp_ns_ = frame.timestamp_ns;
    return Error(ErrorCode::Success);
}

Error validate_frame_contract(const Frame& frame) {
    if (frame.format != PixelFormat::Bgr8Unorm) {
        return Error(ErrorCode::UnsupportedPixelFormat,
                     "Frame format is not packed BGR8");
    }

    if (frame.width == 0 || frame.height == 0) {
        return Error(ErrorCode::BackendFailure,
                     "Frame dimensions must be non-zero");
    }

    if (frame.width > std::numeric_limits<uint32_t>::max() / 3) {
        return Error(ErrorCode::BackendFailure,
                     "Frame width overflows BGR8 stride calculation");
    }

    const uint32_t expected_stride = frame.width * 3;
    if (frame.stride_bytes != expected_stride) {
        return Error(ErrorCode::BackendFailure,
                     "Frame stride " + std::to_string(frame.stride_bytes) +
                         " does not match packed BGR8 stride " +
                         std::to_string(expected_stride));
    }

    const size_t expected_bytes =
        static_cast<size_t>(frame.stride_bytes) * static_cast<size_t>(frame.height);
    if (frame.bytes.size() != expected_bytes) {
        return Error(ErrorCode::BackendFailure,
                     "Frame byte size " + std::to_string(frame.bytes.size()) +
                         " does not match stride * height " +
                         std::to_string(expected_bytes));
    }

    if (frame.timestamp_ns == 0) {
        return Error(ErrorCode::BackendFailure,
                     "Frame timestamp must be a non-zero monotonic time");
    }

    return Error(ErrorCode::Success);
}

}  // namespace capture
