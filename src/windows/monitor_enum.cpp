#include "monitor_enum.h"
#include "capture/error.h"

#include <vector>

namespace capture {

namespace {
    std::vector<MonitorInfo> g_monitors;
    
    BOOL CALLBACK enum_callback(HMONITOR hmon, HDC, LPRECT rect, LPARAM) {
        MonitorInfo info;
        info.handle = hmon;
        info.bounds = *rect;
        info.index = static_cast<uint32_t>(g_monitors.size());
        
        // Try to get monitor name
        MONITORINFOEX minfo = {};
        minfo.cbSize = sizeof(minfo);
        if (GetMonitorInfo(hmon, &minfo)) {
            info.name = minfo.szDevice;
        }
        
        g_monitors.push_back(info);
        return TRUE;
    }
}

std::vector<MonitorInfo> enumerate_monitors() {
    g_monitors.clear();
    EnumDisplayMonitors(nullptr, nullptr, enum_callback, 0);
    return g_monitors;
}

HMONITOR get_monitor_by_index(uint32_t index) {
    auto monitors = enumerate_monitors();
    if (index < monitors.size()) {
        return monitors[index].handle;
    }
    return nullptr;
}

}  // namespace capture
