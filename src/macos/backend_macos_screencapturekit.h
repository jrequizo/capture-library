#pragma once

#include "capture/backend.h"

#include <memory>

namespace capture {

struct MacOSScreenCaptureKitState;

class MacOSScreenCaptureKitBackend : public ICaptureBackend {
public:
    MacOSScreenCaptureKitBackend();
    ~MacOSScreenCaptureKitBackend() override;

    Error list_targets(std::vector<CaptureTarget>& out_targets) override;
    Error init(const CaptureTarget& target) override;
    Error grab_frame(Frame& out_frame, int timeout_ms = 1000) override;
    Error shutdown() override;

private:
    std::unique_ptr<MacOSScreenCaptureKitState> state_;
};

}  // namespace capture
