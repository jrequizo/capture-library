#pragma once

#include <d3d11.h>
#include <cstdint>

namespace capture::d3d11_helpers {

// Helper to create a D3D11 device for capture
HRESULT create_capture_device(ID3D11Device** out_device, ID3D11DeviceContext** out_context);

// Helper to create a staging texture for CPU readback
HRESULT create_staging_texture(
    ID3D11Device* device,
    ID3D11Texture2D* source_texture,
    ID3D11Texture2D** out_staging_texture
);

}  // namespace capture::d3d11_helpers
