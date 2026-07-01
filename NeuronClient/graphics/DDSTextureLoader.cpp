#include "pch.h"
#include "DDSTextureLoader.h"

#include <algorithm>
#include <cstring>

// Adapted from Microsoft's DirectXTK / DirectXTex DDSTextureLoader11 (MIT License).
// Only the 2D texture path and the formats this game uses are implemented.

namespace Neuron::Graphics
{
  namespace
  {
    constexpr uint32_t kDdsMagic = 0x20534444; // "DDS "

#pragma pack(push, 1)
    struct DDS_PIXELFORMAT
    {
      uint32_t size;
      uint32_t flags;
      uint32_t fourCC;
      uint32_t RGBBitCount;
      uint32_t RBitMask;
      uint32_t GBitMask;
      uint32_t BBitMask;
      uint32_t ABitMask;
    };

    struct DDS_HEADER
    {
      uint32_t size;
      uint32_t flags;
      uint32_t height;
      uint32_t width;
      uint32_t pitchOrLinearSize;
      uint32_t depth;
      uint32_t mipMapCount;
      uint32_t reserved1[11];
      DDS_PIXELFORMAT ddspf;
      uint32_t caps;
      uint32_t caps2;
      uint32_t caps3;
      uint32_t caps4;
      uint32_t reserved2;
    };

    struct DDS_HEADER_DXT10
    {
      uint32_t dxgiFormat;
      uint32_t resourceDimension;
      uint32_t miscFlag;
      uint32_t arraySize;
      uint32_t miscFlags2;
    };
#pragma pack(pop)

    static_assert(sizeof(DDS_PIXELFORMAT) == 32, "DDS_PIXELFORMAT layout");
    static_assert(sizeof(DDS_HEADER) == 124, "DDS_HEADER layout");
    static_assert(sizeof(DDS_HEADER_DXT10) == 20, "DDS_HEADER_DXT10 layout");

    // DDS_PIXELFORMAT.flags
    constexpr uint32_t DDS_FOURCC = 0x4;
    constexpr uint32_t DDS_RGB = 0x40;
    constexpr uint32_t DDS_LUMINANCE = 0x20000;
    constexpr uint32_t DDS_ALPHA = 0x2;

    // DDS_HEADER.caps2 (cube/volume) - we reject these.
    constexpr uint32_t DDS_CUBEMAP = 0x200;
    constexpr uint32_t DDS_VOLUME = 0x200000;

    constexpr uint32_t MakeFourCC(char a, char b, char c, char d)
    {
      return static_cast<uint32_t>(static_cast<uint8_t>(a)) | (static_cast<uint32_t>(static_cast<uint8_t>(b)) << 8) |
             (static_cast<uint32_t>(static_cast<uint8_t>(c)) << 16) | (static_cast<uint32_t>(static_cast<uint8_t>(d)) << 24);
    }

    bool IsBitMask(const DDS_PIXELFORMAT& pf, uint32_t r, uint32_t g, uint32_t b, uint32_t a)
    {
      return pf.RBitMask == r && pf.GBitMask == g && pf.BBitMask == b && pf.ABitMask == a;
    }

