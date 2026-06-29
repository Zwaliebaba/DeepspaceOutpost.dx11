#include "pch.h"
#include "GuiOverlay.h"

#include "Canvas.h"
#include "GuiButton.h"
#include "GuiWindow.h"
#include "ImmediateRenderer.h"
#include "Strings.h"

#include "input_win.h"

using Neuron::Graphics::ImmediateRenderer;
using Neuron::Graphics::MatrixStackId;

namespace
{
  bool s_ready = false;   // demo window registered (ClientEngine brought up the device/canvas)
  bool s_shown = false;   // overlay currently visible (F1 toggles)
  bool s_prevF1 = false;  // edge-detect the F1 toggle

  // Game-supplied factory for the real Options window (set via
  // GuiOverlay::SetOptionsWindowFactory). Null until the game registers it.
  std::function<GuiWindow*()> s_optionsFactory;

  // Centre a window of (w,h) in the current client area.
  void CentreWindow(GuiWindow* w, int width, int height)
  {
    const auto sz = Neuron::Graphics::Core::GetOutputSize();
    w->SetSize(width, height);
    w->SetPosition((static_cast<int>(sz.Width) - width) / 2, (static_cast<int>(sz.Height) - height) / 2);
  }

  // A modal Options window (the "one real screen" for this phase): a GuiWindow with a
  // couple of rows and a Close button, opened from the main menu. Wiring its controls
  // to actual game preferences is the next increment.
  class OptionsWindow : public GuiWindow
  {
    public:
      OptionsWindow()
        : GuiWindow("Options")
      {
        SetTitle("Options");
        CentreWindow(this, 260, 140);
        SetMovable(true);
      }

      void Create() override
      {
        GuiWindow::Create(); // iconised close button (top-right)

        auto label = NEW LabelButton();
        label->SetProperties("OptLabel", 10, 24, 240, 15, Strings::Get(std::string("Menu_Options")));
        RegisterButton(label);

        // A normal (non-iconised) CloseButton dismisses the window on click/Enter.
        auto close = NEW CloseButton();
        close->m_centered = true;
        close->SetProperties("OptClose", 10, 100, 240, 18, Strings::Get(std::string("Button_Close")));
        RegisterButton(close);

        m_buttonOrder.clear();
        m_buttonOrder.push_back(close);
        m_currentButton = 0;
      }
  };

  // Open (or focus) the Options window. The game supplies the real one via the factory;
  // absent that, the built-in placeholder above is shown.
  void OpenOptions()
  {
    if (Canvas::EclGetWindow(std::string_view("Options")))
    {
      Canvas::EclBringWindowToFront(std::string_view("Options"));
      return;
    }
    GuiWindow* window = s_optionsFactory ? s_optionsFactory() : static_cast<GuiWindow*>(NEW OptionsWindow());
    if (window)
      Canvas::EclRegisterWindow(window);
  }

  // Main-menu "Options" button.
  class OpenOptionsButton : public GuiButton
  {
    public:
      void MouseUp() override { OpenOptions(); }
  };

  // The main menu: a centred GuiWindow with localized, mouse- and keyboard-driven
  // buttons. Title bar + panel use the imported interface texture + fonts.
  class MainMenuWindow : public GuiWindow
  {
    public:
      MainMenuWindow()
        : GuiWindow("MainMenu")
      {
        SetTitle("Deepspace Outpost");
        CentreWindow(this, 260, 150);
        SetMovable(true);
      }

      void Create() override
      {
        GuiWindow::Create(); // iconised close button

        auto heading = NEW LabelButton();
        heading->SetProperties("Heading", 10, 24, 240, 15, Strings::Get(std::string("AppName")));
        RegisterButton(heading);

        auto options = NEW OpenOptionsButton();
        options->m_centered = true;
        options->SetProperties("Options", 10, 74, 240, 18, Strings::Get(std::string("Menu_Options")));
        RegisterButton(options);

        auto quit = NEW GameExitButton();
        quit->m_centered = true;
        quit->SetProperties("Quit", 10, 98, 240, 18, Strings::Get(std::string("Menu_Quit")));
        RegisterButton(quit);

        m_buttonOrder.clear();
        m_buttonOrder.push_back(options);
        m_buttonOrder.push_back(quit);
        m_currentButton = 0;
      }
  };

