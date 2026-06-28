#pragma once

// Texture-resource shim for the imported GUI (Phase 4).
//
// The donor's Resource pulled in Bitmap/Shape/Sound/TextStreamReaders; the imported
// widgets only call Resource::GetTexture(name) to obtain a bindable texture. This
// shim resolves the name through Neuron::Graphics::TextureManager and returns the
// native shader-resource view to bind via ImmediateRenderer::BindTexture.

struct ID3D11ShaderResourceView;

class Resource
{
  public:
    static ID3D11ShaderResourceView* GetTexture(const char* _name);
};
