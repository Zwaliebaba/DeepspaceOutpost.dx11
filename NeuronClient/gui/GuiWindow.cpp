#include "pch.h"
#include "GuiWindow.h"
#include "Canvas.h"
#include "GraphicsCore.h"
#include "GuiButton.h"
#include "ImmediateRenderer.h"
#include "Input.h"
#include "Resource.h"
#include "TextRenderer.h"

using Neuron::Graphics::ImmediateRenderer;
using Neuron::Graphics::Primitive;

GuiWindow::GuiWindow(std::string_view _name)
  : m_x(0),
    m_y(0),
    m_w(0),
    m_h(0),
    m_resizable(true),
    m_currentButton(0),
    m_buttonChangedThisUpdate(false)
{
  SetName(_name);
  SetTitle(_name);
  SetMovable(true);
  m_currentTextEdit = "None";

  _strupr(m_title.data());

  Canvas::EclSetCurrentFocus(m_name);
}

GuiWindow::~GuiWindow()
{
  std::vector<GuiWindow*>* windows = Canvas::EclGetWindows();
  if (!windows->empty() && (*windows)[0])
    Canvas::EclSetCurrentFocus((*windows)[0]->m_name);

  while (!m_buttons.empty())
  {
    GuiButton* button = m_buttons[0];
    delete button;
    m_buttons.erase(m_buttons.begin() + (0));
  }
  m_buttons.clear();
}

void GuiWindow::Create()
{
  auto close = NEW CloseButton();
  close->SetProperties("Close", m_w - 12, 2, 10, 10, " ", "Close this window");
  close->m_iconised = true;
  RegisterButton(close);
}

void GuiWindow::Remove()
{
  while (m_buttons.size() > 0)
  {
    GuiButton* button = m_buttons[0];
    RemoveButton(button->m_name.c_str());
  }
  m_buttonOrder.clear();
  m_currentButton = 0;
}

// Get the coordinates of the drawable area on the rectangle
int GuiWindow::GetClientRectX1() { return 2; }

int GuiWindow::GetClientRectX2() { return m_w - 2; }

int GuiWindow::GetClientRectY1() { return 15 + 1; }

int GuiWindow::GetClientRectY2() { return m_h - 2; }

void GuiWindow::Render(bool hasFocus)
{
  //
  // Main body fill

  ImmediateRenderer::UseProgram(Neuron::Graphics::ShaderProgram::GuiWindow);
  ImmediateRenderer::BindTexture(0, Resource::GetTexture("Textures\\InterfaceRed.dds"));
  ImmediateRenderer::SetSampler(0, D3D11_FILTER_MIN_MAG_MIP_LINEAR, D3D11_TEXTURE_ADDRESS_WRAP);

  float texH = 1.0f;
  float texW = texH * 512.0f / 64.0f;

  ImmediateRenderer::Color(1.0f, 1.0f, 1.0f, 0.96f);
  ImmediateRenderer::Begin(Primitive::Quads);
  ImmediateRenderer::TexCoord(0.0f, 0.0f);
  ImmediateRenderer::Vertex(m_x, m_y);
  ImmediateRenderer::TexCoord(texW, 0.0f);
  ImmediateRenderer::Vertex(m_x + m_w, m_y);
  ImmediateRenderer::TexCoord(texW, texH);
  ImmediateRenderer::Vertex(m_x + m_w, m_y + m_h);
  ImmediateRenderer::TexCoord(0.0f, texH);
  ImmediateRenderer::Vertex(m_x, m_y + m_h);
  ImmediateRenderer::End();

  ImmediateRenderer::UseProgram(Neuron::Graphics::ShaderProgram::Generic);
  ImmediateRenderer::BindTexture(0, nullptr);

  //
  // Title bar fill

  float titleBarHeight = GetClientRectY1() - 1;
  ImmediateRenderer::Begin(Primitive::Quads);
  ImmediateRenderer::ColorBytes(199, 214, 220, 255);
  ImmediateRenderer::Vertex(m_x, m_y);
  ImmediateRenderer::Vertex(m_x + m_w, m_y);
  ImmediateRenderer::ColorBytes(112, 141, 168, 255);
  ImmediateRenderer::Vertex(m_x + m_w, m_y + titleBarHeight);
  ImmediateRenderer::Vertex(m_x, m_y + titleBarHeight);
  ImmediateRenderer::End();

  ImmediateRenderer::ColorBytes(199, 214, 220, 255);
  ImmediateRenderer::Begin(Primitive::Lines); // top
  ImmediateRenderer::Vertex(m_x, m_y);
  ImmediateRenderer::Vertex(m_x + m_w, m_y);
  ImmediateRenderer::End();

  ImmediateRenderer::Begin(Primitive::Lines); // left
  ImmediateRenderer::Vertex(m_x, m_y);
  ImmediateRenderer::Vertex(m_x, m_y + m_h);
  ImmediateRenderer::End();

  ImmediateRenderer::Begin(Primitive::Lines); // right
  ImmediateRenderer::Vertex(m_x + m_w, m_y);
  ImmediateRenderer::Vertex(m_x + m_w, m_y + m_h);
  ImmediateRenderer::End();

  ImmediateRenderer::Begin(Primitive::Lines); // bottom
  ImmediateRenderer::Vertex(m_x, m_y + m_h);
  ImmediateRenderer::Vertex(m_x + m_w, m_y + m_h);
  ImmediateRenderer::End();

  ImmediateRenderer::ColorBytes(42, 56, 82, 255);
  ImmediateRenderer::Begin(Primitive::LineLoop);
  ImmediateRenderer::Vertex(m_x - 2, m_y - 2);
  ImmediateRenderer::Vertex(m_x + m_w + 1, m_y - 2);
  ImmediateRenderer::Vertex(m_x + m_w + 1, m_y + m_h + 1);
  ImmediateRenderer::Vertex(m_x - 2, m_y + m_h + 1);
  ImmediateRenderer::End();

  g_gameFont.SetRenderShadow(true);
  ImmediateRenderer::ColorBytes(255, 255, 150, 30);
  int y = m_y + 9;
  int fontSize = 12;
  g_gameFont.DrawText2DCenter(m_x + m_w / 2, y, fontSize, m_title.c_str());
  g_gameFont.DrawText2DCenter(m_x + m_w / 2, y, fontSize, m_title.c_str());
  g_gameFont.SetRenderShadow(false);

  for (int i = m_buttons.size() - 1; i >= 0; --i)
  {
    GuiButton* button = m_buttons[i];
    auto bounds = button->GetBounds();

    bool highlighted = Canvas::EclMouseInButton(this, button) || strcmp(m_currentTextEdit.c_str(), button->m_name.c_str()) == 0;
    bool clicked = hasFocus && strcmp(Canvas::EclGetCurrentClickedButton().c_str(), button->m_name.c_str()) == 0;
    button->Render(m_x + bounds.X, m_y + bounds.Y, highlighted, clicked);
  }
}

