# High-Performance Display Capture Library

A cross-platform C++ display capture library targeting Windows first and macOS next. The exported frame contract is CPU-readable packed BGR8, suitable for OpenCV and ML inference pipelines such as YOLO and custom classifiers.

Python CV pipeline support is now available through optional pybind11 bindings that expose monitor capture as NumPy `uint8` arrays in OpenCV-native BGR order.

## Architecture

### Core Principle

Capture stays separate from preprocessing and inference. The public API is intentionally narrow:

```cpp
struct CaptureRect {
    int32_t x, y;
    uint32_t width, height;
};

struct CaptureTarget {
    Kind kind;
    uint64_t id;
    std::string name;
    CaptureRect bounds;       // target metadata from list_targets()
    bool has_bounds;
    CaptureRect region;       // optional ROI, relative to target top-left
    bool has_region;
};

struct Frame {
    PixelFormat format;        // Bgr8Unorm
    uint32_t width, height;
    uint32_t stride_bytes;     // tightly packed: width * 3
    uint64_t timestamp_ns;     // monotonic nanoseconds
    std::vector<uint8_t> bytes;
};

class ICaptureBackend {
    virtual Error list_targets(std::vector<CaptureTarget>& out) = 0;
    virtual Error init(const CaptureTarget& target) = 0;
    virtual Error grab_frame(Frame& out, int timeout_ms) = 0;
    virtual Error shutdown() = 0;
};
```

The current API version is `0.3.1`, exposed through `CAPTURE_API_VERSION_MAJOR`, `CAPTURE_API_VERSION_MINOR`, and `CAPTURE_API_VERSION_PATCH` in `capture/backend.h`.

## Platform Backends

**Windows: implemented**

- Windows Graphics Capture with D3D11.
- Monitor enumeration through `EnumDisplayMonitors`.
- GPU to CPU transfer via reusable staging textures.
- SDR/BGR8 output for direct `CV_8UC3` wrapping.
- Optional region capture with bounds validation.
- Target metadata: monitor index, name, origin, and size.

**macOS: in progress**

- Backend is wired into the factory and CMake build.
- Monitor enumeration is implemented with CoreGraphics active display IDs, names, and bounds.
- Screen Recording permission preflight is implemented with deterministic diagnostics.
- ScreenCaptureKit monitor streaming is implemented for BGRA source frames normalized to packed BGR8.
- Optional region capture uses the same target-relative semantics as Windows.

**Linux: unsupported**

Linux is not part of the required support matrix for this project. The old Linux portal/PipeWire stub remains in the tree for reference, but it is no longer built or advertised as supported.

## Building

### Prerequisites

**Windows**

- Visual Studio 2019+ or Clang-CL.
- Windows 10.0.19041+.
- CMake 3.16+.

**macOS**

- Apple Clang with C++17 support.
- macOS 10.15+ for Screen Recording permission preflight.
- macOS 12.3+ for ScreenCaptureKit frame streaming.
- CMake 3.16+.

### Build

```bash
mkdir build
cd build
cmake ..
cmake --build . --config Release
```

### Windows Quick Start

```powershell
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release
.\build\Release\capture_once.exe --backend windows --list-targets
.\build\Release\capture_once.exe --backend windows --monitor 0 --output screen.png
.\build\Release\capture_loop.exe --backend windows --monitor 0 --frames 120
```

If target creation fails, verify the process is running on Windows 10 2004 or newer and that the active desktop session has a visible monitor. For performance checks, start with `benchmark_fps --threshold-preset smoke`, then move to `single-monitor` or `multi-monitor`.

