#include "pch.h"
#include "GameWindows.h"

#include "GuiWindow.h"
#include "GuiButton.h"
#include "Canvas.h"
#include "GuiOverlay.h"
#include "GraphicsCore.h"

#include <cstdio>
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
extern int scene_shading;
extern int hoopy_casinos;
extern int instant_dock;
extern void write_config_file(void);

// Render-free market API (docked.h / docked.cpp), declared here to keep this TU off
// the legacy game headers.
extern int market_item_count(void);
extern void market_format_row(int item, char* buf, int buflen);
extern int market_credits(void);
extern int market_buy(int item);
extern int market_sell(int item);

// Render-free read-only info screens (docked.h / docked.cpp).
extern int cmdr_status_line_count(void);
extern void cmdr_status_line(int i, char* buf, int buflen);
extern void cmdr_status_title(char* buf, int buflen);
extern int inventory_line_count(void);
extern void inventory_line(int i, char* buf, int buflen);
extern int planet_data_line_count(void);
extern void planet_data_line(int i, char* buf, int buflen);
extern void planet_data_title(char* buf, int buflen);

// Equip-ship screen (docked.h / docked.cpp).
extern int equip_do(int index);
extern void equip_reset(void);
extern int equip_visible_count(void);
extern int equip_visible_index(int i);
extern void equip_row_text(int index, char* buf, int buflen);
extern int equip_buyable(int index);

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
        addCycle("Ship Shading", &scene_shading, {"Flat", "Lit"});
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
  // Mirrors options.cpp's option_list (Game Settings + Quit); the old Save/Load
  // Commander entries were removed along with that functionality.
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

  // ----- Market prices ------------------------------------------------------

  // Per-row Buy/Sell action: trades one unit of its commodity through the render-free
  // market API. The window refreshes all rows from live state each frame, so this
  // works for both local and thin-client (server-authoritative) trading.
  class TradeButton : public GuiButton
  {
    public:
      TradeButton(int _item, bool _buy)
        : m_item(_item), m_buy(_buy)
      {
        m_centered = true;
      }

      void MouseUp() override
      {
        if (m_buy)
          market_buy(m_item);
        else
          market_sell(m_item);
      }

    private:
      int m_item;
      bool m_buy;
  };

  // The market screen: a row per commodity (product / unit / price / for-sale /
  // in-hold) with inline Buy and Sell buttons, plus a live cash readout. Replaces the
  // legacy gfx_display_* display_market_prices.
  class MarketWindow : public GuiWindow
  {
    public:
      MarketWindow()
        : GuiWindow("Market")
      {
        SetTitle("Market Prices");
        Centre(this, 560, 360);
      }

      void Create() override
      {
        GuiWindow::Create();
        m_buttonOrder.clear();
        m_rows.clear();

        const int infoX = 10;
        const int infoW = 360;
        const int buyX = infoX + infoW + 6;   // 376
        const int sellX = buyX + 86;           // 462
        const int actW = 80;
        const int rowH = 16;

        char hdr[128];
        snprintf(hdr, sizeof(hdr), "%-15s %-2s %7s %6s %6s", "PRODUCT", "U", "PRICE", "SALE", "HOLD");
        auto* header = new LabelButton();
        header->SetProperties("MktHeader", infoX, 28, infoW, 14, hdr);
        RegisterButton(header);

        const int count = market_item_count();
        int y = 44;
        for (int i = 0; i < count; ++i)
        {
          auto* info = new LabelButton();
          info->SetProperties("MktRow" + std::to_string(i), infoX, y, infoW, 14, "");
          RegisterButton(info);
          m_rows.push_back(info);

          auto* buy = new TradeButton(i, true);
          buy->SetProperties("Buy" + std::to_string(i), buyX, y, actW, 14, "Buy");
          RegisterButton(buy);
          m_buttonOrder.push_back(buy);

          auto* sell = new TradeButton(i, false);
          sell->SetProperties("Sell" + std::to_string(i), sellX, y, actW, 14, "Sell");
          RegisterButton(sell);
          m_buttonOrder.push_back(sell);

          y += rowH;
        }

        y += 6;
        m_cash = new LabelButton();
        m_cash->SetProperties("MktCash", infoX, y, infoW, 14, "");
        RegisterButton(m_cash);

        auto* close = new CloseButton();
        close->m_centered = true;
        close->SetProperties("Close", buyX, y, actW + 6 + actW, 16, "Close");
        RegisterButton(close);
        m_buttonOrder.push_back(close);

        m_currentButton = 0;
        Refresh();
      }

      void Update() override
      {
        GuiWindow::Update();
        Refresh();
      }

    private:
      void Refresh()
      {
        char buf[128];
        for (size_t i = 0; i < m_rows.size(); ++i)
        {
          market_format_row(static_cast<int>(i), buf, sizeof(buf));
          m_rows[i]->SetCaption(buf);
        }
        if (m_cash)
        {
          const int credits = market_credits();
          snprintf(buf, sizeof(buf), "Cash: %d.%d Cr", credits / 10, credits % 10);
          m_cash->SetCaption(buf);
        }
      }

      std::vector<LabelButton*> m_rows;
      LabelButton* m_cash = nullptr;
  };

  // ----- Generic read-only info window --------------------------------------

  // A scrollless panel of read-only text lines + a Close button, driven by a
  // game-supplied line source (count + per-line getter, both render-free). Used for
  // Commander Status / Inventory / Planet Data; the lines are rebuilt from live game
  // state each frame so values stay current.
  class InfoWindow : public GuiWindow
  {
    public:
      using CountFn = int (*)();
      using LineFn = void (*)(int, char*, int);
      using TitleFn = void (*)(char*, int);

      InfoWindow(std::string_view _name, const char* _staticTitle, TitleFn _titleFn, CountFn _count, LineFn _line, int _w,
                 int _h)
        : GuiWindow(_name), m_count(_count), m_line(_line)
      {
        if (_titleFn)
        {
          char title[64];
          _titleFn(title, sizeof(title));
          SetTitle(title);
        }
        else
        {
          SetTitle(_staticTitle);
        }
        Centre(this, _w, _h);
      }

      void Create() override
      {
        GuiWindow::Create();
        m_buttonOrder.clear();
        m_labels.clear();

        const int x = 10;
        const int w = static_cast<int>(m_w) - 20;
        const int n = m_count ? m_count() : 0;
        int y = 28;
        for (int i = 0; i < n; ++i)
        {
          auto* label = new LabelButton();
          label->SetProperties("Info" + std::to_string(i), x, y, w, 14, "");
          RegisterButton(label);
          m_labels.push_back(label);
          y += 15;
        }

        y += 6;
        auto* close = new CloseButton();
        close->m_centered = true;
        close->SetProperties("Close", x, y, w, 16, "Close");
        RegisterButton(close);
        m_buttonOrder.push_back(close);

        m_currentButton = 0;
        Refresh();
      }

      void Update() override
      {
        GuiWindow::Update();
        Refresh();
      }

    private:
      void Refresh()
      {
        if (m_count)
          m_count(); // rebuild the source cache so values stay live
        char buf[160];
        for (size_t i = 0; i < m_labels.size(); ++i)
        {
          if (m_line)
            m_line(static_cast<int>(i), buf, sizeof(buf));
          else
            buf[0] = '\0';
          m_labels[i]->SetCaption(buf);
        }
      }

      CountFn m_count;
      LineFn m_line;
      std::vector<LabelButton*> m_labels;
  };

  // ----- Equip Ship ---------------------------------------------------------

  // A row in the equip list: clicking buys the item, or expands a laser sub-menu
  // (handled render-free by equip_do). The window rebuilds its rows when the visible
  // set changes (i.e. after a sub-menu expand).
  class EquipButton : public GuiButton
  {
    public:
      explicit EquipButton(int _index)
        : m_index(_index)
      {
      }
      void MouseUp() override { equip_do(m_index); }

    private:
      int m_index;
  };

  // Equip Ship: the dynamic buy-list (tech-level filtered, with laser sub-menus). Rows
  // are rebuilt only when the visible set changes; otherwise captions/enabled state
  // refresh from live state each frame.
  class EquipWindow : public GuiWindow
  {
    public:
      EquipWindow()
        : GuiWindow("Equip")
      {
        SetTitle("Equip Ship");
        Centre(this, 360, 460);
        equip_reset(); // start at the top-level list
      }

      void Create() override
      {
        GuiWindow::Create();
        BuildContents();
      }

      void Update() override
      {
        // Rebuild at frame start (before clicks are processed) when the visible set
        // changed last frame, so buttons are never deleted mid-click. Canvas tracks
        // the clicked button by name, so recreating rows across frames is safe.
        if (VisibleSetChanged())
        {
          Remove();
          GuiWindow::Create();
          BuildContents();
        }
        GuiWindow::Update();
        RefreshCaptions();
      }

    private:
      void BuildContents()
      {
        m_buttonOrder.clear();
        m_rows.clear();
        m_shownIndices.clear();

        const int x = 10;
        const int w = static_cast<int>(m_w) - 20;
        const int count = equip_visible_count();
        int y = 28;
        for (int i = 0; i < count; ++i)
        {
          const int idx = equip_visible_index(i);
          m_shownIndices.push_back(idx);

          auto* row = new EquipButton(idx);
          row->SetProperties("Eq" + std::to_string(idx), x, y, w, 14, "");
          RegisterButton(row);
          m_rows.push_back(row);
          m_buttonOrder.push_back(row);
          y += 16;
        }

        y += 6;
        m_cash = new LabelButton();
        m_cash->SetProperties("Cash", x, y, w, 14, "");
        RegisterButton(m_cash);

        auto* close = new CloseButton();
        close->m_centered = true;
        close->SetProperties("CloseBtn", x, y + 18, w, 16, "Close");
        RegisterButton(close);
        m_buttonOrder.push_back(close);

        m_currentButton = 0;
        RefreshCaptions();
      }

      bool VisibleSetChanged()
      {
        const int count = equip_visible_count();
        if (count != static_cast<int>(m_shownIndices.size()))
          return true;
        for (int i = 0; i < count; ++i)
          if (equip_visible_index(i) != m_shownIndices[i])
            return true;
        return false;
      }

      void RefreshCaptions()
      {
        char buf[80];
        for (size_t i = 0; i < m_rows.size(); ++i)
        {
          const int idx = m_shownIndices[i];
          equip_row_text(idx, buf, sizeof(buf));
          m_rows[i]->SetCaption(buf);
          m_rows[i]->SetDisabled(!equip_buyable(idx));
        }
        if (m_cash)
        {
          const int credits = market_credits();
          snprintf(buf, sizeof(buf), "Cash: %d.%d Cr", credits / 10, credits % 10);
          m_cash->SetCaption(buf);
        }
      }

      std::vector<EquipButton*> m_rows;
      std::vector<int> m_shownIndices;
      LabelButton* m_cash = nullptr;
  };
}

