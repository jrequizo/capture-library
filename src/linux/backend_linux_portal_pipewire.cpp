#include "backend_linux_portal_pipewire.h"

namespace capture {

LinuxPortalPipewireBackend::LinuxPortalPipewireBackend() : initialized_(false) {}

LinuxPortalPipewireBackend::~LinuxPortalPipewireBackend() {
    shutdown();
}

Error LinuxPortalPipewireBackend::list_targets(std::vector<CaptureTarget>& out_targets) {
    // TODO: Implement via XDG Desktop Portal ScreenCast interface
    return Error(ErrorCode::NotSupported, "Linux backend not yet implemented");
}

Error LinuxPortalPipewireBackend::init(const CaptureTarget& target) {
    // TODO: Implement portal session + PipeWire initialization
    return Error(ErrorCode::NotSupported, "Linux backend not yet implemented");
}

Error LinuxPortalPipewireBackend::grab_frame(Frame& out_frame, int timeout_ms) {
    if (!initialized_) {
        return Error(ErrorCode::BackendFailure, "Backend not initialized");
    }
    
    // TODO: Implement PipeWire frame readback
    return Error(ErrorCode::NotSupported, "Linux backend not yet implemented");
}

Error LinuxPortalPipewireBackend::shutdown() {
    initialized_ = false;
    return Error(ErrorCode::Success);
}

}  // namespace capture
