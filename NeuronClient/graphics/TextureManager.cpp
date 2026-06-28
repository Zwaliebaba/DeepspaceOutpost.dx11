#include "pch.h"
#include "TextureManager.h"

#include "Image.h" // platform/Image.h : load_image_rgba (BMP/PCX/uncompressed DDS -> RGBA)

namespace Neuron::Graphics
{
  namespace
  {
    std::string NarrowPath(const std::wstring& _w)
    {
      if (_w.empty())
        return {};
      const int n = ::WideCharToMultiByte(CP_UTF8, 0, _w.data(), static_cast<int>(_w.size()), nullptr, 0, nullptr, nullptr);
      std::string s(static_cast<size_t>(n), '\0');
      ::WideCharToMultiByte(CP_UTF8, 0, _w.data(), static_cast<int>(_w.size()), s.data(), n, nullptr, nullptr);
      for (char& c : s)
        if (c == '\\')
          c = '/';
      return s;
    }
  }

  std::shared_ptr<Texture> TextureManager::LoadTexture(const std::wstring& _name)
  {
    if (const auto it = sm_textures.find(_name); it != sm_textures.end())
      return it->second;

    auto texture = std::make_shared<Texture>();

    ID3D11Device* device = Core::GetD3DDevice();
    if (device)
    {
      const std::string path = NarrowPath(_name);
      const Image img = load_image_rgba(path.c_str(), /*key_index0*/ false);
      if (img.ok())
      {
        D3D11_TEXTURE2D_DESC td{};
        td.Width = static_cast<UINT>(img.width);
        td.Height = static_cast<UINT>(img.height);
        td.MipLevels = 1;
        td.ArraySize = 1;
        td.Format = DXGI_FORMAT_R8G8B8A8_UNORM; // Image.h decodes to 0xAABBGGRR (R8G8B8A8_UNORM order)
        td.SampleDesc.Count = 1;
        td.Usage = D3D11_USAGE_IMMUTABLE;
        td.BindFlags = D3D11_BIND_SHADER_RESOURCE;

        D3D11_SUBRESOURCE_DATA sd{};
        sd.pSysMem = img.rgba.data();
        sd.SysMemPitch = static_cast<UINT>(img.width) * 4u;

        com_ptr<ID3D11Texture2D> tex;
        if (SUCCEEDED(device->CreateTexture2D(&td, &sd, tex.put())))
        {
          D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc{};
          srvDesc.Format = td.Format;
          srvDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
          srvDesc.Texture2D.MipLevels = 1;

          if (SUCCEEDED(device->CreateShaderResourceView(tex.get(), &srvDesc, texture->m_srv.put())))
          {
            texture->m_texture = tex;
            texture->m_width = static_cast<float>(img.width);
            texture->m_height = static_cast<float>(img.height);
          }
        }
      }
    }

    sm_textures.emplace(_name, texture);
    return texture;
  }

  void TextureManager::Shutdown() { sm_textures.clear(); }
}