    DXGI_FORMAT GetDXGIFormat(const DDS_PIXELFORMAT& pf)
    {
      if (pf.flags & DDS_RGB)
      {
        switch (pf.RGBBitCount)
        {
        case 32:
          if (IsBitMask(pf, 0x000000ff, 0x0000ff00, 0x00ff0000, 0xff000000)) return DXGI_FORMAT_R8G8B8A8_UNORM;
          if (IsBitMask(pf, 0x00ff0000, 0x0000ff00, 0x000000ff, 0xff000000)) return DXGI_FORMAT_B8G8R8A8_UNORM;
          if (IsBitMask(pf, 0x00ff0000, 0x0000ff00, 0x000000ff, 0x00000000)) return DXGI_FORMAT_B8G8R8X8_UNORM;
          if (IsBitMask(pf, 0x3ff00000, 0x000ffc00, 0x000003ff, 0xc0000000)) return DXGI_FORMAT_R10G10B10A2_UNORM;
          if (IsBitMask(pf, 0x0000ffff, 0xffff0000, 0x00000000, 0x00000000)) return DXGI_FORMAT_R16G16_UNORM;
          if (IsBitMask(pf, 0xffffffff, 0x00000000, 0x00000000, 0x00000000)) return DXGI_FORMAT_R32_FLOAT;
          break;
        case 16:
          if (IsBitMask(pf, 0x7c00, 0x03e0, 0x001f, 0x8000)) return DXGI_FORMAT_B5G5R5A1_UNORM;
          if (IsBitMask(pf, 0xf800, 0x07e0, 0x001f, 0x0000)) return DXGI_FORMAT_B5G6R5_UNORM;
          if (IsBitMask(pf, 0x0f00, 0x00f0, 0x000f, 0xf000)) return DXGI_FORMAT_B4G4R4A4_UNORM;
          break;
        default: break;
        }
      }
      else if (pf.flags & DDS_LUMINANCE)
      {
        if (pf.RGBBitCount == 8 && IsBitMask(pf, 0xff, 0, 0, 0)) return DXGI_FORMAT_R8_UNORM;
        if (pf.RGBBitCount == 16 && IsBitMask(pf, 0xffff, 0, 0, 0)) return DXGI_FORMAT_R16_UNORM;
        if (pf.RGBBitCount == 16 && IsBitMask(pf, 0x00ff, 0, 0, 0xff00)) return DXGI_FORMAT_R8G8_UNORM;
      }
      else if (pf.flags & DDS_ALPHA)
      {
        if (pf.RGBBitCount == 8) return DXGI_FORMAT_A8_UNORM;
      }
      else if (pf.flags & DDS_FOURCC)
      {
        if (pf.fourCC == MakeFourCC('D', 'X', 'T', '1')) return DXGI_FORMAT_BC1_UNORM;
        if (pf.fourCC == MakeFourCC('D', 'X', 'T', '2')) return DXGI_FORMAT_BC2_UNORM;
        if (pf.fourCC == MakeFourCC('D', 'X', 'T', '3')) return DXGI_FORMAT_BC2_UNORM;
        if (pf.fourCC == MakeFourCC('D', 'X', 'T', '4')) return DXGI_FORMAT_BC3_UNORM;
        if (pf.fourCC == MakeFourCC('D', 'X', 'T', '5')) return DXGI_FORMAT_BC3_UNORM;
        if (pf.fourCC == MakeFourCC('B', 'C', '4', 'U')) return DXGI_FORMAT_BC4_UNORM;
        if (pf.fourCC == MakeFourCC('A', 'T', 'I', '1')) return DXGI_FORMAT_BC4_UNORM;
        if (pf.fourCC == MakeFourCC('B', 'C', '5', 'U')) return DXGI_FORMAT_BC5_UNORM;
        if (pf.fourCC == MakeFourCC('A', 'T', 'I', '2')) return DXGI_FORMAT_BC5_UNORM;
        if (pf.fourCC == 36) return DXGI_FORMAT_R16G16B16A16_UNORM;
        if (pf.fourCC == 111) return DXGI_FORMAT_R16_FLOAT;
        if (pf.fourCC == 112) return DXGI_FORMAT_R16G16_FLOAT;
        if (pf.fourCC == 113) return DXGI_FORMAT_R16G16B16A16_FLOAT;
        if (pf.fourCC == 114) return DXGI_FORMAT_R32_FLOAT;
        if (pf.fourCC == 115) return DXGI_FORMAT_R32G32_FLOAT;
        if (pf.fourCC == 116) return DXGI_FORMAT_R32G32B32A32_FLOAT;
      }
      return DXGI_FORMAT_UNKNOWN;
    }

    bool IsCompressed(DXGI_FORMAT fmt)
    {
      switch (fmt)
      {
      case DXGI_FORMAT_BC1_UNORM:
      case DXGI_FORMAT_BC2_UNORM:
      case DXGI_FORMAT_BC3_UNORM:
      case DXGI_FORMAT_BC4_UNORM:
      case DXGI_FORMAT_BC5_UNORM:
        return true;
      default:
        return false;
      }
    }

