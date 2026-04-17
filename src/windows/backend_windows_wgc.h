#pragma once

#include "capture/backend.h"
#include "monitor_enum.h"

#include <d3d11.h>
#include <wrl/client.h>
#include <memory>
#include <mutex>
#include <condition_variable>

#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Graphics.Capture.h>
#include <winrt/Windows.Graphics.DirectX.h>
#include <winrt/Windows.Graphics.DirectX.Direct3D11.h>

using Microsoft::WRL::ComPtr;

namespace capture {

class WindowsWgcBackend : public ICaptureBackend {
public:
    WindowsWgcBackend();
    ~WindowsWgcBackend() override;
    
    Error list_targets(std::vector<CaptureTarget>& out_targets) override;
    Error init(const CaptureTarget& target) override;
    Error grab_frame(Frame& out_frame, int timeout_ms = 1000) override;
    Error shutdown() override;

private:
    // D3D11 resources
    ComPtr<ID3D11Device> device_;
    ComPtr<ID3D11DeviceContext> context_;
    
    // WinRT Direct3D device
    winrt::Windows::Graphics::DirectX::Direct3D11::IDirect3DDevice winrt_device_{ nullptr };
    
    // WGC resources
    winrt::Windows::Graphics::Capture::GraphicsCaptureItem capture_item_{ nullptr };
    winrt::Windows::Graphics::Capture::Direct3D11CaptureFramePool frame_pool_{ nullptr };
    winrt::Windows::Graphics::Capture::GraphicsCaptureSession session_{ nullptr };
    winrt::event_token frame_arrived_token_{};
    winrt::event_token item_closed_token_{};
    bool item_closed_registered_ = false;
    
    // Frame synchronization
    std::mutex frame_mutex_;
    std::condition_variable frame_cv_;
    ComPtr<ID3D11Texture2D> latest_frame_texture_;
    bool frame_ready_ = false;
    bool target_lost_ = false;
    
    // Staging texture (reused across frames)
    ComPtr<ID3D11Texture2D> staging_texture_;
    uint32_t staging_width_ = 0;
    uint32_t staging_height_ = 0;
    
    // State
    bool initialized_ = false;
    bool session_started_ = false;
    HMONITOR monitor_handle_ = nullptr;
    CaptureTarget current_target_;
    
    // Internal helpers
    Error start_capture();
    Error stop_capture();
    Error read_frame_to_cpu(ID3D11Texture2D* source, Frame& out_frame);
    void on_frame_arrived(
        winrt::Windows::Graphics::Capture::Direct3D11CaptureFramePool const& sender,
        winrt::Windows::Foundation::IInspectable const& args);
    void on_capture_item_closed(
        winrt::Windows::Graphics::Capture::GraphicsCaptureItem const& sender,
        winrt::Windows::Foundation::IInspectable const& args);
    Error ensure_staging_texture(uint32_t width, uint32_t height, DXGI_FORMAT format);
};

}  // namespace capture
