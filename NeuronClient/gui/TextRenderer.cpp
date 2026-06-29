#include "pch.h"
#include "TextRenderer.h"
#include "ImmediateRenderer.h"

#include <cstdarg> // va_list / va_start / va_end

using Neuron::Graphics::Core;
using Neuron::Graphics::ImmediateRenderer;
using Neuron::Graphics::MatrixStackId;
using Neuron::Graphics::Primitive;
using Neuron::Graphics::TextureManager;

// Horizontal size as a proportion of vertical size.
constexpr float HORIZONTAL_SIZE = 0.6f;

constexpr float TEX_MARGIN = 0.003f;
constexpr float TEX_STRETCH = 1.0f - 26.0f * TEX_MARGIN;

constexpr float TEX_WIDTH = 1.0f / 16.0f * TEX_STRETCH * 0.9f;
constexpr float TEX_HEIGHT = 1.0f / 14.0f * TEX_STRETCH;

void TextRenderer::Startup(const std::string& _filename)
{
  m_filename = _filename;
  // Synchronous load (the donor loaded asynchronously via a coroutine + ASyncLoader;
  // that machinery is not imported). Until Neuron::Graphics::Core is initialised the
  // returned Texture is simply not yet loaded; re-Startup once the device is up.
  m_texture = TextureManager::LoadTexture(_filename);
}

void TextRenderer::Shutdown() { m_texture = nullptr; }

void TextRenderer::BeginText2D()
{
  const D3D11_VIEWPORT vp = Core::GetScreenViewport();

  // Screen-space orthographic projection (Y down), pushing the prior matrices so
  // EndText2D can restore them.
  ImmediateRenderer::SetMatrixMode(MatrixStackId::Projection);
  ImmediateRenderer::PushMatrix();
  ImmediateRenderer::LoadIdentity();
  ImmediateRenderer::Ortho2D(vp.TopLeftX - 0.325f, vp.TopLeftX + vp.Width - 0.325f, vp.TopLeftY + vp.Height - 0.325f,
                             vp.TopLeftY - 0.325f);

  ImmediateRenderer::SetMatrixMode(MatrixStackId::ModelView);
  ImmediateRenderer::PushMatrix();
  ImmediateRenderer::LoadIdentity();

  ImmediateRenderer::Color(1.0f, 1.0f, 1.0f, 1.0f);
  ImmediateRenderer::SetBlendEnabled(true);
  ImmediateRenderer::SetCullEnabled(false);
  ImmediateRenderer::SetDepthTestEnabled(false);
  ImmediateRenderer::SetDepthWriteEnabled(false);
  ImmediateRenderer::SetFogEnabled(false);
}

void TextRenderer::EndText2D()
{
  ImmediateRenderer::SetMatrixMode(MatrixStackId::Projection);
  ImmediateRenderer::PopMatrix();
  ImmediateRenderer::SetMatrixMode(MatrixStackId::ModelView);
  ImmediateRenderer::PopMatrix();

  ImmediateRenderer::SetDepthWriteEnabled(true);
  ImmediateRenderer::SetDepthTestEnabled(true);
  ImmediateRenderer::SetCullEnabled(true);
  ImmediateRenderer::SetBlendEnabled(false);
}

float TextRenderer::GetTexCoordX(unsigned char theChar)
{
  constexpr float CHAR_WIDTH = 1.0f / 16.0f;
  const float xPos = theChar % 16;
  const float texX = xPos * CHAR_WIDTH + TEX_MARGIN + 0.002f;
  return texX;
}

float TextRenderer::GetTexCoordY(unsigned char theChar)
{
  constexpr float CHAR_HEIGHT = 1.0f / 14.0f;
  const float yPos = (theChar >> 4) - 2;
  const float texY = yPos * CHAR_HEIGHT + TEX_MARGIN + 0.001f;
  return texY;
}

void TextRenderer::SetRenderShadow(bool _renderShadow) { m_renderShadow = _renderShadow; }

