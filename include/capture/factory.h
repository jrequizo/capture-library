#pragma once

#include "backend.h"
#include "error.h"

#include <memory>
#include <string>

namespace capture {

enum class BackendType {
    Windows,
    Linux,
    Auto  // detect based on platform
};

class CaptureFactory {
public:
    static Error create_backend(
        BackendType type,
        std::unique_ptr<ICaptureBackend>& out_backend
    );
    
    static Error create_platform_backend(
        std::unique_ptr<ICaptureBackend>& out_backend
    );
};

}  // namespace capture
