#include "pch.h"
#include "TextRenderer.h"
#include "Render2D.h"

#include <cstdarg> // va_list / va_start / va_end
#include <cstring> // strlen

using Neuron::Graphics::Render2D;
using Neuron::Graphics::Texture;
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

  // A 1px-offset black drop-shadow first (when requested), then the glyphs in the
  // current tint, so text stays readable over the GUI panels. Both passes submit into
  // the surrounding Render2D::Begin/End scope (alpha-blended, no depth), which the
  // overlay opens once per frame.
  if (m_renderShadow)
    EmitGlyphs(m_texture.get(), _x + 1.0f, _y + 1.0f, _size, _text, 0xFF000000u);

  EmitGlyphs(m_texture.get(), _x, _y, _size, _text, m_color);
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
