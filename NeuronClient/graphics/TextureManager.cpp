#include "pch.h"
#include "TextureManager.h"

#include "Image.h" // platform/Image.h : load_image_rgba (BMP/PCX/uncompressed DDS -> RGBA)

namespace Neuron::Graphics
{
  namespace
  {
    // Normalize '\\' to '/' so "Textures\\X.dds" and "Textures/X.dds" share one cache
    // entry (and one disk load).
    std::string NormalizePath(std::string _path)
    {
      for (char& c : _path)
        if (c == '\\')
          c = '/';
      return _path;
    }
  }

  std::shared_ptr<Texture> TextureManager::LoadTexture(const std::string& _name)
  {
    const std::string key = NormalizePath(_name);

    if (const auto it = sm_textures.find(key); it != sm_textures.end())
      return it->second;

    auto texture = std::make_shared<Texture>();

    ID3D11Device* device = Core::GetD3DDevice();
    if (device)
    {
      const Image img = load_image_rgba(key.c_str(), /*key_index0*/ false);
      if (img.ok())
      {
        // Full mip chain so minified draws (e.g. the tall 64x510 interface strip shrunk
        // into a small window) filter cleanly instead of aliasing the texture's fine
        // detail (its 1px scanlines). MipLevels=0 + GENERATE_MIPS; upload mip 0 then let
        // the GPU build the rest. (Requires a non-immutable, render-target-bindable
        // texture, hence USAGE_DEFAULT + BIND_RENDER_TARGET.)
        D3D11_TEXTURE2D_DESC td{};
        td.Width = static_cast<UINT>(img.width);
        td.Height = static_cast<UINT>(img.height);
        td.MipLevels = 0;
        td.ArraySize = 1;
        td.Format = DXGI_FORMAT_R8G8B8A8_UNORM; // Image.h decodes to 0xAABBGGRR (R8G8B8A8_UNORM order)
        td.SampleDesc.Count = 1;
        td.Usage = D3D11_USAGE_DEFAULT;
        td.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET;
        td.MiscFlags = D3D11_RESOURCE_MISC_GENERATE_MIPS;

        com_ptr<ID3D11Texture2D> tex;
        if (SUCCEEDED(device->CreateTexture2D(&td, nullptr, tex.put())))
        {
          ID3D11DeviceContext* ctx = Core::GetD3DDeviceContext();
          ctx->UpdateSubresource(tex.get(), 0, nullptr, img.rgba.data(), static_cast<UINT>(img.width) * 4u, 0);

          // Null SRV desc => view over the whole generated mip chain.
          if (SUCCEEDED(device->CreateShaderResourceView(tex.get(), nullptr, texture->m_srv.put())))
          {
            ctx->GenerateMips(texture->m_srv.get());
            texture->m_texture = tex;
            texture->m_width = static_cast<float>(img.width);
            texture->m_height = static_cast<float>(img.height);
          }
        }
      }
    }

    sm_textures.emplace(key, texture);
    return texture;
  }

  void TextureManager::Shutdown() { sm_textures.clear(); }
}
