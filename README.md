# High-Performance Display Capture Library

A cross-platform C++ display capture library targeting Windows first and macOS next. The exported frame contract is CPU-readable BGRA8, suitable for ML inference pipelines such as YOLO and custom classifiers.

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
    PixelFormat format;        // Bgra8Unorm
    uint32_t width, height;
    uint32_t stride_bytes;     // tightly packed: width * 4
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

The current API version is `0.2.0`, exposed through `CAPTURE_API_VERSION_MAJOR`, `CAPTURE_API_VERSION_MINOR`, and `CAPTURE_API_VERSION_PATCH` in `capture/backend.h`.

## Platform Backends

**Windows: implemented**

- Windows Graphics Capture with D3D11.
- Monitor enumeration through `EnumDisplayMonitors`.
- GPU to CPU transfer via reusable staging textures.
- SDR/BGRA8 output.
- Optional region capture with bounds validation.
- Target metadata: monitor index, name, origin, and size.

**macOS: planned**

- ScreenCaptureKit backend placeholder is wired into the factory and CMake build.
- Monitor enumeration, permission handling, and BGRA8 normalization are planned.

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
- macOS with ScreenCaptureKit support for the future backend.
- CMake 3.16+.

### Build

```bash
mkdir build
cd build
cmake ..
cmake --build . --config Release
```

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

Benchmark with structured metrics:

```bash
./benchmark_fps --monitor 0 --frames 300 --json
```

Run a sustained capture smoke test:

```bash
./capture_loop --monitor 0 --frames 600 --report-every 60
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

## Implementation Status

### Completed

- Error handling enums.
- Frame structure and BGRA8 contract.
- Backend interface definition.
- API version constants.
- Portable time utilities.
- PNG writer for validation.
- Windows monitor enumeration with target metadata.
- Windows Graphics Capture + D3D11 capture.
- GPU to CPU BGRA8 transfer.
- Windows region capture.
- Sustained capture loop smoke-test utility.
- Benchmark utility with optional JSON metrics.

### Planned

- macOS ScreenCaptureKit implementation.
- Sustained capture loop sample.
- Long-running reliability tests.
- Preprocessing module for inference pipelines: BGRA to RGB/BGR, resize, normalize, and tensor packing.
- CMake install/export packaging.

## Known Limitations

- Monitor capture only. Window capture is not implemented.
- HDR is not implemented. Output is SDR/BGRA8.
- CPU-readable frames only. GPU-native export is not implemented.
- macOS backend is currently a placeholder.
- Linux is unsupported.

## Design Rationale

1. CPU-readable BGRA8 maps cleanly to common desktop capture paths and simple RGB/BGR conversion.
2. Capture, preprocessing, and inference remain separate so each layer can be optimized independently.
3. Platform-specific internals can change while the external frame contract stays stable.
4. Region capture is expressed as target configuration, so `grab_frame` keeps one portable signature.

## References

- [Windows.Graphics.Capture API](https://learn.microsoft.com/en-us/windows/uwp/audio-video-camera/screen-capture)
- [IGraphicsCaptureItemInterop](https://learn.microsoft.com/en-us/windows/win32/api/windows.graphics.capture.interop/nf-windows-graphics-capture-interop-igraphicscaptureiteminterop-createformonitor)
- [Direct3D 11 Staging Resources](https://learn.microsoft.com/en-us/windows/win32/api/d3d11/ne-d3d11-d3d11_usage)
- [ScreenCaptureKit](https://developer.apple.com/documentation/screencapturekit)
