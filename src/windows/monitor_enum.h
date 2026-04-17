#pragma once

#include "capture/backend.h"
#include <vector>
#include <string>
#include <windows.h>

namespace capture {

struct MonitorInfo {
    HMONITOR handle;
    RECT bounds;
    std::string name;
    uint32_t index;
};

// Enumerate all available monitors
std::vector<MonitorInfo> enumerate_monitors();

// Get monitor info by index
HMONITOR get_monitor_by_index(uint32_t index);

}  // namespace capture
