#include "wgc_interop.h"

#include <inspectable.h>

using namespace winrt;
using namespace winrt::Windows::Graphics;
using namespace winrt::Windows::Graphics::Capture;
using namespace winrt::Windows::Graphics::DirectX;
using namespace winrt::Windows::Graphics::DirectX::Direct3D11;
using Microsoft::WRL::ComPtr;

namespace capture::wgc_interop {

IDirect3DDevice create_direct3d_device(ID3D11Device* d3d_device) {
    // Get DXGI device from D3D11 device
    ComPtr<IDXGIDevice> dxgi_device;
    HRESULT hr = d3d_device->QueryInterface(IID_PPV_ARGS(&dxgi_device));
    if (FAILED(hr)) {
        throw winrt::hresult_error(hr, L"Failed to get IDXGIDevice from ID3D11Device");
    }

    // Use CreateDirect3D11DeviceFromDXGIDevice to get a WinRT device
    winrt::com_ptr<::IInspectable> inspectable;
    hr = CreateDirect3D11DeviceFromDXGIDevice(dxgi_device.Get(), inspectable.put());
    if (FAILED(hr)) {
        throw winrt::hresult_error(hr, L"CreateDirect3D11DeviceFromDXGIDevice failed");
    }

    return inspectable.as<IDirect3DDevice>();
}

HRESULT get_d3d_texture_from_surface(
    IDirect3DSurface const& surface,
    ID3D11Texture2D** out_texture) {
    
    // QI the WinRT surface for the interop interface to get native D3D11 texture
    auto interop = surface.as<::Windows::Graphics::DirectX::Direct3D11::IDirect3DDxgiInterfaceAccess>();
    return interop->GetInterface(IID_PPV_ARGS(out_texture));
}

GraphicsCaptureItem create_capture_item_for_monitor(HMONITOR monitor) {
    // Use the interop factory to create a capture item from an HMONITOR
    auto interop_factory = winrt::get_activation_factory<GraphicsCaptureItem, IGraphicsCaptureItemInterop>();
    
    GraphicsCaptureItem item{ nullptr };
    HRESULT hr = interop_factory->CreateForMonitor(
        monitor,
        winrt::guid_of<ABI::Windows::Graphics::Capture::IGraphicsCaptureItem>(),
        winrt::put_abi(item));
    
    if (FAILED(hr)) {
        throw winrt::hresult_error(hr, L"CreateForMonitor failed");
    }
    
    return item;
}

}  // namespace capture::wgc_interop
