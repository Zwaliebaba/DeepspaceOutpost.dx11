#ifndef TEXT_RENDERER_H
#define TEXT_RENDERER_H

// 2D bitmap-font text renderer (Phase 3 of the GUI/text import).
//
// Imported from the donor and trimmed to the screen-space ("overlay") path the
// menus use. The donor's world-space DrawText3D* overloads still rode the legacy
// gl* path and depended on the camera/app; they are intentionally omitted here.
// Asset loading is synchronous (the donor's coroutine/ASyncLoader machinery is not
// imported). Text renders through Neuron::Graphics::ImmediateRenderer's TextOverlay
// program against a bitmap-grid font sheet (a GameData/Fonts/*.dds).

#include "TextureManager.h"

#include <memory>
#include <string>
#include <string_view>

#define DEF_FONT_SIZE 12.0f

class DX9TextRenderer
{
  public:
    void Startup(const std::wstring& _filename);
    void Shutdown();

    void BeginText2D();
    void EndText2D();

    void SetRenderShadow(bool _renderShadow);

    void DrawText2DSimple(float _x, float _y, float _size, std::string_view _text);
    void DrawText2D(float _x, float _y, float _size, std::string_view _text, ...);
    void DrawText2DRight(float _x, float _y, float _size, std::string_view _text, ...);
    void DrawText2DCenter(float _x, float _y, float _size, std::string_view _text, ...);

    static float GetTextWidth(size_t _numChars, float _size = 13.0f);

  protected:
    std::wstring m_filename;
    std::shared_ptr<Neuron::Graphics::Texture> m_texture;
    bool m_renderShadow = {};

    static float GetTexCoordX(unsigned char theChar);
    static float GetTexCoordY(unsigned char theChar);
};

// The two fonts the GUI draws through (window titles / HUD vs. button captions).
inline DX9TextRenderer g_gameFont;
inline DX9TextRenderer g_editorFont;

#endif
