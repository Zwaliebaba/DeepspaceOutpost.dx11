#include "pch.h"
#include "TextRenderer.h"
#include "Render2D.h"

using Neuron::Graphics::Render2D;
using Neuron::Graphics::Texture;
using Neuron::Graphics::TextureManager;

// Horizontal size as a proportion of vertical size.
constexpr float HORIZONTAL_SIZE = 0.6f;

// One glyph cell on the 16-col x 14-row grid (16x16 px cells in the 256x224 sheet).
// Cells are sampled whole, on exact texel boundaries, so point sampling reconstructs
// the native glyph pixels cleanly at any size (the former 0.9 crop + sub-texel margins
// pushed sampling off the texel grid and softened small text).
constexpr float TEX_WIDTH = 1.0f / 16.0f;
constexpr float TEX_HEIGHT = 1.0f / 14.0f;

void TextRenderer::Startup(const std::string& _filename)
{
  m_filename = _filename;
  // Synchronous load (the donor loaded asynchronously via a coroutine + ASyncLoader;
  // that machinery is not imported). Until Neuron::Graphics::Core is initialised the
  // returned Texture is simply not yet loaded; re-Startup once the device is up.
  m_texture = TextureManager::LoadTexture(_filename);
}

void TextRenderer::Shutdown() { m_texture = nullptr; }

float TextRenderer::GetTexCoordX(unsigned char theChar)
{
  // Left edge of the glyph's column cell, aligned to the texel grid.
  const float xPos = theChar % 16;
  return xPos * TEX_WIDTH;
}

float TextRenderer::GetTexCoordY(unsigned char theChar)
{
  // Top edge of the glyph's row cell, aligned to the texel grid.
  const float yPos = (theChar >> 4) - 2;
  return yPos * TEX_HEIGHT;
}

void TextRenderer::SetRenderShadow(bool _renderShadow) { m_renderShadow = _renderShadow; }

void TextRenderer::SetColor(uint8_t r, uint8_t g, uint8_t b, uint8_t a) { m_color = Render2D::Rgba(r, g, b, a); }

// Submit one run of glyph quads from _sheet at (_x,_y) in a single tint, into the open
// Render2D batch. Shared by the shadow and text passes of DrawText2DSimple.
void TextRenderer::EmitGlyphs(Texture* _sheet, float _x, float _y, float _size, std::string_view _text, uint32_t _tint)
{
  ID3D11ShaderResourceView* srv = _sheet->GetShaderResourceView();
  const float horiSize = _size * HORIZONTAL_SIZE;

  for (const char ch : _text)
  {
    const unsigned char thisChar = static_cast<unsigned char>(ch);
    if (thisChar > 32)
    {
      const float texX = GetTexCoordX(thisChar);
      const float texY = GetTexCoordY(thisChar);
      Render2D::TexQuad(srv, _x, _y, _x + horiSize, _y + _size, texX, texY, texX + TEX_WIDTH, texY + TEX_HEIGHT, _tint);
    }
    _x += horiSize;
  }
}

void TextRenderer::DrawText2DSimple(float _x, float _y, float _size, std::string_view _text)
{
  if (!m_texture || !m_texture->GetShaderResourceView())
    return;

  // Compatibility offset matching the original code.
  _y -= 7.0f;
  _x -= 3.0f;

  // When an outline is requested, draw the glyphs through Render2D's built-in
  // text-outline program (a crisp black outline produced in the shader) instead of a
  // second offset geometry pass; otherwise the plain default program. The text colour
  // rides the per-vertex tint either way.
  const float w = static_cast<float>(m_texture->GetWidth());
  const float h = static_cast<float>(m_texture->GetHeight());
  const bool outline = m_renderShadow && w > 0.0f && h > 0.0f;
  if (outline)
  {
    Render2D::SetProgram(Render2D::TextOutlineProgram());
    Render2D::SetTextOutline(0xFF000000u, 1.0f / w, 1.0f / h, 1.0f);
  }

  EmitGlyphs(m_texture.get(), _x, _y, _size, _text, m_color);

  if (outline)
    Render2D::SetProgram(Render2D::DefaultProgram);
}

void TextRenderer::DrawText2D(float _x, float _y, float _size, std::string_view _text)
{
  DrawText2DSimple(_x, _y, _size, _text);
}

void TextRenderer::DrawText2DRight(float _x, float _y, float _size, std::string_view _text)
{
  const float width = GetTextWidth(_text.size(), _size);
  DrawText2DSimple(_x - width, _y, _size, _text);
}

void TextRenderer::DrawText2DCenter(float _x, float _y, float _size, std::string_view _text)
{
  const float width = GetTextWidth(_text.size(), _size);
  DrawText2DSimple(_x - width / 2, _y, _size, _text);
}

float TextRenderer::GetTextWidth(size_t _numChars, float _size) { return _numChars * _size * HORIZONTAL_SIZE; }
