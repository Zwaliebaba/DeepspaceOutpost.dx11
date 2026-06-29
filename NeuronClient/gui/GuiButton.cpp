#include "pch.h"
#include "GuiButton.h"
#include "Canvas.h"
#include "TextRenderer.h"
#include "GameApp.h"
#include "GuiWindow.h"
#include "ImmediateRenderer.h"

using Neuron::Graphics::ImmediateRenderer;
using Neuron::Graphics::Primitive;

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
    ImmediateRenderer::Begin(Primitive::Quads);
    ImmediateRenderer::ColorBytes(199, 214, 220, 255);
    if (clicked)
      ImmediateRenderer::ColorBytes(255, 255, 255, 255);
    ImmediateRenderer::Vertex(realX, realY);
    ImmediateRenderer::Vertex(realX + m_bounds.Width, realY);
    ImmediateRenderer::ColorBytes(112, 141, 168, 255);
    if (clicked)
      ImmediateRenderer::ColorBytes(162, 191, 208, 255);
    ImmediateRenderer::Vertex(realX + m_bounds.Width, realY + m_bounds.Height);
    ImmediateRenderer::Vertex(realX, realY + m_bounds.Height);
    ImmediateRenderer::End();

    g_editorFont.SetRenderShadow(true);

    if (m_disabled)
      ImmediateRenderer::ColorBytes(128, 128, 75, 30);
    else
      ImmediateRenderer::ColorBytes(255, 255, 150, 30);

    if (m_centered)
    {
      g_editorFont.DrawText2DCenter(realX + m_bounds.Width / 2, y, m_fontSize, m_caption);
      g_editorFont.DrawText2DCenter(realX + m_bounds.Width / 2, y, m_fontSize, m_caption);
    }
    else
    {
      g_editorFont.DrawText2D(realX + 5, y, m_fontSize, m_caption);
      g_editorFont.DrawText2D(realX + 5, y, m_fontSize, m_caption);
    }
    g_editorFont.SetRenderShadow(false);
  }
  else
  {
    ImmediateRenderer::ColorBytes(107, 37, 39, 64);
    ImmediateRenderer::Begin(Primitive::Quads);
    ImmediateRenderer::Vertex(realX, realY);
    ImmediateRenderer::Vertex(realX + m_bounds.Width, realY);
    ImmediateRenderer::Vertex(realX + m_bounds.Width, realY + m_bounds.Height);
    ImmediateRenderer::Vertex(realX, realY + m_bounds.Height);
    ImmediateRenderer::End();

    ImmediateRenderer::Begin(Primitive::Lines);
    ImmediateRenderer::ColorBytes(100, 34, 34, 200);
    ImmediateRenderer::Vertex(realX, realY + m_bounds.Height);
    ImmediateRenderer::Vertex(realX, realY);

    ImmediateRenderer::Vertex(realX, realY);
    ImmediateRenderer::Vertex(realX + m_bounds.Width, realY);

    ImmediateRenderer::Color(0.1f, 0.0f, 0.0f, 1.0f);
    ImmediateRenderer::Vertex(realX + m_bounds.Width, realY);
    ImmediateRenderer::Vertex(realX + m_bounds.Width, realY + m_bounds.Height);

    ImmediateRenderer::Vertex(realX + m_bounds.Width, realY + m_bounds.Height);
    ImmediateRenderer::Vertex(realX, realY + m_bounds.Height);
    ImmediateRenderer::End();

    if (m_disabled)
      ImmediateRenderer::Color(0.5f, 0.5f, 0.5f, 1.0f);
    else
      ImmediateRenderer::Color(1.0f, 1.0f, 1.0f, 1.0f);

    if (m_centered)
      g_editorFont.DrawText2DCenter(realX + m_bounds.Width / 2, y, m_fontSize, m_caption);
    else
      g_editorFont.DrawText2D(realX + 5, y, m_fontSize, m_caption);
  }

  ImmediateRenderer::Color(1.0f, 1.0f, 1.0f, 1.0f);
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
    ImmediateRenderer::Begin(Primitive::Quads);
    ImmediateRenderer::ColorBytes(199, 214, 220, 255);
    ImmediateRenderer::Vertex(realX, realY);
    ImmediateRenderer::Vertex(realX + m_bounds.Width, realY);
    ImmediateRenderer::ColorBytes(112, 141, 168, 255);
    ImmediateRenderer::Vertex(realX + m_bounds.Width, realY + m_bounds.Height);
    ImmediateRenderer::Vertex(realX, realY + m_bounds.Height);
    ImmediateRenderer::End();

    g_editorFont.SetRenderShadow(true);
    ImmediateRenderer::ColorBytes(255, 255, 150, 30);
    if (m_centered)
    {
      g_editorFont.DrawText2DCenter(realX + m_bounds.Width / 2, realY + 10, m_fontSize, m_caption);
      g_editorFont.DrawText2DCenter(realX + m_bounds.Width / 2, realY + 10, m_fontSize, m_caption);
    }
    else
    {
      g_editorFont.DrawText2D(realX + 5, realY + 10, m_fontSize, m_caption);
      g_editorFont.DrawText2D(realX + 5, realY + 10, m_fontSize, m_caption);
    }
    g_editorFont.SetRenderShadow(false);
  }
  else
  {
    ImmediateRenderer::ColorBytes(107, 37, 39, 64);

    if (highlighted)
    {
      parent->SetCurrentButton(this);
      ImmediateRenderer::Begin(Primitive::Lines);
      ImmediateRenderer::ColorBytes(100, 34, 34, 250);
      ImmediateRenderer::Vertex(realX, realY + m_bounds.Height);
      ImmediateRenderer::Vertex(realX, realY);

      ImmediateRenderer::Vertex(realX, realY);
      ImmediateRenderer::Vertex(realX + m_bounds.Width, realY);

      ImmediateRenderer::Color(0.0f, 0.0f, 0.0f, 1.0f);
      ImmediateRenderer::Vertex(realX + m_bounds.Width, realY);
      ImmediateRenderer::Vertex(realX + m_bounds.Width, realY + m_bounds.Height);

      ImmediateRenderer::Vertex(realX + m_bounds.Width, realY + m_bounds.Height);
      ImmediateRenderer::Vertex(realX, realY + m_bounds.Height);
      ImmediateRenderer::End();
    }

    ImmediateRenderer::Color(1.0f, 1.0f, 1.0f, 1.0f);
    if (m_centered)
      g_editorFont.DrawText2DCenter(realX + m_bounds.Width / 2, realY + 10, m_fontSize, m_caption);
    else
      g_editorFont.DrawText2D(realX + 5, realY + 10, m_fontSize, m_caption);

    if (highlighted)
    {
      ImmediateRenderer::Color(1.0f, 1.0f, 1.0f, 0.5f);
      if (m_centered)
        g_editorFont.DrawText2DCenter(realX + m_bounds.Width / 2, realY + 10, m_fontSize, m_caption);
      else
        g_editorFont.DrawText2D(realX + 5, realY + 10, m_fontSize, m_caption);
    }
  }

  ImmediateRenderer::Color(1.0f, 1.0f, 1.0f, 1.0f);

  GuiButton::Render(realX, realY, highlighted, clicked);
}

