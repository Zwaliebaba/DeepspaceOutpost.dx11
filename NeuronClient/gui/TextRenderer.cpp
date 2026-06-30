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

  ImmediateRenderer::SetBlendEnabled(true);
  ImmediateRenderer::BindTexture(0, m_texture->GetShaderResourceView());
  ImmediateRenderer::SetSampler(0, D3D11_FILTER_MIN_MAG_MIP_LINEAR, D3D11_TEXTURE_ADDRESS_CLAMP);

  // Dedicated text program: u_Color * per-vertex colour * glyph. The per-vertex colour
  // carries the tint set by the caller (or white from BeginText2D); u_Color is set per
  // pass below (white for the glyph, black for its drop shadow).
  ImmediateRenderer::UseProgram(Neuron::Graphics::ShaderProgram::TextOverlay);

  // Batch the whole string into one quad draw, offset by (ox, oy) pixels.
  const size_t numChars = _text.size();
  auto emit = [&](float ox, float oy) {
    float x = _x + ox;
    const float y = _y + oy;
    ImmediateRenderer::Begin(Primitive::Quads);
    for (unsigned int i = 0; i < numChars; ++i)
    {
      const unsigned char thisChar = _text[i];

      if (thisChar > 32)
      {
        const float texX = GetTexCoordX(thisChar);
        const float texY = GetTexCoordY(thisChar);

        ImmediateRenderer::TexCoord(texX, texY + TEX_HEIGHT);
        ImmediateRenderer::Vertex(x, y + _size);

        ImmediateRenderer::TexCoord(texX + TEX_WIDTH, texY + TEX_HEIGHT);
        ImmediateRenderer::Vertex(x + horiSize, y + _size);

        ImmediateRenderer::TexCoord(texX + TEX_WIDTH, texY);
        ImmediateRenderer::Vertex(x + horiSize, y);

        ImmediateRenderer::TexCoord(texX, texY);
        ImmediateRenderer::Vertex(x, y);
      }

      x += horiSize;
    }
    ImmediateRenderer::End();
  };

  if (m_renderShadow)
  {
    // Legacy soft-"glow" path used for window titles and highlighted captions, which
    // sit on the light title/selection bars. The caller draws the string twice and
    // relies on this blend to build the glow, so keep it as-is.
    ImmediateRenderer::SetBlendFunc(D3D11_BLEND_SRC_ALPHA, D3D11_BLEND_INV_SRC_COLOR);
    ImmediateRenderer::SetDrawColor(1.0f, 1.0f, 1.0f, 1.0f);
    emit(0.0f, 0.0f);
  }
  else
  {
    // Body text (labels, market rows, normal button captions). Use straight alpha
    // blending so glyphs sit opaquely on the panel instead of washing additively into
    // the bright centre of the red gradient (the old SRC_ALPHA/ONE blend made the text
    // low-contrast and hard to read). A 1px near-black drop shadow gives every glyph a
    // crisp edge against any background colour.
    ImmediateRenderer::SetBlendFunc(D3D11_BLEND_SRC_ALPHA, D3D11_BLEND_INV_SRC_ALPHA);

    // Shadow pass: force the colour to black (u_Color = 0) while keeping the glyph's
    // own alpha, so it reads as a shadow regardless of the caller's tint.
    ImmediateRenderer::SetDrawColor(0.0f, 0.0f, 0.0f, 0.9f);
    emit(1.0f, 1.0f);

    // Main pass: full tint (u_Color = white) * caller's per-vertex colour * glyph.
    ImmediateRenderer::SetDrawColor(1.0f, 1.0f, 1.0f, 1.0f);
    emit(0.0f, 0.0f);
  }

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
