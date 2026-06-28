#include "pch.h"
#include "Resource.h"
#include "TextureManager.h"

ID3D11ShaderResourceView* Resource::GetTexture(const char* _name)
{
  if (!_name)
    return nullptr;

  const int n = ::MultiByteToWideChar(CP_UTF8, 0, _name, -1, nullptr, 0);
  if (n <= 0)
    return nullptr;
  std::wstring wide(static_cast<size_t>(n - 1), L'\0');
  ::MultiByteToWideChar(CP_UTF8, 0, _name, -1, wide.data(), n);

  const std::shared_ptr<Neuron::Graphics::Texture> texture = Neuron::Graphics::TextureManager::LoadTexture(wide);
  return texture ? texture->GetShaderResourceView() : nullptr;
}