    uint32_t BitsPerPixel(DXGI_FORMAT fmt)
    {
      switch (fmt)
      {
      case DXGI_FORMAT_R32G32B32A32_FLOAT: return 128;
      case DXGI_FORMAT_R16G16B16A16_UNORM:
      case DXGI_FORMAT_R16G16B16A16_FLOAT:
      case DXGI_FORMAT_R32G32_FLOAT: return 64;
      case DXGI_FORMAT_R8G8B8A8_UNORM:
      case DXGI_FORMAT_B8G8R8A8_UNORM:
      case DXGI_FORMAT_B8G8R8X8_UNORM:
      case DXGI_FORMAT_R10G10B10A2_UNORM:
      case DXGI_FORMAT_R16G16_UNORM:
      case DXGI_FORMAT_R16G16_FLOAT:
      case DXGI_FORMAT_R32_FLOAT: return 32;
      case DXGI_FORMAT_B5G6R5_UNORM:
      case DXGI_FORMAT_B5G5R5A1_UNORM:
      case DXGI_FORMAT_B4G4R4A4_UNORM:
      case DXGI_FORMAT_R8G8_UNORM:
      case DXGI_FORMAT_R16_UNORM:
      case DXGI_FORMAT_R16_FLOAT: return 16;
      case DXGI_FORMAT_R8_UNORM:
      case DXGI_FORMAT_A8_UNORM: return 8;
      default: return 0;
      }
    }

    // Bytes for one surface (mip) of (w,h) in `fmt`, plus its row pitch.
    void GetSurfaceInfo(uint32_t w, uint32_t h, DXGI_FORMAT fmt, size_t& numBytes, size_t& rowBytes)
    {
      if (IsCompressed(fmt))
      {
        const size_t bpb = (fmt == DXGI_FORMAT_BC1_UNORM || fmt == DXGI_FORMAT_BC4_UNORM) ? 8 : 16; // bytes per 4x4 block
        const size_t nbw = std::max<size_t>(1, (static_cast<size_t>(w) + 3) / 4);
        const size_t nbh = std::max<size_t>(1, (static_cast<size_t>(h) + 3) / 4);
        rowBytes = nbw * bpb;
        numBytes = rowBytes * nbh;
      }
      else
      {
        const size_t bpp = BitsPerPixel(fmt);
        rowBytes = (static_cast<size_t>(w) * bpp + 7) / 8;
        numBytes = rowBytes * h;
      }
    }
  } // namespace

