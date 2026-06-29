#include "pch.h"
#include "GameWindows.h"

#include "GuiWindow.h"
#include "GuiButton.h"
#include "Canvas.h"
#include "GuiOverlay.h"
#include "GraphicsCore.h"

#include <string>
#include <string_view>
#include <utility>
#include <vector>

// Game config globals (declared in elite.h) and the config writer (file.h),
// re-declared here so this winrt/widget-based translation unit stays free of the
// legacy game headers (which define macros that don't mix with the GUI headers).
extern int wireframe;
extern int anti_alias_gfx;
extern int planet_render_style;
extern int hoopy_casinos;
extern int instant_dock;
extern void write_config_file(void);

namespace
{
  // Centre a window of (w,h) in the current client area, like the engine overlay does.
  void Centre(GuiWindow* _window, int _width, int _height)
  {
    const auto sz = Neuron::Graphics::Core::GetOutputSize();
    _window->SetSize(_width, _height);
    _window->SetPosition((static_cast<int>(sz.Width) - _width) / 2, (static_cast<int>(sz.Height) - _height) / 2);
    _window->SetMovable(true);
  }

  // ----- Game Settings ------------------------------------------------------

  // A row that cycles a bound int through a fixed list of labels on click, mirroring
  // the legacy toggle_setting() behaviour. The caption shows "<label>:  <value>".
  class CycleButton : public GuiButton
  {
    public:
      CycleButton(std::string _label, int* _value, std::vector<std::string> _options)
        : m_label(std::move(_label)), m_value(_value), m_options(std::move(_options))
      {
        m_centered = true;
      }

      void MouseUp() override
      {
        if (!m_value || m_options.empty())
          return;
        *m_value = (*m_value + 1) % static_cast<int>(m_options.size());
        Refresh();
      }

      // Rebuild the caption from the current bound value (clamped defensively).
      void Refresh()
      {
        int v = m_value ? *m_value : 0;
        if (v < 0 || v >= static_cast<int>(m_options.size()))
          v = 0;
        SetCaption(m_label + ":  " + (m_options.empty() ? std::string() : m_options[v]));
      }

    private:
      std::string m_label;
      int* m_value;
      std::vector<std::string> m_options;
  };

  // Persists the current settings to the config file (the legacy "Save Settings" row).
  class SaveSettingsButton : public GuiButton
  {
    public:
      SaveSettingsButton() { m_centered = true; }
      void MouseUp() override { write_config_file(); }
  };

  class SettingsWindow : public GuiWindow
  {
    public:
      SettingsWindow()
        : GuiWindow("Settings")
      {
        SetTitle("Game Settings");
        Centre(this, 300, 220);
      }

      void Create() override
      {
        GuiWindow::Create(); // iconised close button (top-right)
        m_buttonOrder.clear();

        const int margin = 10;
        const int rowH = 22;
        const int btnH = 18;
        const int x = margin;
        const int w = static_cast<int>(m_w) - 2 * margin;
        int y = 30;

        auto addCycle = [&](const std::string& label, int* value, std::vector<std::string> options) {
          auto* button = new CycleButton(label, value, std::move(options));
          button->SetProperties(label, x, y, w, btnH, label);
          button->Refresh();
          RegisterButton(button);
          m_buttonOrder.push_back(button);
          y += rowH;
        };

        // Mirror options.cpp's setting_list (name + value labels) and its global mapping.
        addCycle("Graphics", &wireframe, {"Solid", "Wireframe"});
        addCycle("Anti Alias", &anti_alias_gfx, {"Off", "On"});
        addCycle("Planet Style", &planet_render_style, {"Wireframe", "Green", "SNES", "Fractal"});
        addCycle("Planet Desc.", &hoopy_casinos, {"BBC", "MSX"});
        addCycle("Instant Dock", &instant_dock, {"Off", "On"});

        y += 4;
        auto* save = new SaveSettingsButton();
        save->SetProperties("Save", x, y, w, btnH, "Save Settings");
        RegisterButton(save);
        m_buttonOrder.push_back(save);
        y += rowH;

        auto* close = new CloseButton();
        close->m_centered = true;
        close->SetProperties("Close", x, y, w, btnH, "Close");
        RegisterButton(close);
        m_buttonOrder.push_back(close);

        m_currentButton = 0;
      }
  };

  // ----- Quit confirmation --------------------------------------------------

  // A small modal: "Quit game?" with Yes (engine GameExitButton -> request quit) and
  // No (CloseButton -> dismiss).
  class QuitConfirmWindow : public GuiWindow
  {
    public:
      QuitConfirmWindow()
        : GuiWindow("QuitConfirm")
      {
        SetTitle("Quit");
        Centre(this, 240, 120);
      }

      void Create() override
      {
        GuiWindow::Create();
        m_buttonOrder.clear();

        auto* label = new LabelButton();
        label->SetProperties("QuitPrompt", 10, 30, 220, 15, "Quit game?");
        RegisterButton(label);

        auto* yes = new GameExitButton();
        yes->m_centered = true;
        yes->SetProperties("Yes", 10, 60, 105, 18, "Yes");
        RegisterButton(yes);
        m_buttonOrder.push_back(yes);

        auto* no = new CloseButton();
        no->m_centered = true;
        no->SetProperties("No", 125, 60, 105, 18, "No");
        RegisterButton(no);
        m_buttonOrder.push_back(no);

        m_currentButton = 1; // default to No
      }
  };

  // ----- Options menu -------------------------------------------------------

  // Opens (or focuses) a registered window, creating it via `make` the first time.
  template <typename T>
  void OpenWindow(std::string_view _name)
  {
    if (Canvas::EclGetWindow(_name))
      Canvas::EclBringWindowToFront(_name);
    else
      Canvas::EclRegisterWindow(new T());
  }

  class OpenSettingsButton : public GuiButton
  {
    public:
      void MouseUp() override { OpenWindow<SettingsWindow>(std::string_view("Settings")); }
  };

  class OpenQuitButton : public GuiButton
  {
    public:
      void MouseUp() override { OpenWindow<QuitConfirmWindow>(std::string_view("QuitConfirm")); }
  };

  // The game's Options menu (named "Options" so the overlay's open/dedup matches).
  // Mirrors options.cpp's option_list, minus Save/Load Commander for now (those still
  // route through legacy gfx_display_* file-entry screens not yet migrated).
  class OptionsMenuWindow : public GuiWindow
  {
    public:
      OptionsMenuWindow()
        : GuiWindow("Options")
      {
        SetTitle("Options");
        Centre(this, 260, 140);
      }

      void Create() override
      {
        GuiWindow::Create();
        m_buttonOrder.clear();

        const int x = 10;
        const int w = static_cast<int>(m_w) - 20;
        int y = 30;

        auto* settings = new OpenSettingsButton();
        settings->m_centered = true;
        settings->SetProperties("GameSettings", x, y, w, 18, "Game Settings");
        RegisterButton(settings);
        m_buttonOrder.push_back(settings);
        y += 26;

        auto* quit = new OpenQuitButton();
        quit->m_centered = true;
        quit->SetProperties("Quit", x, y, w, 18, "Quit");
        RegisterButton(quit);
        m_buttonOrder.push_back(quit);
        y += 26;

        auto* close = new CloseButton();
        close->m_centered = true;
        close->SetProperties("Close", x, y, w, 18, "Close");
        RegisterButton(close);
        m_buttonOrder.push_back(close);

        m_currentButton = 0;
      }
  };
}

void RegisterGameWindows()
{
  GuiOverlay::SetOptionsWindowFactory([]() -> GuiWindow* { return new OptionsMenuWindow(); });
}
