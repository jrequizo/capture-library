#include "capture/factory.h"

#ifdef _WIN32
#include "../windows/backend_windows_wgc.h"
#else
#include "../linux/backend_linux_portal_pipewire.h"
#endif

namespace capture {

Error CaptureFactory::create_backend(
    BackendType type,
    std::unique_ptr<ICaptureBackend>& out_backend) {
    
#ifdef _WIN32
    if (type == BackendType::Windows || type == BackendType::Auto) {
        out_backend = std::make_unique<WindowsWgcBackend>();
        return Error(ErrorCode::Success);
    }
#else
    if (type == BackendType::Linux || type == BackendType::Auto) {
        out_backend = std::make_unique<LinuxPortalPipewireBackend>();
        return Error(ErrorCode::Success);
    }
#endif
    
    return Error(ErrorCode::NotSupported,
                 "Requested backend type is not supported on this platform");
}

Error CaptureFactory::create_platform_backend(
    std::unique_ptr<ICaptureBackend>& out_backend) {
    return create_backend(BackendType::Auto, out_backend);
}

}  // namespace capture