  void EnsureMenu()
  {
    if (!Canvas::EclGetWindow(std::string_view("MainMenu")))
      Canvas::EclRegisterWindow(NEW MainMenuWindow());
  }
}

void GuiOverlay::Startup()
{
  // ClientEngine has already brought up Core / ImmediateRenderer / Canvas / fonts /
  // Strings by the time this runs; the overlay just registers its demo window.
  if (s_ready)
    return;
  EnsureMenu();
  s_ready = true;
}

void GuiOverlay::Shutdown()
{
  // Canvas / ImmediateRenderer are torn down by ClientEngine::Shutdown().
  s_ready = false;
  s_shown = false;
}

bool GuiOverlay::IsReady() { return s_ready; }

void GuiOverlay::SetOptionsWindowFactory(std::function<GuiWindow*()> _factory)
{
  s_optionsFactory = std::move(_factory);
}

void GuiOverlay::Open()
{
  if (!s_ready)
    return;
  s_shown = true;
  OpenOptions();
}

bool GuiOverlay::IsShown() { return s_shown; }

void GuiOverlay::Update()
{
  if (!s_ready)
    return;

  // Refresh edge state once per frame so menu navigation steps once per keypress.
  input_update_menu_edges();

  // F1 toggles the menu. Read the raw key: while the menu owns input the game's kbd_*
  // snapshot is suppressed, so it can't be used to toggle back off.
  const bool f1 = input_key_down(VK_F1);
  if (f1 && !s_prevF1)
  {
    s_shown = !s_shown;
    if (s_shown)
      EnsureMenu(); // re-create if a previous Esc closed it
  }
  s_prevF1 = f1;

  // Windows may have been dismissed (Esc / close button) from within EclUpdate last
  // frame; when none remain, drop out of "shown" so input returns to the game. This
  // covers both entry points (F1 opens MainMenu; F11/GuiOverlay::Open opens Options
  // directly without a MainMenu) and keeps the overlay up while any sub-window (Game
  // Settings, Quit confirmation) is still open.
  const auto* windows = Canvas::EclGetWindows();
  if (s_shown && (!windows || windows->empty()))
    s_shown = false;

  // While the menu is up it owns input: hide the keyboard from the game.
  input_suppress_game_keys(s_shown);

  if (!s_shown)
    return;

  // Feed the mouse (client-pixel space, matching the full-window GUI) so Canvas can
  // hover/click/drag, then run keyboard nav as a transitional fallback.
  int mx = 0, my = 0;
  bool lmb = false, rmb = false;
  input_mouse_state(mx, my, lmb, rmb);
  Canvas::EclUpdateMouse(mx, my, lmb, rmb);

  Canvas::EclUpdate(); // edge-triggered keyboard navigation (transitional)
}

void GuiOverlay::Render(int clientWidth, int clientHeight)
{
  if (!s_ready || !s_shown)
    return;

  // The back buffer is already bound with a full client-area viewport (the Renderer's
  // blitCanvasToBackBuffer ran just before us), so the GUI draws full-window on top of
  // the letterboxed game - in client-pixel space, matching where Canvas places windows.

  // Screen-space (client-space) orthographic projection, Y down.
  ImmediateRenderer::UseProgram(Neuron::Graphics::ShaderProgram::Generic);
  ImmediateRenderer::SetMatrixMode(MatrixStackId::Projection);
  ImmediateRenderer::PushMatrix();
  ImmediateRenderer::LoadIdentity();
  ImmediateRenderer::Ortho2D(0.0f, static_cast<float>(clientWidth), static_cast<float>(clientHeight), 0.0f);
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
