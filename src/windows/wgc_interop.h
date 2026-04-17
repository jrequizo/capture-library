#pragma once

// WGC interop helpers:
// - Convert D3D11 device to WinRT IDirect3DDevice
// - Get D3D11 texture from WinRT IDirect3DSurface
// - Create GraphicsCaptureItem from HMONITOR

#include <d3d11.h>
#include <dxgi.h>
#include <wrl/client.h>

#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Graphics.Capture.h>
#include <winrt/Windows.Graphics.DirectX.h>
#include <winrt/Windows.Graphics.DirectX.Direct3D11.h>

#include <windows.graphics.capture.interop.h>
#include <Windows.Graphics.DirectX.Direct3D11.interop.h>

namespace capture::wgc_interop {

// Create a WinRT IDirect3DDevice from a D3D11 device
winrt::Windows::Graphics::DirectX::Direct3D11::IDirect3DDevice
create_direct3d_device(ID3D11Device* d3d_device);

// Get the underlying D3D11 texture from a WinRT Direct3D11CaptureFrame
HRESULT get_d3d_texture_from_surface(
    winrt::Windows::Graphics::DirectX::Direct3D11::IDirect3DSurface const& surface,
    ID3D11Texture2D** out_texture);

// Create a GraphicsCaptureItem for a monitor
winrt::Windows::Graphics::Capture::GraphicsCaptureItem
create_capture_item_for_monitor(HMONITOR monitor);

}  // namespace capture::wgc_interop
