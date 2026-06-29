#include "pch.h"
#include "GuiOverlay.h"

#include "Canvas.h"
#include "GuiButton.h"
#include "GuiWindow.h"
#include "ImmediateRenderer.h"
#include "Strings.h"

#include "Renderer.h"
#include "keyboard.h"

using Neuron::Graphics::ImmediateRenderer;
using Neuron::Graphics::MatrixStackId;

namespace
{
  bool s_ready = false;   // demo window registered (ClientEngine brought up the device/canvas)
  bool s_shown = false;   // overlay currently visible (F1 toggles)
  bool s_prevF1 = false;  // edge-detect the F1 toggle

  // A small demonstration menu that exercises the imported stack end to end:
  // a GuiWindow with a title bar (drawn with the interface texture + g_gameFont),
  // localized button captions (Strings), and keyboard navigation.
  class DemoMenuWindow : public GuiWindow
  {
    public:
      DemoMenuWindow()
        : GuiWindow("DemoMenu")
      {
        SetTitle("Deepspace Outpost");
        SetSize(240, 150);
        SetPosition(40, 40);
        SetMovable(true);
      }

      void Create() override
      {
        GuiWindow::Create(); // iconised close button

        auto heading = NEW LabelButton();
        heading->SetProperties("Heading", 10, 22, 220, 15, Strings::Get(std::string("AppName")));
        RegisterButton(heading);

        auto start = NEW GuiButton();
        start->m_centered = true;
        start->SetProperties("Start", 10, 50, 220, 18, Strings::Get(std::string("Menu_Start")));
        RegisterButton(start);

        auto options = NEW GuiButton();
        options->m_centered = true;
        options->SetProperties("Options", 10, 74, 220, 18, Strings::Get(std::string("Menu_Options")));
        RegisterButton(options);

        auto quit = NEW GameExitButton();
        quit->m_centered = true;
        quit->SetProperties("Quit", 10, 98, 220, 18, Strings::Get(std::string("Menu_Quit")));
        RegisterButton(quit);

        // Keyboard navigation order (up/down move between these; Enter activates).
        m_buttonOrder.clear();
        m_buttonOrder.push_back(start);
        m_buttonOrder.push_back(options);
        m_buttonOrder.push_back(quit);
        m_currentButton = 0;
      }
  };

  void EnsureDemoWindow()
  {
    if (!Canvas::EclGetWindow(std::string_view("DemoMenu")))
      Canvas::EclRegisterWindow(NEW DemoMenuWindow());
  }
}

void GuiOverlay::Startup()
{
  // ClientEngine has already brought up Core / ImmediateRenderer / Canvas / fonts /
  // Strings by the time this runs; the overlay just registers its demo window.
  if (s_ready)
    return;
  EnsureDemoWindow();
  s_ready = true;
}

void GuiOverlay::Shutdown()
{
  // Canvas / ImmediateRenderer are torn down by ClientEngine::Shutdown().
  s_ready = false;
  s_shown = false;
}

bool GuiOverlay::IsReady() { return s_ready; }

void GuiOverlay::Update()
{
  if (!s_ready)
    return;

  // Edge-detected F1 toggle (kbd_F1_pressed is level state).
  const bool f1 = kbd_F1_pressed != 0;
  if (f1 && !s_prevF1)
  {
    s_shown = !s_shown;
    if (s_shown)
      EnsureDemoWindow(); // re-create if a previous Esc closed it
  }
  s_prevF1 = f1;

  if (!s_shown)
    return;

  Canvas::EclUpdate(); // keyboard menu navigation (up/down/enter/esc -> keyboard.h)
}

void GuiOverlay::Render(int canvasWidth, int canvasHeight)
{
  if (!s_ready || !s_shown)
    return;

  Renderer* r = platform_renderer();
  if (!r)
    return;

  // Draw into the same 512x514 canvas the game's 2D batch just rendered into, so the
  // overlay is letterboxed identically by the present step.
  r->bindCanvasTarget();

  // Screen-space (canvas-space) orthographic projection, Y down.
  ImmediateRenderer::UseProgram(Neuron::Graphics::ShaderProgram::Generic);
  ImmediateRenderer::SetMatrixMode(MatrixStackId::Projection);
  ImmediateRenderer::PushMatrix();
  ImmediateRenderer::LoadIdentity();
  ImmediateRenderer::Ortho2D(0.0f, static_cast<float>(canvasWidth), static_cast<float>(canvasHeight), 0.0f);
  ImmediateRenderer::SetMatrixMode(MatrixStackId::ModelView);
  ImmediateRenderer::PushMatrix();
  ImmediateRenderer::LoadIdentity();

  ImmediateRenderer::SetBlendEnabled(true);
  ImmediateRenderer::SetBlendFunc(D3D11_BLEND_SRC_ALPHA, D3D11_BLEND_INV_SRC_ALPHA);
  ImmediateRenderer::SetDepthTestEnabled(false);
  ImmediateRenderer::SetDepthWriteEnabled(false);
  ImmediateRenderer::SetCullEnabled(false);
  ImmediateRenderer::SetFogEnabled(false);
  ImmediateRenderer::Color(1.0f, 1.0f, 1.0f, 1.0f);

  Canvas::Render();

  ImmediateRenderer::SetMatrixMode(MatrixStackId::Projection);
  ImmediateRenderer::PopMatrix();
  ImmediateRenderer::SetMatrixMode(MatrixStackId::ModelView);
  ImmediateRenderer::PopMatrix();
}
