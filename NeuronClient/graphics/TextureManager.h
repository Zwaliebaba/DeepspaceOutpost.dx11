#pragma once

// Native Direct3D 11 texture manager (Neuron::Graphics).
//
// Phase 2 of the GUI/text/GraphicsCore import. The donor's TextureManager/Texture
// and DDSTextureLoader were D3D9 (com_ptr<IDirect3DTexture9>) and pulled in the
// hard-coded June-2010 DirectX SDK, so they are NOT imported. This is a fresh,
// native D3D11-only replacement that satisfies the same call sites the text
// renderer and GUI use: LoadTexture(name) -> Texture::GetShaderResourceView().
//
// All of the game's .dds assets are uncompressed 32-bpp (no DXTn/BCn), so loading
// goes through the existing platform image loader (platform/Image.h::load_image_rgba)
// rather than a dedicated block-compressed DDS path.

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
      // '/' or '\\' separators, e.g. L"Textures/InterfaceRed.dds". Always returns a
      // non-null Texture; if the device is not yet up or the file is missing, the
      // returned Texture is simply not loaded (GetShaderResourceView() == nullptr).
      static std::shared_ptr<Texture> LoadTexture(const std::wstring& _name);

    private:
      inline static std::unordered_map<std::wstring, std::shared_ptr<Texture>> sm_textures;
  };
}
