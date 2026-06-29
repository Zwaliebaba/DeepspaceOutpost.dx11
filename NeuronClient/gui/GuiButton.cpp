#include "pch.h"
#include "GuiButton.h"
#include "Canvas.h"
#include "TextRenderer.h"
#include "GuiWindow.h"
#include "Render2D.h"
#include "platform_win.h"

using Neuron::Graphics::Render2D;

namespace
{
  // Is `btn` the window's currently-selected (keyboard-navigated) button? Guards the
  // m_buttonOrder[m_currentButton] access so a window whose Create() did not populate
  // m_buttonOrder (or an out-of-range index) cannot read out of bounds.
  bool IsCurrentButton(const GuiWindow* parent, const GuiButton* btn)
  {
    return parent && !parent->m_buttonOrder.empty() && parent->m_currentButton >= 0 &&
           parent->m_currentButton < static_cast<int>(parent->m_buttonOrder.size()) &&
           parent->m_buttonOrder[parent->m_currentButton] == btn;
  }
}

GuiButton::GuiButton()
  : m_fontSize(11.0f),
    m_centered(false),
    m_disabled(false),
    m_highlightedThisFrame(false),
    m_mouseHighlightMode(false)
{
  m_name = "New Button";
  GuiButton::SetTooltip(" ");
}

void GuiButton::SetDisabled(bool _disabled) { m_disabled = _disabled; }

void GuiButton::Render(int realX, int realY, bool highlighted, bool clicked)
{
  float y = 7.5f + realY + (m_bounds.Height - m_fontSize) / 2;

  auto parent = m_parent;

  UpdateButtonHighlight();

  if (!m_mouseHighlightMode)
    highlighted = false;

  if (IsCurrentButton(parent, this))
    highlighted = true;

  if (highlighted || clicked)
  {
    // Vertical gradient fill (lighter on top), brighter when clicked.
    const uint32_t top = clicked ? Render2D::Rgba(255, 255, 255, 255) : Render2D::Rgba(199, 214, 220, 255);
    const uint32_t bottom = clicked ? Render2D::Rgba(162, 191, 208, 255) : Render2D::Rgba(112, 141, 168, 255);
    Render2D::TexQuadColored(nullptr, realX, realY, realX + m_bounds.Width, realY + m_bounds.Height, 0, 0, 0, 0, top,
                             top, bottom, bottom);

    g_editorFont.SetRenderShadow(true);
    if (m_disabled)
      g_editorFont.SetColor(128, 128, 75, 255);
    else
      g_editorFont.SetColor(255, 255, 150, 255);

    if (m_centered)
      g_editorFont.DrawText2DCenter(realX + m_bounds.Width / 2, y, m_fontSize, m_caption);
    else
      g_editorFont.DrawText2D(realX + 5, y, m_fontSize, m_caption);
    g_editorFont.SetRenderShadow(false);
  }
  else
  {
    Render2D::FillRect(realX, realY, realX + m_bounds.Width, realY + m_bounds.Height, Render2D::Rgba(107, 37, 39, 64));

    // Bevel: light top/left, dark right/bottom.
    const uint32_t light = Render2D::Rgba(100, 34, 34, 200);
    const uint32_t dark = Render2D::Rgba(26, 0, 0, 255);
    Render2D::DrawLine(realX, realY + m_bounds.Height, realX, realY, light);
    Render2D::DrawLine(realX, realY, realX + m_bounds.Width, realY, light);
    Render2D::DrawLine(realX + m_bounds.Width, realY, realX + m_bounds.Width, realY + m_bounds.Height, dark);
    Render2D::DrawLine(realX + m_bounds.Width, realY + m_bounds.Height, realX, realY + m_bounds.Height, dark);

    if (m_disabled)
      g_editorFont.SetColor(128, 128, 128, 255);
    else
      g_editorFont.SetColor(255, 255, 255, 255);

    if (m_centered)
      g_editorFont.DrawText2DCenter(realX + m_bounds.Width / 2, y, m_fontSize, m_caption);
    else
      g_editorFont.DrawText2D(realX + 5, y, m_fontSize, m_caption);
  }
}

void GuiButton::SetProperties(const std::string& _name, int _x, int _y, int _w, int _h, const std::string& _caption,
                              const std::string& _tooltip)
{
  if (_w == -1)
    _w = _name.size() * 7 + 9;

  if (_h == -1)
    _h = 15;

  m_name = _name;

  m_bounds.X = _x;
  m_bounds.Y = _y;
  m_bounds.Width = _w;
  m_bounds.Height = _h;
  SetCaption(_caption.empty() ? _name : _caption);
  SetTooltip(_tooltip);
}

void GuiButton::UpdateButtonHighlight()
{
  auto parent = m_parent;

  if (parent->m_buttonChangedThisUpdate)
    m_mouseHighlightMode = false;

  if (Canvas::EclMouseInButton(m_parent, this))
  {
    if (!m_highlightedThisFrame)
    {
      parent->SetCurrentButton(this);
      m_highlightedThisFrame = true;
      m_mouseHighlightMode = true;
    }
  }
  else
    m_highlightedThisFrame = false;
}

BorderlessButton::BorderlessButton()
  : GuiButton() {}

