#include "capture/error.h"

namespace capture {

std::string Error::to_string() const {
    std::string code_name;
    
    switch (code_) {
        case ErrorCode::Success:
            code_name = "Success";
            break;
        case ErrorCode::NotSupported:
            code_name = "NotSupported";
            break;
        case ErrorCode::PermissionDenied:
            code_name = "PermissionDenied";
            break;
        case ErrorCode::Timeout:
            code_name = "Timeout";
            break;
        case ErrorCode::TargetLost:
            code_name = "TargetLost";
            break;
        case ErrorCode::BackendFailure:
            code_name = "BackendFailure";
            break;
        case ErrorCode::UnsupportedPixelFormat:
            code_name = "UnsupportedPixelFormat";
            break;
        case ErrorCode::InvalidMonitorIndex:
            code_name = "InvalidMonitorIndex";
            break;
        case ErrorCode::InitializationFailed:
            code_name = "InitializationFailed";
            break;
        case ErrorCode::AccessDenied:
            code_name = "AccessDenied";
            break;
        case ErrorCode::InvalidRegion:
            code_name = "InvalidRegion";
            break;
        default:
            code_name = "Unknown";
            break;
    }
    
    if (message_.empty()) {
        return code_name;
    }
    return code_name + ": " + message_;
}

}  // namespace capture