void TextRenderer::DrawText2DSimple(float _x, float _y, float _size, std::string_view _text)
{
  if (!m_texture || !m_texture->GetShaderResourceView())
    return;

  // Compatibility offset matching the original code.
  _y -= 7.0f;
  _x -= 3.0f;

  const float horiSize = _size * HORIZONTAL_SIZE;

  if (m_renderShadow)
    ImmediateRenderer::SetBlendFunc(D3D11_BLEND_SRC_ALPHA, D3D11_BLEND_INV_SRC_COLOR);
  else
    ImmediateRenderer::SetBlendFunc(D3D11_BLEND_SRC_ALPHA, D3D11_BLEND_ONE);
  ImmediateRenderer::SetBlendEnabled(true);

  ImmediateRenderer::BindTexture(0, m_texture->GetShaderResourceView());
  ImmediateRenderer::SetSampler(0, D3D11_FILTER_MIN_MAG_MIP_LINEAR, D3D11_TEXTURE_ADDRESS_CLAMP);

  // Dedicated text program: u_Color (white here) * per-vertex colour * glyph. The
  // per-vertex colour carries the tint set by the caller (or white from BeginText2D).
  ImmediateRenderer::UseProgram(Neuron::Graphics::ShaderProgram::TextOverlay);
  ImmediateRenderer::SetDrawColor(1.0f, 1.0f, 1.0f, 1.0f);

  // Batch the whole string into one draw.
  ImmediateRenderer::Begin(Primitive::Quads);
  const size_t numChars = _text.size();
  for (unsigned int i = 0; i < numChars; ++i)
  {
    const unsigned char thisChar = _text[i];

    if (thisChar > 32)
    {
      const float texX = GetTexCoordX(thisChar);
      const float texY = GetTexCoordY(thisChar);

      ImmediateRenderer::TexCoord(texX, texY + TEX_HEIGHT);
      ImmediateRenderer::Vertex(_x, _y + _size);

      ImmediateRenderer::TexCoord(texX + TEX_WIDTH, texY + TEX_HEIGHT);
      ImmediateRenderer::Vertex(_x + horiSize, _y + _size);

      ImmediateRenderer::TexCoord(texX + TEX_WIDTH, texY);
      ImmediateRenderer::Vertex(_x + horiSize, _y);

      ImmediateRenderer::TexCoord(texX, texY);
      ImmediateRenderer::Vertex(_x, _y);
    }

    _x += horiSize;
  }
  ImmediateRenderer::End();

  ImmediateRenderer::UseProgram(Neuron::Graphics::ShaderProgram::Generic);
  ImmediateRenderer::SetBlendFunc(D3D11_BLEND_SRC_ALPHA, D3D11_BLEND_INV_SRC_ALPHA);
  ImmediateRenderer::BindTexture(0, nullptr);
}

void TextRenderer::DrawText2D(float _x, float _y, float _size, std::string_view _text, ...)
{
  char buf[512];
  va_list ap;
  va_start(ap, _text);
  vsprintf(buf, _text.data(), ap);
  va_end(ap);
  DrawText2DSimple(_x, _y, _size, buf);
}

void TextRenderer::DrawText2DRight(float _x, float _y, float _size, std::string_view _text, ...)
{
  char buf[512];
  va_list ap;
  va_start(ap, _text);
  vsprintf(buf, _text.data(), ap);
  va_end(ap);

  const float width = GetTextWidth(strlen(buf), _size);
  DrawText2DSimple(_x - width, _y, _size, buf);
}

void TextRenderer::DrawText2DCenter(float _x, float _y, float _size, std::string_view _text, ...)
{
  char buf[512];
  va_list ap;
  va_start(ap, _text);
  vsprintf(buf, _text.data(), ap);
  va_end(ap);

  const float width = GetTextWidth(strlen(buf), _size);
  DrawText2DSimple(_x - width / 2, _y, _size, buf);
}

float TextRenderer::GetTextWidth(size_t _numChars, float _size) { return _numChars * _size * HORIZONTAL_SIZE; }