void GuiWindow::Update()
{
  m_buttonChangedThisUpdate = false;

  if (strcmp(Canvas::EclGetCurrentFocus().c_str(), m_name.c_str()) == 0)
  {
    if (g_inputManager->controlEvent(ControlMenuDown))
    {
      m_buttonChangedThisUpdate = true;
      m_currentButton++;
      m_currentButton = std::min(m_currentButton, static_cast<int>(m_buttonOrder.size()) - 1);
    }
    if (g_inputManager->controlEvent(ControlMenuUp))
    {
      m_buttonChangedThisUpdate = true;
      m_currentButton--;
      m_currentButton = std::max(0, m_currentButton);
    }

    if (g_inputManager->controlEvent(ControlMenuActivate))
    {
      if (!m_buttonOrder.empty() && m_currentButton >= 0 && m_currentButton < static_cast<int>(m_buttonOrder.size()))
      {
        GuiButton* b = m_buttonOrder[m_currentButton];
        if (b)
          b->MouseUp();
      }
    }

    if (g_inputManager->controlEvent(ControlMenuClose))
      Canvas::EclRemoveWindow(m_name);
  }
}

void GuiWindow::SetCurrentButton(const GuiButton* button)
{
  for (int i = 0; i < m_buttonOrder.size(); ++i)
  {
    if (m_buttonOrder[i] == button)
    {
      m_currentButton = i;
      return;
    }
  }
}

void GuiWindow::SetPosition(int _x, int _y)
{
  m_x = _x;
  m_y = _y;
}

void GuiWindow::SetSize(int _w, int _h)
{
  m_w = _w;
  m_h = _h;
}

void GuiWindow::SetMovable(bool _movable) { m_movable = _movable; }

void GuiWindow::MakeAllOnScreen()
{
  int screenW = Neuron::Graphics::Core::GetOutputSize().Width;
  int screenH = Neuron::Graphics::Core::GetOutputSize().Height;
  if (m_x < 10)
    m_x = 10;
  if (m_y < 10)
    m_y = 10;
  if (m_x + m_w > screenW - 10)
    m_x = screenW - m_w - 10;
  if (m_y + m_h > screenH - 10)
    m_y = screenH - m_h - 10;
}

void GuiWindow::BeginTextEdit(std::string_view _name) { m_currentTextEdit = _name; }

void GuiWindow::EndTextEdit() { m_currentTextEdit = "None"; }

void GuiWindow::RegisterButton(GuiButton* button)
{
  button->SetParent(this);

  m_buttons.insert(m_buttons.begin(), button);

  auto bounds = button->GetBounds();
  if (bounds.Y + bounds.Height + 10 > m_h)
  {
    SetSize(m_w, bounds.Y + bounds.Height + 10);
    MakeAllOnScreen();
  }
}

void GuiWindow::RemoveButton(std::string_view _name)
{
  for (int i = 0; i < m_buttons.size(); ++i)
  {
    GuiButton* button = m_buttons[i];
    if (button->m_name == _name)
    {
      m_buttons.erase(m_buttons.begin() + (i));
      delete button;
    }
  }
}

GuiButton* GuiWindow::GetButton(std::string_view _name)
{
  for (int i = 0; i < m_buttons.size(); ++i)
  {
    GuiButton* button = m_buttons[i];
    if (button->m_name == _name)
      return button;
  }

  return nullptr;
}

GuiButton* GuiWindow::GetButton(int _x, int _y)
{
  for (int i = 0; i < m_buttons.size(); ++i)
  {
    GuiButton* button = m_buttons[i];
    auto bounds = button->GetBounds();

    if (_x >= bounds.X && _x <= bounds.X + bounds.Width && _y >= bounds.Y && _y <= bounds.Y + bounds.Height)
      return button;
  }

  return nullptr;
}

void GuiWindow::Keypress(int keyCode, bool shift, bool ctrl, bool alt)
{
  if (GuiButton* currentTextEdit = GetButton(m_currentTextEdit.c_str()))
    currentTextEdit->Keypress(keyCode, shift, ctrl, alt);
}

void GuiWindow::MouseEvent(bool lmb, bool rmb, bool up, bool down) {}