void BorderlessButton::Render(int realX, int realY, bool highlighted, bool clicked)
{
  auto parent = m_parent;
  if (IsCurrentButton(parent, this))
    clicked = true;
  if (clicked)
  {
    const uint32_t top = Render2D::Rgba(199, 214, 220, 255);
    const uint32_t bottom = Render2D::Rgba(112, 141, 168, 255);
    Render2D::TexQuadColored(nullptr, realX, realY, realX + m_bounds.Width, realY + m_bounds.Height, 0, 0, 0, 0, top,
                             top, bottom, bottom);

    g_editorFont.SetRenderShadow(true);
    g_editorFont.SetColor(255, 255, 150, 255);
    if (m_centered)
      g_editorFont.DrawText2DCenter(realX + m_bounds.Width / 2, realY + 10, m_fontSize, m_caption);
    else
      g_editorFont.DrawText2D(realX + 5, realY + 10, m_fontSize, m_caption);
    g_editorFont.SetRenderShadow(false);
  }
  else
  {
    if (highlighted)
    {
      parent->SetCurrentButton(this);
      const uint32_t light = Render2D::Rgba(100, 34, 34, 250);
      const uint32_t dark = Render2D::Rgba(0, 0, 0, 255);
      Render2D::DrawLine(realX, realY + m_bounds.Height, realX, realY, light);
      Render2D::DrawLine(realX, realY, realX + m_bounds.Width, realY, light);
      Render2D::DrawLine(realX + m_bounds.Width, realY, realX + m_bounds.Width, realY + m_bounds.Height, dark);
      Render2D::DrawLine(realX + m_bounds.Width, realY + m_bounds.Height, realX, realY + m_bounds.Height, dark);
    }

    g_editorFont.SetColor(255, 255, 255, 255);
    if (m_centered)
      g_editorFont.DrawText2DCenter(realX + m_bounds.Width / 2, realY + 10, m_fontSize, m_caption);
    else
      g_editorFont.DrawText2D(realX + 5, realY + 10, m_fontSize, m_caption);

    if (highlighted)
    {
      g_editorFont.SetColor(255, 255, 255, 128);
      if (m_centered)
        g_editorFont.DrawText2DCenter(realX + m_bounds.Width / 2, realY + 10, m_fontSize, m_caption);
      else
        g_editorFont.DrawText2D(realX + 5, realY + 10, m_fontSize, m_caption);
    }
  }

  GuiButton::Render(realX, realY, highlighted, clicked);
}

void GameExitButton::MouseUp() { platform_request_quit(); }

CloseButton::CloseButton()
  : GuiButton(),
    m_iconised(false) {}

void CloseButton::MouseUp() { Canvas::EclRemoveWindow(m_parent->m_name); }

void CloseButton::Render(int realX, int realY, bool highlighted, bool clicked)
{
  if (m_iconised)
  {
    const uint32_t fill =
      (highlighted || clicked) ? Render2D::Rgba(160, 137, 139, 64) : Render2D::Rgba(60, 37, 39, 64);
    Render2D::FillRect(realX, realY, realX + m_bounds.Width, realY + m_bounds.Height, fill);

    const uint32_t light = Render2D::Rgba(0, 0, 150, 100);
    const uint32_t dark = Render2D::Rgba(26, 0, 0, 255);
    Render2D::DrawLine(realX, realY + m_bounds.Height, realX, realY, light);
    Render2D::DrawLine(realX, realY, realX + m_bounds.Width, realY, light);
    Render2D::DrawLine(realX + m_bounds.Width, realY, realX + m_bounds.Width, realY + m_bounds.Height, dark);
    Render2D::DrawLine(realX + m_bounds.Width, realY + m_bounds.Height, realX, realY + m_bounds.Height, dark);
  }
  else
    GuiButton::Render(realX, realY, highlighted, clicked);
}

void LabelButton::Render(int realX, int realY, bool highlighted, bool clicked)
{
  if (m_disabled)
    g_editorFont.SetColor(128, 128, 128, 255);
  else
    g_editorFont.SetColor(255, 255, 255, 255);

  g_editorFont.DrawText2D(realX + 5, realY + 10, 11.0f, m_caption);
}

void InvertedBox::Render(int realX, int realY, bool highlighted, bool clicked)
{
  Render2D::FillRect(realX, realY, realX + m_bounds.Width, realY + m_bounds.Height, Render2D::Rgba(13, 0, 0, 102));

  // Border lines: black top/left, dark-red right/bottom.
  const uint32_t blackBorder = Render2D::Rgba(0, 0, 0, 255);
  const uint32_t redBorder = Render2D::Rgba(100, 34, 34, 150);
  Render2D::DrawLine(realX, realY, realX + m_bounds.Width, realY, blackBorder);
  Render2D::DrawLine(realX, realY, realX, realY + m_bounds.Height, blackBorder);
  Render2D::DrawLine(realX + m_bounds.Width, realY, realX + m_bounds.Width, realY + m_bounds.Height, redBorder);
  Render2D::DrawLine(realX, realY + m_bounds.Height, realX + m_bounds.Width, realY + m_bounds.Height, redBorder);
}
