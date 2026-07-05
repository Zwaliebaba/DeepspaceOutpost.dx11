#include "pch.h"
#include "TextureManager.h"
#include "DDSTextureLoader.h"
#include "FileSys.h"

#include <string>

// A texture that never loads shows up as "nothing rendered" downstream (e.g. the star
// sprite pass draws no pixels), which is hard to diagnose from the black screen alone.
// Log the two failure modes - file missing (bad path / working dir) vs decode failure -
// so the debugger's output window names the culprit. Unconditional (not _DEBUG-only) so a
// release build surfaces it too.
static void log_texture_failure(const std::string& _key, const char* _why, long _hr)
{
  char msg[512];
  if (_hr != 0)
    std::snprintf(msg, sizeof(msg), "[TextureManager] FAILED to load '%s': %s (hr=0x%08lX)\n", _key.c_str(), _why,
                  static_cast<unsigned long>(_hr));
  else
    std::snprintf(msg, sizeof(msg), "[TextureManager] FAILED to load '%s': %s\n", _key.c_str(), _why);
  OutputDebugStringA(msg);
}

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
      // Asset paths are ASCII; widen for the wide-path FileSys read.
      const byte_buffer_t bytes = BinaryFile::ReadFile(std::wstring(key.begin(), key.end()));
      if (!bytes.empty())
      {
        com_ptr<ID3D11Texture2D> tex;
        com_ptr<ID3D11ShaderResourceView> srv;
        const HRESULT hr = CreateDDSTextureFromMemory(device, Core::GetD3DDeviceContext(), bytes.data(), bytes.size(),
                                                      tex.put(), srv.put(), /*generateMips*/ true);
        if (SUCCEEDED(hr))
        {
          D3D11_TEXTURE2D_DESC desc{};
          tex->GetDesc(&desc);
          texture->m_texture = tex;
          texture->m_srv = srv;
          texture->m_width = static_cast<float>(desc.Width);
          texture->m_height = static_cast<float>(desc.Height);
        }
        else
        {
          log_texture_failure(key, "DDS decode/create failed", hr);
        }
      }
      else
      {
        log_texture_failure(key, "file not found or empty (check working dir / asset deploy)", 0);
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
      const std::string path = NormalizePath(_name);
      // Asset paths are ASCII; widen for the wide-path FileSys read.
      const byte_buffer_t bytes = BinaryFile::ReadFile(std::wstring(path.begin(), path.end()));
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