### macOS Quick Start

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release
./build/capture_once --backend macos --list-targets
./build/capture_once --backend macos --monitor 0 --output screen.png
./build/capture_loop --backend macos --monitor 0 --frames 120
```

If Screen Recording permission is denied, grant access to the terminal or host application in **System Settings > Privacy & Security > Screen Recording**, then restart that process. ScreenCaptureKit frame streaming requires macOS 12.3 or newer.

### macOS Runtime Notes

The macOS backend uses ScreenCaptureKit for monitor capture on macOS 12.3+. Use `capture_once --backend macos --list-targets` to verify display IDs, names, origins, and pixel sizes. Frame capture returns packed BGR8 data with the same stride and region semantics as Windows.

If permission is denied, grant Screen Recording access to the terminal or host application in **System Settings > Privacy & Security > Screen Recording**, then restart that process.

### Python Bindings (NumPy / OpenCV)

Python bindings are optional and disabled by default.

Prerequisites:

- Python 3.9+.
- `pybind11` with CMake package support.
- NumPy.

Install Python-side dependencies:

```bash
pip install pybind11 numpy
```

Configure and build with Python bindings enabled:

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DCAPTURE_BUILD_PYTHON_BINDINGS=ON
cmake --build build --config Release
```

Minimal usage:

```python
import capture

targets = capture.list_targets("auto")
print(targets)

with capture.CaptureSession(backend="auto", monitor=0, timeout_ms=3000) as session:
    frame = session.grab()  # NumPy uint8 array, shape (H, W, 3), BGR channel order
    print(frame.shape, frame.dtype)
```

OpenCV display example:

```python
import cv2
import capture

with capture.CaptureSession(monitor=0) as session:
    frame = session.grab()
    cv2.imshow("capture", frame)
    cv2.waitKey(0)
```

### Python Performance Monitor (test-fullscreen-mss)

The exploratory Python monitor project now lives inside this repository at `test-fullscreen-mss/`.

It uses the `capture` pybind module (not `mss`) and provides rolling FPS metrics, 1% lows, P99 frame time, dip counts, monitor listing, and cursor-crossing correlation telemetry.

Install Python-side dependencies for the monitor:

```bash
pip install -r test-fullscreen-mss/requirements.txt
```

List monitors with both 1-based and 0-based numbering, names, bounds, and refresh rates:

```powershell
python test-fullscreen-mss/performance_monitor.py --list-monitors --backend auto
```

Run the monitor against display 2 using Windows-style 1-based indexing:

```powershell
python test-fullscreen-mss/performance_monitor.py --monitor 2 --monitor-base 1 --backend auto --timeout-ms 3000
```

Useful tuning flags:

- `--dip-threshold-ms <ms>`: threshold used to count dips (default `16.7`).
- `--crossing-correlation-ms <ms>`: window used to correlate dips with monitor boundary crossings.
- `--idle-sleep-ms <ms>`: optional per-frame sleep to reduce CPU pressure.
- `--history-seconds <s>`: rolling metrics window.

## Usage

Capture one frame:

```bash
./capture_once --backend auto --monitor 0 --output screen.png
```

List targets:

```bash
./capture_once --list-targets
```

Capture a region relative to monitor 0:

```bash
./capture_once --monitor 0 --region 100,100,640,480 --output region.png
```

Wrap a captured frame in OpenCV without channel conversion:

```cpp
cv::Mat view(frame.height, frame.width, CV_8UC3, frame.bytes.data(), frame.stride_bytes);
```

Benchmark with structured metrics:

```bash
./benchmark_fps --monitor 0 --frames 300 --json
```

Benchmark with CI-style thresholds and an artifact file:

```bash
./benchmark_fps --monitor 0 --frames 300 --min-avg-fps 60 --max-frame-ms 50 --artifact benchmark-monitor0.json
```

Benchmark with the multi-monitor threshold preset:

```bash
./benchmark_fps --monitor 1 --frames 300 --threshold-preset multi-monitor --json
```

Run a sustained capture smoke test:

```bash
./capture_loop --monitor 0 --frames 600 --report-every 60
```

Run repeated backend init/shutdown stress coverage:

```bash
./capture_stress --monitor 0 --cycles 50 --frames-per-cycle 3 --json
```

### Command Line Options

`capture_once`:

```text
--backend <windows|macos|auto>   Capture backend (default: auto)
--monitor <index>                Monitor index (default: 0)
--region <x,y,w,h>               Capture region relative to the monitor
--timeout-ms <ms>                Frame timeout (default: 5000)
--list-targets                   Print target metadata and exit
--output <path>                  Output file path (default: capture.png)
--help                           Show help message
```

`benchmark_fps`:

