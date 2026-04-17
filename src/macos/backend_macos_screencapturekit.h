#pragma once

#include "capture/backend.h"

namespace capture {

class MacOSScreenCaptureKitBackend : public ICaptureBackend {
public:
    MacOSScreenCaptureKitBackend();
    ~MacOSScreenCaptureKitBackend() override;

    Error list_targets(std::vector<CaptureTarget>& out_targets) override;
    Error init(const CaptureTarget& target) override;
    Error grab_frame(Frame& out_frame, int timeout_ms = 1000) override;
    Error shutdown() override;

private:
    bool initialized_ = false;
    CaptureTarget current_target_;
};

}  // namespace capture
