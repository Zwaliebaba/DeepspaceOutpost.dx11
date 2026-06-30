#include "pch.h"
#include "GuiOverlay.h"

#include "Canvas.h"
#include "GuiButton.h"
#include "GuiWindow.h"
#include "Render2D.h"
#include "Strings.h"

#include "input_win.h"

using Neuron::Graphics::Core;
using Neuron::Graphics::Render2D;

namespace
{
  bool s_ready = false;   // overlay initialised (ClientEngine brought up the device/canvas)
  bool s_shown = false;   // overlay currently visible (opened on demand by F8/F11)

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

  // Built-in placeholder Options window: shown only as the no-factory fallback for
  // OpenOptions (the game normally supplies the real Options window via
  // SetOptionsWindowFactory). Kept so the overlay still does something useful if a
  // host hasn't registered one.
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

}

void GuiOverlay::Startup()
{
  // ClientEngine has already brought up Core / Render2D / Canvas / fonts /
  // Strings by the time this runs. The overlay starts hidden; the game opens it on
  // demand (F8 market, F11 options) via Open() / ShowWindow().
  s_ready = true;
}

void GuiOverlay::Shutdown()
{
  // Canvas / Render2D are torn down by ClientEngine::Shutdown().
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

void GuiOverlay::ShowWindow(std::string_view _name, const std::function<GuiWindow*()>& _factory)
{
  if (!s_ready)
    return;
  s_shown = true;
  if (Canvas::EclGetWindow(_name))
  {
    Canvas::EclBringWindowToFront(_name);
    return;
  }
  if (_factory)
  {
    GuiWindow* window = _factory();
    if (window)
      Canvas::EclRegisterWindow(window);
  }
}

void GuiOverlay::Update()
{
  if (!s_ready)
    return;

  // Refresh edge state once per frame so menu navigation steps once per keypress.
  input_update_menu_edges();

  // Windows may have been dismissed (Esc / close button) from within EclUpdate last
  // frame; when none remain, drop out of "shown" so input returns to the game. This
  // keeps the overlay up while any window (or sub-window) is still open and closes it
  // automatically once the last one is dismissed.
  const auto* windows = Canvas::EclGetWindows();
  if (s_shown && (!windows || windows->empty()))
    s_shown = false;

  // While the overlay is up it owns input: hide the keyboard from the game.
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

  // gfx2d_flush has already drawn the (letterboxed) game to the back buffer this frame;
  // the GUI draws full-window on top in client-pixel space, matching where Canvas places
  // windows. Open one native 2D pass (client-space ortho, Y down, alpha blend, no
  // depth/cull, 1:1 mapping) and let Canvas submit every window/button/glyph into the
  // batch, flushed at End.
  Render2D::Begin(Core::GetRenderTargetView(), clientWidth, clientHeight);
  Canvas::Render();
  Render2D::End();
}