```text
--backend <windows|macos|auto>   Capture backend (default: auto)
--monitor <index>                Monitor index (default: 0)
--frames <count>                 Number of frames to capture (default: 300)
--timeout-ms <ms>                Frame timeout (default: 3000)
--region <x,y,w,h>               Capture region relative to the monitor
--threshold-preset <name>         Thresholds: single-monitor, multi-monitor, smoke
--min-avg-fps <fps>              Minimum average FPS threshold (default: 60)
--max-avg-ms <ms>                Optional maximum average frame time threshold
--max-frame-ms <ms>              Maximum single-frame time threshold (default: 50)
--artifact <path>                Write JSON benchmark artifact to path
--json                           Print machine-readable metrics
--help                           Show help message
```

`capture_loop`:

```text
--backend <windows|macos|auto>   Capture backend (default: auto)
--monitor <index>                Monitor index (default: 0)
--frames <count>                 Number of frames to capture (default: 600)
--timeout-ms <ms>                Per-frame timeout (default: 3000)
--region <x,y,w,h>               Capture region relative to the monitor
--report-every <count>           Progress interval (default: 60, 0 disables)
--json                           Print machine-readable metrics
--help                           Show help message
```

`capture_stress`:

```text
--backend <windows|macos|auto>   Capture backend (default: auto)
--monitor <index>                Monitor index (default: 0)
--cycles <count>                 Init/shutdown cycles (default: 50)
--frames-per-cycle <count>       Frames captured per cycle (default: 3)
--timeout-ms <ms>                Per-frame timeout (default: 3000)
--region <x,y,w,h>               Capture region relative to the monitor
--json                           Print machine-readable metrics
--help                           Show help message
```

## Implementation Status

### Completed

- Error handling enums.
- Frame structure and BGR8 contract.
- Backend interface definition.
- API version constants.
- Portable time utilities.
- PNG writer for validation.
- Windows monitor enumeration with target metadata.
- Windows Graphics Capture + D3D11 capture.
- GPU to CPU BGR8 transfer.
- Windows region capture.
- Sustained capture loop smoke-test utility.
- Benchmark utility with optional JSON metrics, percentile timings, thresholds, and artifact output.
- Benchmark threshold presets for single-monitor, multi-monitor, and smoke runs.
- Repeated backend init/shutdown stress-test utility.
- macOS target enumeration with CoreGraphics metadata.
- macOS Screen Recording permission preflight.
- macOS ScreenCaptureKit monitor streaming with packed BGR8 output.
- macOS region capture, timeout, and stream-stop error handling.
- Python bindings exposing `list_targets()` and `CaptureSession` with NumPy BGR output.
- Shared frame conformance checks across capture tools for BGR8 format, stride, dimensions, byte size, and monotonic timestamps.
- GitHub Actions build matrix for Windows and macOS.

### Planned

- Preprocessing module for inference pipelines: BGR to RGB, resize, normalize, and tensor packing.
- CMake install/export packaging.

## Known Limitations

- Monitor capture only. Window capture is not implemented.
- HDR is not implemented. Output is SDR/BGR8.
- CPU-readable frames only. GPU-native export is not implemented.
- macOS capture requires Screen Recording permission and macOS 12.3+ for ScreenCaptureKit.
- Linux is unsupported.

## Design Rationale

1. CPU-readable BGR8 maps directly to OpenCV `CV_8UC3` and avoids repeated alpha stripping in downstream pipelines.
2. Capture, preprocessing, and inference remain separate so each layer can be optimized independently.
3. Platform-specific internals can change while the external frame contract stays stable.
4. Region capture is expressed as target configuration, so `grab_frame` keeps one portable signature.

## References

- [Windows.Graphics.Capture API](https://learn.microsoft.com/en-us/windows/uwp/audio-video-camera/screen-capture)
- [IGraphicsCaptureItemInterop](https://learn.microsoft.com/en-us/windows/win32/api/windows.graphics.capture.interop/nf-windows-graphics-capture-interop-igraphicscaptureiteminterop-createformonitor)
- [Direct3D 11 Staging Resources](https://learn.microsoft.com/en-us/windows/win32/api/d3d11/ne-d3d11-d3d11_usage)
- [ScreenCaptureKit](https://developer.apple.com/documentation/screencapturekit)
