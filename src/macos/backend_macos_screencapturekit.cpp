#include "backend_macos_screencapturekit.h"

namespace capture {

MacOSScreenCaptureKitBackend::MacOSScreenCaptureKitBackend() = default;

MacOSScreenCaptureKitBackend::~MacOSScreenCaptureKitBackend() {
    shutdown();
}

Error MacOSScreenCaptureKitBackend::list_targets(std::vector<CaptureTarget>& out_targets) {
    out_targets.clear();
    return Error(ErrorCode::NotSupported,
                 "macOS ScreenCaptureKit backend is planned but not yet implemented");
}

Error MacOSScreenCaptureKitBackend::init(const CaptureTarget& target) {
    current_target_ = target;
    initialized_ = false;
    return Error(ErrorCode::NotSupported,
                 "macOS ScreenCaptureKit backend is planned but not yet implemented");
}

Error MacOSScreenCaptureKitBackend::grab_frame(Frame& out_frame, int timeout_ms) {
    (void)out_frame;
    (void)timeout_ms;

    if (!initialized_) {
        return Error(ErrorCode::BackendFailure, "Backend not initialized");
    }

    return Error(ErrorCode::NotSupported,
                 "macOS ScreenCaptureKit backend is planned but not yet implemented");
}

Error MacOSScreenCaptureKitBackend::shutdown() {
    initialized_ = false;
    return Error(ErrorCode::Success);
}

}  // namespace capture
