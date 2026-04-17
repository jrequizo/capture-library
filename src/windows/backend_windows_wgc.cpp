#include "backend_windows_wgc.h"
#include "d3d11_helpers.h"
#include "wgc_interop.h"
#include "../common/monotonic_time.h"

#include <d3d11.h>
#include <wrl/client.h>
#include <chrono>
#include <cstring>
#include <string>

using Microsoft::WRL::ComPtr;

namespace capture {

namespace {

bool is_valid_region(const CaptureRect& region, uint32_t width, uint32_t height) {
    if (region.width == 0 || region.height == 0) {
        return false;
    }
    if (region.x < 0 || region.y < 0) {
        return false;
    }

    const uint64_t x = static_cast<uint64_t>(region.x);
    const uint64_t y = static_cast<uint64_t>(region.y);
    return x + region.width <= width && y + region.height <= height;
}

std::string region_error_message(const CaptureRect& region, uint32_t width, uint32_t height) {
    return "Region (" + std::to_string(region.x) + "," + std::to_string(region.y) +
           " " + std::to_string(region.width) + "x" + std::to_string(region.height) +
           ") is outside target size " + std::to_string(width) + "x" +
           std::to_string(height);
}

}  // namespace

WindowsWgcBackend::WindowsWgcBackend() {}

WindowsWgcBackend::~WindowsWgcBackend() {
    shutdown();
}

Error WindowsWgcBackend::list_targets(std::vector<CaptureTarget>& out_targets) {
    auto monitors = enumerate_monitors();
    
    out_targets.clear();
    for (const auto& mon : monitors) {
        CaptureTarget target;
        target.kind = CaptureTarget::Kind::Monitor;
        target.id = mon.index;
        target.name = mon.name;
        target.bounds.x = mon.bounds.left;
        target.bounds.y = mon.bounds.top;
        target.bounds.width = static_cast<uint32_t>(mon.bounds.right - mon.bounds.left);
        target.bounds.height = static_cast<uint32_t>(mon.bounds.bottom - mon.bounds.top);
        target.has_bounds = true;
        out_targets.push_back(target);
    }
    
    return Error(ErrorCode::Success);
}

Error WindowsWgcBackend::init(const CaptureTarget& target) {
    if (target.kind != CaptureTarget::Kind::Monitor) {
        return Error(ErrorCode::NotSupported,
                     "Only monitor capture is supported in MVP");
    }
    
    // Ensure WinRT is initialized on this thread
    try {
        winrt::init_apartment(winrt::apartment_type::multi_threaded);
    } catch (winrt::hresult_error const&) {
        // Already initialized, that's okay
    }
    
    // Get monitor handle
    HMONITOR monitor = get_monitor_by_index(static_cast<uint32_t>(target.id));
    if (!monitor) {
        return Error(ErrorCode::InvalidMonitorIndex,
                     "Monitor index out of range");
    }
    
    monitor_handle_ = monitor;
    current_target_ = target;
    
    // Create D3D11 device
    if (!device_) {
        HRESULT hr = d3d11_helpers::create_capture_device(
            device_.GetAddressOf(),
            context_.GetAddressOf()
        );
        if (FAILED(hr)) {
            return Error(ErrorCode::InitializationFailed,
                         "Failed to create D3D11 device");
        }
    }
    
    // Create WinRT device wrapper
    try {
        winrt_device_ = wgc_interop::create_direct3d_device(device_.Get());
    } catch (winrt::hresult_error const& e) {
        auto msg = winrt::to_string(e.message());
        return Error(ErrorCode::InitializationFailed,
                     "Failed to create WinRT Direct3D device: " + msg);
    }
    
    // Create capture item for this monitor
    try {
        capture_item_ = wgc_interop::create_capture_item_for_monitor(monitor_handle_);
    } catch (winrt::hresult_error const& e) {
        auto msg = winrt::to_string(e.message());
        return Error(ErrorCode::BackendFailure,
                     "Failed to create capture item for monitor: " + msg);
    }

    if (target.has_region) {
        auto size = capture_item_.Size();
        if (!is_valid_region(target.region,
                             static_cast<uint32_t>(size.Width),
                             static_cast<uint32_t>(size.Height))) {
            return Error(ErrorCode::InvalidRegion,
                         region_error_message(target.region,
                                              static_cast<uint32_t>(size.Width),
                                              static_cast<uint32_t>(size.Height)));
        }
    }

    {
        std::lock_guard<std::mutex> lock(frame_mutex_);
        frame_ready_ = false;
        target_lost_ = false;
        latest_frame_texture_ = nullptr;
    }

    item_closed_token_ = capture_item_.Closed(
        [this](auto const& sender, auto const& args) {
            on_capture_item_closed(sender, args);
        });
    item_closed_registered_ = true;
    
    initialized_ = true;
    return Error(ErrorCode::Success);
}

Error WindowsWgcBackend::start_capture() {
    if (session_started_) {
        return Error(ErrorCode::Success);
    }
    
    try {
        auto size = capture_item_.Size();
        
        frame_pool_ = winrt::Windows::Graphics::Capture::Direct3D11CaptureFramePool::CreateFreeThreaded(
            winrt_device_,
            winrt::Windows::Graphics::DirectX::DirectXPixelFormat::B8G8R8A8UIntNormalized,
            2,
            size);
        
        frame_arrived_token_ = frame_pool_.FrameArrived(
            [this](auto const& sender, auto const& args) {
                on_frame_arrived(sender, args);
            });
        
        session_ = frame_pool_.CreateCaptureSession(capture_item_);
        
        // IsBorderRequired requires Windows 11 / SDK 10.0.20348+
        // IsCursorCaptureEnabled requires Windows 10 20H1 / SDK 10.0.19041+
        // Use runtime check via QueryInterface to avoid crashes
        try {
            auto session3 = session_.try_as<winrt::Windows::Graphics::Capture::IGraphicsCaptureSession3>();
            if (session3) {
                session3.IsBorderRequired(false);
            }
        } catch (...) {
            // Feature not available on this Windows version
        }
        
        try {
            auto session2 = session_.try_as<winrt::Windows::Graphics::Capture::IGraphicsCaptureSession2>();
            if (session2) {
                session2.IsCursorCaptureEnabled(false);
            }
        } catch (...) {
            // Feature not available on this Windows version
        }
        
        session_.StartCapture();
        session_started_ = true;
    } catch (winrt::hresult_error const& e) {
        auto msg = winrt::to_string(e.message());
        return Error(ErrorCode::BackendFailure,
                     "Failed to start capture session: " + msg);
    } catch (std::exception const& e) {
        return Error(ErrorCode::BackendFailure,
                     std::string("Failed to start capture: ") + e.what());
    }
    
    return Error(ErrorCode::Success);
}

Error WindowsWgcBackend::stop_capture() {
    if (!session_started_) {
        return Error(ErrorCode::Success);
    }
    
    try {
        if (session_) {
            session_.Close();
            session_ = nullptr;
        }
        if (frame_pool_) {
            frame_pool_.FrameArrived(frame_arrived_token_);
            frame_pool_.Close();
            frame_pool_ = nullptr;
        }
    } catch (...) {
        // Best-effort cleanup
    }
    
    session_started_ = false;
    return Error(ErrorCode::Success);
}

void WindowsWgcBackend::on_frame_arrived(
    winrt::Windows::Graphics::Capture::Direct3D11CaptureFramePool const& sender,
    winrt::Windows::Foundation::IInspectable const& args) {
    
    auto frame = sender.TryGetNextFrame();
    if (!frame) return;
    
    auto surface = frame.Surface();
    
    ComPtr<ID3D11Texture2D> texture;
    HRESULT hr = wgc_interop::get_d3d_texture_from_surface(surface, texture.GetAddressOf());
    
    if (SUCCEEDED(hr) && texture) {
        std::lock_guard<std::mutex> lock(frame_mutex_);
        latest_frame_texture_ = texture;
        frame_ready_ = true;
        frame_cv_.notify_one();
    }
    
    frame.Close();
}

void WindowsWgcBackend::on_capture_item_closed(
    winrt::Windows::Graphics::Capture::GraphicsCaptureItem const& sender,
    winrt::Windows::Foundation::IInspectable const& args) {
    (void)sender;
    (void)args;

    std::lock_guard<std::mutex> lock(frame_mutex_);
    target_lost_ = true;
    frame_ready_ = false;
    latest_frame_texture_ = nullptr;
    frame_cv_.notify_all();
}

Error WindowsWgcBackend::grab_frame(Frame& out_frame, int timeout_ms) {
    if (!initialized_) {
        return Error(ErrorCode::BackendFailure,
                     "Backend not initialized");
    }
    if (timeout_ms < 0) {
        return Error(ErrorCode::Timeout, "Frame timeout must be non-negative");
    }
    
    // Start capture if not running
    Error err = start_capture();
    if (err.is_error()) return err;
    
    // Wait for a frame
    ComPtr<ID3D11Texture2D> texture;
    {
        std::unique_lock<std::mutex> lock(frame_mutex_);
        frame_ready_ = false;
        
        bool got_frame = frame_cv_.wait_for(
            lock,
            std::chrono::milliseconds(timeout_ms),
            [this] { return frame_ready_ || target_lost_; });

        if (target_lost_) {
            return Error(ErrorCode::TargetLost, "Capture target was closed or disconnected");
        }
        
        if (!got_frame) {
            return Error(ErrorCode::Timeout,
                         "Timed out waiting for frame after " +
                         std::to_string(timeout_ms) + " ms");
        }
        
        texture = latest_frame_texture_;
        latest_frame_texture_ = nullptr;
        frame_ready_ = false;
    }
    
    if (!texture) {
        return Error(ErrorCode::BackendFailure, "Frame texture is null");
    }
    
    // Read GPU texture to CPU
    return read_frame_to_cpu(texture.Get(), out_frame);
}

Error WindowsWgcBackend::ensure_staging_texture(uint32_t width, uint32_t height, DXGI_FORMAT format) {
    if (staging_texture_ && staging_width_ == width && staging_height_ == height) {
        return Error(ErrorCode::Success);
    }
    
    staging_texture_ = nullptr;
    
    D3D11_TEXTURE2D_DESC desc = {};
    desc.Width = width;
    desc.Height = height;
    desc.MipLevels = 1;
    desc.ArraySize = 1;
    desc.Format = format;
    desc.SampleDesc.Count = 1;
    desc.Usage = D3D11_USAGE_STAGING;
    desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    
    HRESULT hr = device_->CreateTexture2D(&desc, nullptr, staging_texture_.GetAddressOf());
    if (FAILED(hr)) {
        return Error(ErrorCode::BackendFailure, "Failed to create staging texture");
    }
    
    staging_width_ = width;
    staging_height_ = height;
    return Error(ErrorCode::Success);
}

Error WindowsWgcBackend::read_frame_to_cpu(ID3D11Texture2D* source, Frame& out_frame) {
    D3D11_TEXTURE2D_DESC desc;
    source->GetDesc(&desc);
    
    // Verify format is BGRA8
    if (desc.Format != DXGI_FORMAT_B8G8R8A8_UNORM) {
        return Error(ErrorCode::UnsupportedPixelFormat,
                     "Captured texture is not BGRA8 format");
    }
    
    // Ensure staging texture exists with correct dimensions
    Error err = ensure_staging_texture(desc.Width, desc.Height, desc.Format);
    if (err.is_error()) return err;
    
    // Copy GPU texture to staging
    context_->CopyResource(staging_texture_.Get(), source);
    
    // Map staging texture for CPU read
    D3D11_MAPPED_SUBRESOURCE mapped = {};
    HRESULT hr = context_->Map(staging_texture_.Get(), 0, D3D11_MAP_READ, 0, &mapped);
    if (FAILED(hr)) {
        return Error(ErrorCode::BackendFailure, "Failed to map staging texture");
    }
    
    CaptureRect copy_region;
    copy_region.x = 0;
    copy_region.y = 0;
    copy_region.width = desc.Width;
    copy_region.height = desc.Height;

    if (current_target_.has_region) {
        if (!is_valid_region(current_target_.region, desc.Width, desc.Height)) {
            context_->Unmap(staging_texture_.Get(), 0);
            return Error(ErrorCode::InvalidRegion,
                         region_error_message(current_target_.region, desc.Width, desc.Height));
        }
        copy_region = current_target_.region;
    }

    // Copy data respecting GPU row pitch.
    uint32_t stride = copy_region.width * 4;  // BGRA8 = 4 bytes per pixel
    uint8_t* src = static_cast<uint8_t*>(mapped.pData);
    uint8_t* dst = nullptr;
    
    out_frame.width = copy_region.width;
    out_frame.height = copy_region.height;
    out_frame.stride_bytes = stride;
    out_frame.format = PixelFormat::Bgra8Unorm;
    out_frame.timestamp_ns = monotonic_now_ns();
    out_frame.bytes.resize(stride * copy_region.height);
    dst = out_frame.bytes.data();
    
    // Copy row-by-row, respecting GPU's row pitch
    const uint32_t src_x = static_cast<uint32_t>(copy_region.x);
    const uint32_t src_y = static_cast<uint32_t>(copy_region.y);
    for (uint32_t y = 0; y < copy_region.height; ++y) {
        memcpy(dst + y * stride,
               src + (src_y + y) * mapped.RowPitch + src_x * 4,
               stride);
    }
    
    context_->Unmap(staging_texture_.Get(), 0);
    
    return Error(ErrorCode::Success);
}

Error WindowsWgcBackend::shutdown() {
    stop_capture();

    if (capture_item_ && item_closed_registered_) {
        try {
            capture_item_.Closed(item_closed_token_);
        } catch (...) {
            // Best-effort cleanup
        }
        item_closed_registered_ = false;
    }
    
    if (context_) {
        context_->ClearState();
        context_ = nullptr;
    }
    if (device_) {
        device_ = nullptr;
    }
    
    session_ = nullptr;
    frame_pool_ = nullptr;
    capture_item_ = nullptr;
    winrt_device_ = nullptr;
    staging_texture_ = nullptr;

    {
        std::lock_guard<std::mutex> lock(frame_mutex_);
        latest_frame_texture_ = nullptr;
        frame_ready_ = false;
        target_lost_ = false;
    }
    
    initialized_ = false;
    return Error(ErrorCode::Success);
}

}  // namespace capture
