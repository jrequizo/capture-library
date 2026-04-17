#pragma once

#include <string>

namespace capture {

enum class ErrorCode {
    Success = 0,
    NotSupported = 1,
    PermissionDenied = 2,
    Timeout = 3,
    TargetLost = 4,
    BackendFailure = 5,
    UnsupportedPixelFormat = 6,
    InvalidMonitorIndex = 7,
    InitializationFailed = 8,
    AccessDenied = 9,
    InvalidRegion = 10,
};

class Error {
public:
    Error() : code_(ErrorCode::Success) {}
    explicit Error(ErrorCode code, const std::string& message = "")
        : code_(code), message_(message) {}

    ErrorCode code() const { return code_; }
    const std::string& message() const { return message_; }
    
    bool is_success() const { return code_ == ErrorCode::Success; }
    bool is_error() const { return code_ != ErrorCode::Success; }
    
    std::string to_string() const;

private:
    ErrorCode code_;
    std::string message_;
};

}  // namespace capture