  HRESULT CreateDDSTextureFromMemory(ID3D11Device* device, ID3D11DeviceContext* context, const uint8_t* ddsData, size_t ddsDataSize,
                                     ID3D11Texture2D** outTexture, ID3D11ShaderResourceView** outSRV, bool generateMips) noexcept
  {
    if (outTexture) *outTexture = nullptr;
    if (outSRV) *outSRV = nullptr;
    if (!device || !ddsData || ddsDataSize < sizeof(uint32_t) + sizeof(DDS_HEADER))
      return E_INVALIDARG;

    if (*reinterpret_cast<const uint32_t*>(ddsData) != kDdsMagic)
      return E_FAIL;

    const auto* header = reinterpret_cast<const DDS_HEADER*>(ddsData + sizeof(uint32_t));
    if (header->size != sizeof(DDS_HEADER) || header->ddspf.size != sizeof(DDS_PIXELFORMAT))
      return E_FAIL;

    // This loader handles 2D textures only.
    if (header->caps2 & (DDS_CUBEMAP | DDS_VOLUME))
      return E_FAIL;

    size_t offset = sizeof(uint32_t) + sizeof(DDS_HEADER);
    DXGI_FORMAT format = DXGI_FORMAT_UNKNOWN;

    const bool dx10 = (header->ddspf.flags & DDS_FOURCC) && header->ddspf.fourCC == MakeFourCC('D', 'X', '1', '0');
    if (dx10)
    {
      if (ddsDataSize < offset + sizeof(DDS_HEADER_DXT10))
        return E_FAIL;
      const auto* h10 = reinterpret_cast<const DDS_HEADER_DXT10*>(ddsData + offset);
      offset += sizeof(DDS_HEADER_DXT10);
      // resourceDimension 3 == TEXTURE2D; arrays/cube unsupported here.
      if (h10->resourceDimension != 3 || h10->arraySize != 1)
        return E_FAIL;
      format = static_cast<DXGI_FORMAT>(h10->dxgiFormat);
    }
    else
    {
      format = GetDXGIFormat(header->ddspf);
    }

    if (format == DXGI_FORMAT_UNKNOWN)
      return E_FAIL;

    const uint32_t width = header->width;
    const uint32_t height = header->height;
    const uint32_t fileMips = header->mipMapCount ? header->mipMapCount : 1;
    if (width == 0 || height == 0)
      return E_FAIL;

    const uint8_t* bitData = ddsData + offset;
    const uint8_t* bitEnd = ddsData + ddsDataSize;

    // GPU mip generation only for non-block-compressed formats with no file mip chain.
    const bool autogen = generateMips && context != nullptr && fileMips <= 1 && !IsCompressed(format);

    if (autogen)
    {
      size_t numBytes = 0, rowBytes = 0;
      GetSurfaceInfo(width, height, format, numBytes, rowBytes);
      if (bitData + numBytes > bitEnd)
        return E_FAIL;

      D3D11_TEXTURE2D_DESC desc{};
      desc.Width = width;
      desc.Height = height;
      desc.MipLevels = 0; // full chain
      desc.ArraySize = 1;
      desc.Format = format;
      desc.SampleDesc.Count = 1;
      desc.Usage = D3D11_USAGE_DEFAULT;
      desc.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET;
      desc.MiscFlags = D3D11_RESOURCE_MISC_GENERATE_MIPS;

      com_ptr<ID3D11Texture2D> tex;
      HRESULT hr = device->CreateTexture2D(&desc, nullptr, tex.put());
      if (FAILED(hr))
        return hr;

      context->UpdateSubresource(tex.get(), 0, nullptr, bitData, static_cast<UINT>(rowBytes), static_cast<UINT>(numBytes));

      com_ptr<ID3D11ShaderResourceView> srv;
      hr = device->CreateShaderResourceView(tex.get(), nullptr, srv.put());
      if (FAILED(hr))
        return hr;

      context->GenerateMips(srv.get());

      if (outTexture) *outTexture = tex.detach();
      if (outSRV) *outSRV = srv.detach();
      return S_OK;
    }

    // Immutable texture using the file's own mip chain (or just mip 0).
    const uint32_t mipCount = std::max<uint32_t>(1, fileMips);
    std::vector<D3D11_SUBRESOURCE_DATA> initData(mipCount);

    const uint8_t* src = bitData;
    uint32_t w = width, h = height;
    uint32_t level = 0;
    for (; level < mipCount; ++level)
    {
      size_t numBytes = 0, rowBytes = 0;
      GetSurfaceInfo(w, h, format, numBytes, rowBytes);
      if (src + numBytes > bitEnd)
        break; // truncated mip chain: stop with what we have

      initData[level].pSysMem = src;
      initData[level].SysMemPitch = static_cast<UINT>(rowBytes);
      initData[level].SysMemSlicePitch = static_cast<UINT>(numBytes);

      src += numBytes;
      w = std::max<uint32_t>(1, w >> 1);
      h = std::max<uint32_t>(1, h >> 1);
    }
    if (level == 0)
      return E_FAIL;

    D3D11_TEXTURE2D_DESC desc{};
    desc.Width = width;
    desc.Height = height;
    desc.MipLevels = level;
    desc.ArraySize = 1;
    desc.Format = format;
    desc.SampleDesc.Count = 1;
    desc.Usage = D3D11_USAGE_IMMUTABLE;
    desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;

    com_ptr<ID3D11Texture2D> tex;
    HRESULT hr = device->CreateTexture2D(&desc, initData.data(), tex.put());
    if (FAILED(hr))
      return hr;

    com_ptr<ID3D11ShaderResourceView> srv;
    hr = device->CreateShaderResourceView(tex.get(), nullptr, srv.put());
    if (FAILED(hr))
      return hr;

    if (outTexture) *outTexture = tex.detach();
    if (outSRV) *outSRV = srv.detach();
    return S_OK;
  }

