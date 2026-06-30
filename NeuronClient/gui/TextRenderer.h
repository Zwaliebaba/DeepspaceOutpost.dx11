#ifndef TEXT_RENDERER_H
#define TEXT_RENDERER_H

// 2D bitmap-font text renderer (Phase 3 of the GUI/text import).
//
// Imported from the donor (where it was named DX9TextRenderer) and trimmed to the
// screen-space ("overlay") path the
// menus use. The donor's world-space DrawText3D* overloads still rode the legacy
// gl* path and depended on the camera/app; they are intentionally omitted here.
// Asset loading is synchronous (the donor's coroutine/ASyncLoader machinery is not
// imported). Glyph quads are submitted to the native Neuron::Graphics::Render2D batch,
// so text composites in submission order with the rest of the 2D drawn inside the
// surrounding Render2D::Begin/End pass. Sourced from a bitmap-grid font sheet
// (a GameData/Fonts/*.dds).

#include "TextureManager.h"

#include <cstdint>
#include <memory>
#include <string>
#include <string_view>

#define DEF_FONT_SIZE 12.0f

class TextRenderer
{
  public:
    void Startup(const std::string& _filename);
    void Shutdown();

    void SetRenderShadow(bool _renderShadow);

    // Tint for subsequent DrawText* calls (default opaque white). Channels are 0-255.
    void SetColor(uint8_t r, uint8_t g, uint8_t b, uint8_t a = 255);

    // Draw _text at (_x,_y) left-aligned / right-aligned / centred on _x. These take
    // the string verbatim (no printf formatting - format with std::format/snprintf at
    // the call site if needed), so a '%' in a caption is just a glyph.
    void DrawText2DSimple(float _x, float _y, float _size, std::string_view _text);
    void DrawText2D(float _x, float _y, float _size, std::string_view _text);
    void DrawText2DRight(float _x, float _y, float _size, std::string_view _text);
    void DrawText2DCenter(float _x, float _y, float _size, std::string_view _text);

    static float GetTextWidth(size_t _numChars, float _size = 13.0f);

  protected:
    std::string m_filename;
    std::shared_ptr<Neuron::Graphics::Texture> m_texture;
    bool m_renderShadow = {};
    uint32_t m_color = 0xFFFFFFFFu; // current tint (0xAABBGGRR), default opaque white

    static float GetTexCoordX(unsigned char theChar);
    static float GetTexCoordY(unsigned char theChar);

    // Submit one run of glyph quads from the bound sheet at (_x,_y) in a single tint.
    // Shared by the shadow and text passes.
    void EmitGlyphs(Neuron::Graphics::Texture* _sheet, float _x, float _y, float _size, std::string_view _text,
                    uint32_t _tint);
};

// The two fonts the GUI draws through (window titles / HUD vs. button captions).
inline TextRenderer g_gameFont;
inline TextRenderer g_editorFont;

#endif
