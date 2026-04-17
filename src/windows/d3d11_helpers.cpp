#include "d3d11_helpers.h"

#include <windows.h>
#include <d3d11.h>
#include <dxgi.h>
#include <wrl/client.h>

using Microsoft::WRL::ComPtr;

namespace capture::d3d11_helpers {

HRESULT create_capture_device(ID3D11Device** out_device, ID3D11DeviceContext** out_context) {
    ComPtr<ID3D11Device> device;
    ComPtr<ID3D11DeviceContext> context;
    
    D3D_FEATURE_LEVEL feature_levels[] = {
        D3D_FEATURE_LEVEL_11_1,
        D3D_FEATURE_LEVEL_11_0,
        D3D_FEATURE_LEVEL_10_1,
        D3D_FEATURE_LEVEL_10_0
    };
    
    UINT flags = 0;
#ifdef _DEBUG
    flags |= D3D11_CREATE_DEVICE_DEBUG;
#endif
    
    HRESULT hr = D3D11CreateDevice(
        nullptr,
        D3D_DRIVER_TYPE_HARDWARE,
        nullptr,
        flags,
        feature_levels,
        ARRAYSIZE(feature_levels),
        D3D11_SDK_VERSION,
        device.GetAddressOf(),
        nullptr,
        context.GetAddressOf()
    );
    
    if (SUCCEEDED(hr)) {
        *out_device = device.Detach();
        *out_context = context.Detach();
    }
    
    return hr;
}

HRESULT create_staging_texture(
    ID3D11Device* device,
    ID3D11Texture2D* source_texture,
    ID3D11Texture2D** out_staging_texture) {
    
    if (!device || !source_texture || !out_staging_texture) {
        return E_INVALIDARG;
    }
    
    D3D11_TEXTURE2D_DESC desc;
    source_texture->GetDesc(&desc);
    
    // Create staging texture
    desc.Usage = D3D11_USAGE_STAGING;
    desc.BindFlags = 0;
    desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    
    ComPtr<ID3D11Texture2D> staging;
    HRESULT hr = device->CreateTexture2D(&desc, nullptr, staging.GetAddressOf());
    
    if (SUCCEEDED(hr)) {
        *out_staging_texture = staging.Detach();
    }
    
    return hr;
}

}  // namespace capture::d3d11_helpers
