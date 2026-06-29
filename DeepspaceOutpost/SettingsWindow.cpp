#include "pch.h"
#include "SettingsWindow.h"

#include "GuiButton.h"
#include "GuiOverlay.h"
#include "GraphicsCore.h"

#include <string>
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
  // A row that cycles a bound int through a fixed list of labels on click, mirroring
  // the legacy toggle_setting() behaviour. The caption shows "<label>: <value>".
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
}

SettingsWindow::SettingsWindow()
  : GuiWindow("Options") // named "Options" so the overlay's open/dedup check matches
{
  SetTitle("Game Settings");

  const int width = 300;
  const int height = 220;
  SetSize(width, height);

  // Centre in the client area, like the engine's placeholder window.
  const auto sz = Neuron::Graphics::Core::GetOutputSize();
  SetPosition((static_cast<int>(sz.Width) - width) / 2, (static_cast<int>(sz.Height) - height) / 2);
  SetMovable(true);
}

void SettingsWindow::Create()
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

void RegisterGameWindows()
{
  GuiOverlay::SetOptionsWindowFactory([]() -> GuiWindow* { return new SettingsWindow(); });
}
