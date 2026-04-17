# High-Performance Display Capture Library

A cross-platform C++ library for efficient display capture targeting Windows and Linux (Fedora/Wayland). Designed for 60+ FPS capture with BGRA8 pixel format suitable for ML inference pipelines (YOLO, custom classifiers).

## Architecture

### Core Principle
**Separate capture from preprocessing/inference.** The library provides a narrow, portable API:

```cpp
struct Frame {
    PixelFormat format;        // Currently: Bgra8Unorm
    uint32_t width, height;
    uint32_t stride_bytes;     // Tightly packed: width * 4
    uint64_t timestamp_ns;     // Monotonic nanoseconds
    std::vector<uint8_t> bytes;
};

class ICaptureBackend {
    virtual Error list_targets(std::vector<CaptureTarget>& out) = 0;
    virtual Error init(const CaptureTarget& target) = 0;
    virtual Error grab_frame(Frame& out, int timeout_ms) = 0;
    virtual Error shutdown() = 0;
};
```

### Platform Backends

**Windows (MVP target):**
- Uses Windows Graphics Capture + D3D11
- Direct monitor enumeration via `EnumDisplayMonitors`
- GPU → CPU transfer via staging textures
- Supports SDR/BGRA8 formats

**Linux (Fedora/Wayland - planned):**
- XDG Desktop Portal ScreenCast interface
- PipeWire for frame delivery
- Negotiates raw packed formats
- Normalizes to BGRA8 before export

## Building

### Prerequisites

**Windows:**
- Visual Studio 2019+ or Clang-CL
- Windows 10.0.19041+ (Windows 10 version 1903+)
- CMake 3.16+

**Linux:**
- GCC 9+ or Clang 10+
- libpipewire-0.3-dev
- xdg-desktop-portal-dev
- CMake 3.16+

### Build

```bash
mkdir build
cd build
cmake ..
cmake --build . --config Release
```

## Usage

### Basic Example

```bash
./capture_once --backend auto --monitor 0 --output screen.ppm
```

### Command Line Options

```
--backend <windows|linux|auto>   Capture backend (default: auto)
--monitor <index>                 Monitor index (default: 0)
--output <path>                   Output file path (default: capture.ppm)
--help                            Show help message
```

## Implementation Status

### Completed (Milestone A)
- [x] Error handling enums
- [x] Frame structure and contracts
- [x] Backend interface definition
- [x] Portable time utilities
- [x] PPM writer for validation

### In Progress (Milestone B-C)
- [ ] Windows monitor enumeration (done)
- [ ] Windows Graphics Capture + D3D11 (started)
- [ ] GPU to CPU BGRA8 transfer

### Planned (Milestone D-F)
- [ ] Linux portal + PipeWire backend
- [ ] Unified factory and examples
- [ ] Preprocessing for inference (BGRA → RGB/BGR, resize, normalize)

## Performance Target

- **Minimum:** 60 FPS on typical desktop hardware
- **Target:** 120+ FPS on modern GPUs
- **Resolution:** Full monitor resolution without downscaling

## Known Limitations

- **MVP does not include:**
  - Window capture (monitor capture only)
  - HDR support (SDR/BGRA8 only)
  - GPU-native export (CPU BGRA8 only)
  - Zero-copy dmabuf interop
  - Exclusive fullscreen handling
  - X11 support (Wayland first)

## Design Rationale

1. **CPU-readable BGRA8:** Matches Windows desktop capture native format; simple conversion path to RGB/BGR for inference
2. **Separate layers:** Capture → Preprocessing → Inference allows independent optimization
3. **Platform abstraction:** Internal implementation can be GPU-optimized; external API stays simple
4. **Narrow interface:** Reduces maintenance burden and future compatibility issues

## Next Steps

1. Complete Windows backend with full WGC integration
2. Validate 60+ FPS capture on target hardware
3. Implement Linux backend
4. Add preprocessing module for ML pipelines
5. Benchmark and optimize GPU transfer paths

## References

- [Windows.Graphics.Capture API](https://learn.microsoft.com/en-us/windows/uwp/audio-video-camera/screen-capture)
- [IGraphicsCaptureItemInterop](https://learn.microsoft.com/en-us/windows/win32/api/windows.graphics.capture.interop/nf-windows-graphics-capture-interop-igraphicscaptureiteminterop-createformonitor)
- [Direct3D 11 Staging Resources](https://learn.microsoft.com/en-us/windows/win32/api/d3d11/ne-d3d11-d3d11_usage)
- [XDG Desktop Portal ScreenCast](https://flatpak.github.io/xdg-desktop-portal/docs/doc-org.freedesktop.portal.ScreenCast.html)
- [PipeWire Core Connection](https://docs.pipewire.org/core_8h.html)