  HRESULT CreateDDSCubemapFromMemory(ID3D11Device* device, const uint8_t* ddsData, size_t ddsDataSize,
                                     ID3D11Texture2D** outTexture, ID3D11ShaderResourceView** outSRV) noexcept
  {
    if (outTexture) *outTexture = nullptr;
    if (outSRV) *outSRV = nullptr;
    if (!device || !ddsData || ddsDataSize < sizeof(uint32_t) + sizeof(DDS_HEADER))
      return E_INVALIDARG;

    if (*reinterpret_cast<const uint32_t*>(ddsData) != kDdsMagic)
      return E_FAIL;

    const auto* header = reinterpret_cast<const DDS_HEADER*>(ddsData + sizeof(uint32_t));
    if (header->size != sizeof(DDS_HEADER) || header->ddspf.size != sizeof(DDS_PIXELFORMAT))
      return E_FAIL;

    // Require a cubemap with all six faces present (legacy caps2 bits).
    constexpr uint32_t DDS_CUBEMAP_ALLFACES = 0x200 | 0x400 | 0x800 | 0x1000 | 0x2000 | 0x4000 | 0x8000; // 0xFE00
    if ((header->caps2 & DDS_CUBEMAP) == 0 || (header->caps2 & DDS_CUBEMAP_ALLFACES) != DDS_CUBEMAP_ALLFACES)
      return E_FAIL;

    size_t offset = sizeof(uint32_t) + sizeof(DDS_HEADER);
    DXGI_FORMAT format = DXGI_FORMAT_UNKNOWN;

    const bool dx10 = (header->ddspf.flags & DDS_FOURCC) && header->ddspf.fourCC == MakeFourCC('D', 'X', '1', '0');
    if (dx10)
    {
      if (ddsDataSize < offset + sizeof(DDS_HEADER_DXT10))
        return E_FAIL;
      const auto* h10 = reinterpret_cast<const DDS_HEADER_DXT10*>(ddsData + offset);
      offset += sizeof(DDS_HEADER_DXT10);
      // resourceDimension 3 == TEXTURE2D; a DX10 cube is arraySize 6 with the TEXTURECUBE misc flag.
      if (h10->resourceDimension != 3 || h10->arraySize != 6)
        return E_FAIL;
      format = static_cast<DXGI_FORMAT>(h10->dxgiFormat);
    }
    else
    {
      format = GetDXGIFormat(header->ddspf);
    }

    if (format == DXGI_FORMAT_UNKNOWN)
      return E_FAIL;

    const uint32_t width = header->width;
    const uint32_t height = header->height;
    const uint32_t mipCount = header->mipMapCount ? header->mipMapCount : 1;
    if (width == 0 || height == 0)
      return E_FAIL;

    const uint8_t* src = ddsData + offset;
    const uint8_t* bitEnd = ddsData + ddsDataSize;

    // One subresource per (face, mip): faces are stored consecutively, each with its full
    // mip chain (D3D11 subresource index == face * mipCount + mip).
    std::vector<D3D11_SUBRESOURCE_DATA> initData(static_cast<size_t>(6) * mipCount);
    for (uint32_t face = 0; face < 6; ++face)
    {
      uint32_t w = width, h = height;
      for (uint32_t mip = 0; mip < mipCount; ++mip)
      {
        size_t numBytes = 0, rowBytes = 0;
        GetSurfaceInfo(w, h, format, numBytes, rowBytes);
        if (src + numBytes > bitEnd)
          return E_FAIL; // truncated cube data

        D3D11_SUBRESOURCE_DATA& sd = initData[static_cast<size_t>(face) * mipCount + mip];
        sd.pSysMem = src;
        sd.SysMemPitch = static_cast<UINT>(rowBytes);
        sd.SysMemSlicePitch = static_cast<UINT>(numBytes);

        src += numBytes;
        w = std::max<uint32_t>(1, w >> 1);
        h = std::max<uint32_t>(1, h >> 1);
      }
    }

    D3D11_TEXTURE2D_DESC desc{};
    desc.Width = width;
    desc.Height = height;
    desc.MipLevels = mipCount;
    desc.ArraySize = 6;
    desc.Format = format;
    desc.SampleDesc.Count = 1;
    desc.Usage = D3D11_USAGE_IMMUTABLE;
    desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
    desc.MiscFlags = D3D11_RESOURCE_MISC_TEXTURECUBE;

    com_ptr<ID3D11Texture2D> tex;
    HRESULT hr = device->CreateTexture2D(&desc, initData.data(), tex.put());
    if (FAILED(hr))
      return hr;

    D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc{};
    srvDesc.Format = format;
    srvDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURECUBE;
    srvDesc.TextureCube.MostDetailedMip = 0;
    srvDesc.TextureCube.MipLevels = mipCount;

    com_ptr<ID3D11ShaderResourceView> srv;
    hr = device->CreateShaderResourceView(tex.get(), &srvDesc, srv.put());
    if (FAILED(hr))
      return hr;

    if (outTexture) *outTexture = tex.detach();
    if (outSRV) *outSRV = srv.detach();
    return S_OK;
  }
}
