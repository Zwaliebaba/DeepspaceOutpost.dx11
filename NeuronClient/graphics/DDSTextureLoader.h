#pragma once

// Native DDS -> ID3D11Texture2D + shader-resource-view loader (Neuron::Graphics).
//
// Adapted from Microsoft's DirectXTK / DirectXTex DDSTextureLoader11 (MIT License),
// trimmed to the 2D textures this game ships: uncompressed RGB(A)/BGR(A), BC1-BC3
// (DXT1/3/5), and the DX10 extended header. Gives TextureManager its own DDS decoding
// so it no longer depends on the legacy platform/Image loader.
//
// When generateMips is true and the file has no mip chain (and the format is not
// block-compressed), a full mip chain is generated on the GPU - needed so tall detailed
// textures (e.g. the interface gradient + 1px scanlines) filter cleanly when minified.

#include "GraphicsCore.h" // <d3d11_4.h> + winrt

#include <cstdint>

namespace Neuron::Graphics
{
  // Create a 2D texture + SRV from in-memory DDS bytes. Returns an HRESULT; on success
  // *outTexture and *outSRV are AddRef'd (caller owns). context may be null (then no mip
  // generation). Non-2D DDS (volume/cube) and unsupported formats fail with E_FAIL.
  HRESULT CreateDDSTextureFromMemory(ID3D11Device* device, ID3D11DeviceContext* context, const uint8_t* ddsData, size_t ddsDataSize,
                                     ID3D11Texture2D** outTexture, ID3D11ShaderResourceView** outSRV, bool generateMips = true) noexcept;
}