void RegisterGameWindows()
{
  GuiOverlay::SetOptionsWindowFactory([]() -> GuiWindow* { return new OptionsMenuWindow(); });
}

void OpenMarketWindow()
{
  GuiOverlay::ShowWindow(std::string_view("Market"), []() -> GuiWindow* { return new MarketWindow(); });
}

void OpenCommanderWindow()
{
  GuiOverlay::ShowWindow(std::string_view("Commander"), []() -> GuiWindow* {
    return new InfoWindow("Commander", "Commander", cmdr_status_title, cmdr_status_line_count, cmdr_status_line, 380, 420);
  });
}

void OpenInventoryWindow()
{
  GuiOverlay::ShowWindow(std::string_view("Inventory"), []() -> GuiWindow* {
    return new InfoWindow("Inventory", "Inventory", nullptr, inventory_line_count, inventory_line, 360, 380);
  });
}

void OpenPlanetDataWindow()
{
  GuiOverlay::ShowWindow(std::string_view("PlanetData"), []() -> GuiWindow* {
    return new InfoWindow("PlanetData", "Planet Data", planet_data_title, planet_data_line_count, planet_data_line, 440, 440);
  });
}

void OpenEquipWindow()
{
  GuiOverlay::ShowWindow(std::string_view("Equip"), []() -> GuiWindow* { return new EquipWindow(); });
}
