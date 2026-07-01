#pragma once

// Native Direct3D 11 texture manager (Neuron::Graphics).
//
// Phase 2 of the GUI/text/GraphicsCore import. The donor's TextureManager/Texture
// and DDSTextureLoader were D3D9 (com_ptr<IDirect3DTexture9>) and pulled in the
// hard-coded June-2010 DirectX SDK, so they are NOT imported. This is a fresh,
// native D3D11-only replacement that satisfies the same call sites the text
// renderer and GUI use: LoadTexture(name) -> Texture::GetShaderResourceView().
//
// Textures load through this layer's own native DDS loader (graphics/DDSTextureLoader),
// so TextureManager has no dependency on the legacy platform/Image loader. A full mip
// chain is generated for non-block-compressed formats.

#include "GraphicsCore.h"

#include <memory>
#include <string>
#include <unordered_map>

namespace Neuron::Graphics
{
  class Texture
  {
    friend class TextureManager;

    public:
      ID3D11ShaderResourceView* GetShaderResourceView() const noexcept { return m_srv.get(); }
      float GetWidth() const noexcept { return m_width; }
      float GetHeight() const noexcept { return m_height; }
      bool IsLoaded() const noexcept { return m_srv != nullptr; }

    private:
      com_ptr<ID3D11ShaderResourceView> m_srv;
      com_ptr<ID3D11Texture2D> m_texture;
      float m_width = 0.0f;
      float m_height = 0.0f;
  };

  class TextureManager
  {
    public:
      static void Shutdown();

      // Load (or return the cached) texture by asset-relative name, using either
      // '/' or '\\' separators, e.g. "Textures/InterfaceRed.dds". Always returns a
      // non-null Texture; if the device is not yet up or the file is missing, the
      // returned Texture is simply not loaded (GetShaderResourceView() == nullptr).
      static std::shared_ptr<Texture> LoadTexture(const std::string& _name);

      // Load (or return the cached) cubemap by asset-relative name. The returned Texture's
      // GetShaderResourceView() is a TextureCube SRV (sample it with a direction, not UVs).
      // Same not-loaded fallback as LoadTexture when the device is down or the file is
      // missing / not a six-face cube.
      static std::shared_ptr<Texture> LoadCubemap(const std::string& _name);

    private:
      inline static std::unordered_map<std::string, std::shared_ptr<Texture>> sm_textures;
  };
}
