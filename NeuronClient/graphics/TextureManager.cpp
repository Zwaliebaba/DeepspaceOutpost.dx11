#include "pch.h"
#include "TextureManager.h"
#include "DDSTextureLoader.h"

#include <fstream>
#include <iterator>
#include <vector>

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

    std::vector<uint8_t> ReadFileBytes(const std::string& _path)
    {
      std::ifstream file(_path, std::ios::binary); // CWD-relative, like the rest of the game's assets
      if (!file)
        return {};
      return std::vector<uint8_t>((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
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
      const std::vector<uint8_t> bytes = ReadFileBytes(key);
      if (!bytes.empty())
      {
        com_ptr<ID3D11Texture2D> tex;
        com_ptr<ID3D11ShaderResourceView> srv;
        if (SUCCEEDED(CreateDDSTextureFromMemory(device, Core::GetD3DDeviceContext(), bytes.data(), bytes.size(), tex.put(), srv.put(),
                                                 /*generateMips*/ true)))
        {
          D3D11_TEXTURE2D_DESC desc{};
          tex->GetDesc(&desc);
          texture->m_texture = tex;
          texture->m_srv = srv;
          texture->m_width = static_cast<float>(desc.Width);
          texture->m_height = static_cast<float>(desc.Height);
        }
      }
    }

    sm_textures.emplace(key, texture);
    return texture;
  }

  std::shared_ptr<Texture> TextureManager::LoadCubemap(const std::string& _name)
  {
    // Namespace cube entries so a cube and a 2D texture of the same name never collide.
    const std::string key = "cube:" + NormalizePath(_name);

    if (const auto it = sm_textures.find(key); it != sm_textures.end())
      return it->second;

    auto texture = std::make_shared<Texture>();

    ID3D11Device* device = Core::GetD3DDevice();
    if (device)
    {
      const std::vector<uint8_t> bytes = ReadFileBytes(NormalizePath(_name));
      if (!bytes.empty())
      {
        com_ptr<ID3D11Texture2D> tex;
        com_ptr<ID3D11ShaderResourceView> srv;
        if (SUCCEEDED(CreateDDSCubemapFromMemory(device, bytes.data(), bytes.size(), tex.put(), srv.put())))
        {
          D3D11_TEXTURE2D_DESC desc{};
          tex->GetDesc(&desc);
          texture->m_texture = tex;
          texture->m_srv = srv;
          texture->m_width = static_cast<float>(desc.Width);
          texture->m_height = static_cast<float>(desc.Height);
        }
      }
    }

    sm_textures.emplace(key, texture);
    return texture;
  }

  void TextureManager::Shutdown() { sm_textures.clear(); }
}