void GameExitButton::MouseUp() { g_app->m_requestQuit = true; }

CloseButton::CloseButton()
  : GuiButton(),
    m_iconised(false) {}

void CloseButton::MouseUp() { Canvas::EclRemoveWindow(m_parent->m_name); }

void CloseButton::Render(int realX, int realY, bool highlighted, bool clicked)
{
  if (m_iconised)
  {
    if (highlighted || clicked)
      ImmediateRenderer::ColorBytes(160, 137, 139, 64);
    else
      ImmediateRenderer::ColorBytes(60, 37, 39, 64);

    ImmediateRenderer::Begin(Primitive::Quads);
    ImmediateRenderer::Vertex(realX, realY);
    ImmediateRenderer::Vertex(realX + m_bounds.Width, realY);
    ImmediateRenderer::Vertex(realX + m_bounds.Width, realY + m_bounds.Height);
    ImmediateRenderer::Vertex(realX, realY + m_bounds.Height);
    ImmediateRenderer::End();

    ImmediateRenderer::Begin(Primitive::Lines);
    ImmediateRenderer::ColorBytes(0, 0, 150, 100);
    ImmediateRenderer::Vertex(realX, realY + m_bounds.Height);
    ImmediateRenderer::Vertex(realX, realY);

    ImmediateRenderer::Vertex(realX, realY);
    ImmediateRenderer::Vertex(realX + m_bounds.Width, realY);

    ImmediateRenderer::Color(0.1f, 0.0f, 0.0f, 1.0f);
    ImmediateRenderer::Vertex(realX + m_bounds.Width, realY);
    ImmediateRenderer::Vertex(realX + m_bounds.Width, realY + m_bounds.Height);

    ImmediateRenderer::Vertex(realX + m_bounds.Width, realY + m_bounds.Height);
    ImmediateRenderer::Vertex(realX, realY + m_bounds.Height);
    ImmediateRenderer::End();
  }
  else
    GuiButton::Render(realX, realY, highlighted, clicked);
}

void LabelButton::Render(int realX, int realY, bool highlighted, bool clicked)
{
  if (m_disabled)
    ImmediateRenderer::Color(0.5f, 0.5f, 0.5f, 1.0f);
  else
    ImmediateRenderer::Color(1.0f, 1.0f, 1.0f, 1.0f);

  g_editorFont.DrawText2D(realX + 5, realY + 10, 11.0f, m_caption);
}

void InvertedBox::Render(int realX, int realY, bool highlighted, bool clicked)
{
  ImmediateRenderer::Color(0.05f, 0.0f, 0.0f, 0.4f);
  ImmediateRenderer::Begin(Primitive::Quads);
  ImmediateRenderer::Vertex(realX, realY);
  ImmediateRenderer::Vertex(realX + m_bounds.Width, realY);
  ImmediateRenderer::Vertex(realX + m_bounds.Width, realY + m_bounds.Height);
  ImmediateRenderer::Vertex(realX, realY + m_bounds.Height);
  ImmediateRenderer::End();

  //
  // Border lines

  ImmediateRenderer::Color(0.0f, 0.0f, 0.0f, 1.0f);
  ImmediateRenderer::Begin(Primitive::Lines); // top
  ImmediateRenderer::Vertex(realX, realY);
  ImmediateRenderer::Vertex(realX + m_bounds.Width, realY);
  ImmediateRenderer::End();

  ImmediateRenderer::Begin(Primitive::Lines); // left
  ImmediateRenderer::Vertex(realX, realY);
  ImmediateRenderer::Vertex(realX, realY + m_bounds.Height);
  ImmediateRenderer::End();

  ImmediateRenderer::ColorBytes(100, 34, 34, 150);
  ImmediateRenderer::Begin(Primitive::Lines);
  ImmediateRenderer::Vertex(realX + m_bounds.Width, realY);
  ImmediateRenderer::Vertex(realX + m_bounds.Width, realY + m_bounds.Height);
  ImmediateRenderer::End();

  ImmediateRenderer::Begin(Primitive::Lines); // bottom
  ImmediateRenderer::Vertex(realX, realY + m_bounds.Height);
  ImmediateRenderer::Vertex(realX + m_bounds.Width, realY + m_bounds.Height);
  ImmediateRenderer::End();
}
