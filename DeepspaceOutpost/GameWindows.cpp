#include "pch.h"
#include "GameWindows.h"

#include "GuiWindow.h"
#include "GuiButton.h"
#include "Canvas.h"
#include "GuiOverlay.h"
#include "GraphicsCore.h"
#include "Render2D.h"       // native 2D primitives for the chart map
#include "TextRenderer.h"   // g_gameFont for chart labels / data panel
#include "input_win.h"      // input_mouse_state for chart click hit-testing
#include "ChartData.h"      // render-free galactic-chart data source

#include <cstdio>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

// Game config globals (declared in elite.h), re-declared here so this
// winrt/widget-based translation unit stays free of the legacy game headers
// (which define macros that don't mix with the GUI headers).
extern int anti_alias_gfx;
extern int scene_shading;
extern int hoopy_casinos;
extern int instant_dock;

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

// Launch from the station back into flight (space.h / space.cpp), declared here to keep
// this TU off the legacy game headers. Bound to the station menu's Launch button.
extern void launch_player(void);

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
          auto* button = NEW CycleButton(label, value, std::move(options));
          button->SetProperties(label, x, y, w, btnH, label);
          button->Refresh();
          RegisterButton(button);
          m_buttonOrder.push_back(button);
          y += rowH;
        };

        // In-session settings (name + value labels) mapped to their globals. The
        // MMO client keeps no local config file, so there is nothing to persist -
        // these toggles apply for the session only.
        addCycle("Anti Alias", &anti_alias_gfx, {"Off", "On"});
        addCycle("Ship Shading", &scene_shading, {"Flat", "Lit"});
        addCycle("Planet Desc.", &hoopy_casinos, {"BBC", "MSX"});
        addCycle("Instant Dock", &instant_dock, {"Off", "On"});

        y += 4;
        auto* close = NEW CloseButton();
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

        auto* label = NEW LabelButton();
        label->SetProperties("QuitPrompt", 10, 30, 220, 15, "Quit game?");
        RegisterButton(label);

        auto* yes = NEW GameExitButton();
        yes->m_centered = true;
        yes->SetProperties("Yes", 10, 60, 105, 18, "Yes");
        RegisterButton(yes);
        m_buttonOrder.push_back(yes);

        auto* no = NEW CloseButton();
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
      Canvas::EclRegisterWindow(NEW T());
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

        auto* settings = NEW OpenSettingsButton();
        settings->m_centered = true;
        settings->SetProperties("GameSettings", x, y, w, 18, "Game Settings");
        RegisterButton(settings);
        m_buttonOrder.push_back(settings);
        y += 26;

        auto* quit = NEW OpenQuitButton();
        quit->m_centered = true;
        quit->SetProperties("Quit", x, y, w, 18, "Quit");
        RegisterButton(quit);
        m_buttonOrder.push_back(quit);
        y += 26;

        auto* close = NEW CloseButton();
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
        Centre(this, 560, 480);   // I5: taller for the 22px touch rows (auto-grows to fit)
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
        // I5 ergonomics: taller rows / action buttons for touch spacing. The window
        // auto-grows to fit the buttons (RegisterButton), so the extra height is
        // absorbed; the Centre() below seeds a matching size.
        const int rowH = 22;
        const int rowBtnH = 20;

        char hdr[128];
        snprintf(hdr, sizeof(hdr), "%-15s %-2s %7s %6s %6s", "PRODUCT", "U", "PRICE", "SALE", "HOLD");
        auto* header = NEW LabelButton();
        header->SetProperties("MktHeader", infoX, 28, infoW, 14, hdr);
        RegisterButton(header);

        const int count = market_item_count();
        int y = 44;
        for (int i = 0; i < count; ++i)
        {
          auto* info = NEW LabelButton();
          info->SetProperties("MktRow" + std::to_string(i), infoX, y, infoW, rowBtnH, "");
          RegisterButton(info);
          m_rows.push_back(info);

          auto* buy = NEW TradeButton(i, true);
          buy->SetProperties("Buy" + std::to_string(i), buyX, y, actW, rowBtnH, "Buy");
          RegisterButton(buy);
          m_buttonOrder.push_back(buy);

          auto* sell = NEW TradeButton(i, false);
          sell->SetProperties("Sell" + std::to_string(i), sellX, y, actW, rowBtnH, "Sell");
          RegisterButton(sell);
          m_buttonOrder.push_back(sell);

          y += rowH;
        }

        y += 6;
        m_cash = NEW LabelButton();
        m_cash->SetProperties("MktCash", infoX, y, infoW, 14, "");
        RegisterButton(m_cash);

        auto* close = NEW CloseButton();
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
          auto* label = NEW LabelButton();
          label->SetProperties("Info" + std::to_string(i), x, y, w, 14, "");
          RegisterButton(label);
          m_labels.push_back(label);
          y += 15;
        }

        y += 6;
        auto* close = NEW CloseButton();
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

  // A row in the equip list: clicking buys the item (handled render-free by
  // equip_do). The window rebuilds its rows when the visible set changes.
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

  // Equip Ship: the dynamic buy-list (tech-level filtered). Rows are rebuilt only
  // when the visible set changes; otherwise captions/enabled state refresh from
  // live state each frame.
  class EquipWindow : public GuiWindow
  {
    public:
      EquipWindow()
        : GuiWindow("Equip")
      {
        SetTitle("Equip Ship");
        Centre(this, 360, 460);
        equip_reset();
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

          auto* row = NEW EquipButton(idx);
          row->SetProperties("Eq" + std::to_string(idx), x, y, w, 20, "");   // I5: taller touch row
          RegisterButton(row);
          m_rows.push_back(row);
          m_buttonOrder.push_back(row);
          y += 24;   // I5 ergonomics: 24px row pitch for touch spacing
        }

        y += 6;
        m_cash = NEW LabelButton();
        m_cash->SetProperties("Cash", x, y, w, 14, "");
        RegisterButton(m_cash);

        auto* close = NEW CloseButton();
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

  // ----- Charts (galactic / short-range) ------------------------------------

  // The galactic + short-range charts as one native window. Unlike the text screens
  // above, the chart body is custom vector graphics (system dots, the fuel ring, the
  // crosshair), so this overrides Render() and draws through Render2D directly, and
  // overrides MouseEvent() so a click on the map selects the nearest system. All the
  // galaxy data comes from the render-free ChartData API (no legacy gfx_* / game
  // headers). Coordinates from ChartData live in a fixed PLOT_W x PLOT_H chart-canvas
  // that MapTransform() fits uniformly into the window's map area.
  class ChartWindow;

  class ChartActionButton : public GuiButton
  {
    public:
      enum Action { Jump, Toggle };
      ChartActionButton(ChartWindow* _win, Action _action)
        : m_win(_win), m_action(_action)
      {
        m_centered = true;
      }
      void MouseUp() override;   // defined out-of-line once ChartWindow is complete

    private:
      ChartWindow* m_win;
      Action m_action;
  };

  class ChartWindow : public GuiWindow
  {
    public:
      explicit ChartWindow(int _kind)
        : GuiWindow("Chart"), m_kind(_kind)
      {
        SetTitle(_kind == ChartData::SHORT_RANGE ? "Short Range Chart" : "Galactic Chart");
        Centre(this, 680, 480);
        SetMovable(false);   // the whole body is a click-to-select surface, so don't drag it
      }

      void Create() override
      {
        GuiWindow::Create();   // iconised close button (top-right)
        m_buttonOrder.clear();

        const int bw = 130, bh = 20;
        const int by = static_cast<int>(m_h) - 32;

        auto* hyp = NEW ChartActionButton(this, ChartActionButton::Jump);
        hyp->SetProperties("Hyperspace", 12, by, bw, bh, "HYPERSPACE");
        RegisterButton(hyp);
        m_buttonOrder.push_back(hyp);

        auto* toggle = NEW ChartActionButton(this, ChartActionButton::Toggle);
        toggle->SetProperties("ChartMode", 12 + bw + 8, by, bw, bh, ToggleCaption());
        RegisterButton(toggle);
        m_buttonOrder.push_back(toggle);

        m_currentButton = 0;
        ParkCursorOnCurrent();
      }

      void SetKind(int _kind)
      {
        m_kind = _kind;
        SetTitle(_kind == ChartData::SHORT_RANGE ? "Short Range Chart" : "Galactic Chart");
        if (GuiButton* b = GetButton("ChartMode"))
          b->SetCaption(ToggleCaption());
        ParkCursorOnCurrent();
      }

      void ToggleKind()
      {
        SetKind(m_kind == ChartData::SHORT_RANGE ? ChartData::GALACTIC : ChartData::SHORT_RANGE);
      }

      void DoJump()
      {
        ChartData::Jump(m_kind);
        Canvas::EclRemoveWindow(m_name);   // let the break-pattern jump animation show
      }

      void MouseEvent(bool /*lmb*/, bool /*rmb*/, bool up, bool /*down*/) override
      {
        if (!up)
          return;   // act on the click release, like the old chart pointer

        int mx = 0, my = 0;
        bool l = false, r = false;
        input_mouse_state(mx, my, l, r);

        const Xform x = MapTransform();
        if (mx < x.l || mx > x.r || my < x.t || my > x.b)
          return;   // outside the map area (e.g. the data panel) - ignore

        const int cx = static_cast<int>((mx - x.ox) / x.scale);
        const int cy = static_cast<int>((my - x.oy) / x.scale);
        ChartData::SetCursor(m_kind, cx, cy);
      }

      void Render(bool hasFocus) override
      {
        GuiWindow::Render(hasFocus);   // panel frame, title, buttons

        using R = Neuron::Graphics::Render2D;
        const Xform x = MapTransform();

        // Map backdrop + border.
        R::FillRect(x.l, x.t, x.r, x.b, R::Rgba(6, 8, 16, 235));
        const auto edge = R::Rgba(60, 80, 110, 255);
        R::DrawLine(x.l, x.t, x.r, x.t, edge);
        R::DrawLine(x.l, x.b, x.r, x.b, edge);
        R::DrawLine(x.l, x.t, x.l, x.b, edge);
        R::DrawLine(x.r, x.t, x.r, x.b, edge);

        if (!ChartData::Ready())
        {
          g_gameFont.SetColor(230, 180, 40, 255);
          g_gameFont.DrawText2DCenter((x.l + x.r) / 2, (x.t + x.b) / 2, 14, "GALAXY DATA UNAVAILABLE");
          return;
        }

        ChartData::Begin(m_kind);
        const int n = ChartData::Count();
        const int cur = ChartData::CurrentIndex();
        const int sel = ChartData::SelectedIndex();

        // Clip the plotted dots / labels to the map area so they can't spill into the
        // data panel or over the window frame.
        R::SetClip(static_cast<int>(x.l), static_cast<int>(x.t), static_cast<int>(x.r - x.l),
                   static_cast<int>(x.b - x.t));

        // Fuel-range ring (short-range only) + its centre cross.
        int fcx = 0, fcy = 0, fr = 0;
        if (ChartData::FuelCircle(m_kind, &fcx, &fcy, &fr) && fr > 0)
        {
          const auto green = R::Rgba(64, 200, 64, 255);
          const float gx = x.ox + fcx * x.scale, gy = x.oy + fcy * x.scale, gc = 7.0f * x.scale;
          R::DrawCircle(gx, gy, fr * x.scale, green);
          R::DrawLine(gx, gy - gc, gx, gy + gc, green);
          R::DrawLine(gx - gc, gy, gx + gc, gy, green);
        }

        // System dots: short-range draws sized gold blobs, galactic small white dots.
        for (int i = 0; i < n; ++i)
        {
          if (!ChartData::Visible(i))
            continue;
          const float sx = x.ox + ChartData::X(i) * x.scale;
          const float sy = x.oy + ChartData::Y(i) * x.scale;
          if (m_kind == ChartData::SHORT_RANGE)
          {
            float br = ChartData::Blob(i) * x.scale;
            if (br < 1.5f)
              br = 1.5f;
            R::FillCircle(sx, sy, br, R::Rgba(230, 180, 40, 255));
          }
          else
          {
            R::FillRect(sx - 1.0f, sy - 1.0f, sx + 1.5f, sy + 1.5f, R::Rgba(235, 235, 235, 255));
          }
        }

        // Ring the current system (cyan) and the selected system (red).
        if (cur >= 0 && ChartData::Visible(cur))
          R::DrawCircle(x.ox + ChartData::X(cur) * x.scale, x.oy + ChartData::Y(cur) * x.scale, 6.0f,
                        R::Rgba(80, 220, 255, 255));
        if (sel >= 0 && ChartData::Visible(sel))
          R::DrawCircle(x.ox + ChartData::X(sel) * x.scale, x.oy + ChartData::Y(sel) * x.scale, 8.0f,
                        R::Rgba(255, 90, 90, 255));

        // Labels for the current + selected systems (drawing every system would clutter
        // at arbitrary window sizes).
        char nm[32];
        if (cur >= 0 && ChartData::Visible(cur))
        {
          ChartData::Name(cur, nm, sizeof(nm));
          g_gameFont.SetColor(200, 220, 255, 255);
          g_gameFont.DrawText2D(x.ox + ChartData::X(cur) * x.scale + 6, x.oy + ChartData::Y(cur) * x.scale - 6, 11, nm);
        }
        if (sel >= 0 && sel != cur && ChartData::Visible(sel))
        {
          ChartData::Name(sel, nm, sizeof(nm));
          g_gameFont.SetColor(255, 150, 150, 255);
          g_gameFont.DrawText2D(x.ox + ChartData::X(sel) * x.scale + 6, x.oy + ChartData::Y(sel) * x.scale - 6, 11, nm);
        }

        // Crosshair at the cursor.
        int hcx = 0, hcy = 0;
        ChartData::GetCursor(&hcx, &hcy);
        const float hx = x.ox + hcx * x.scale, hy = x.oy + hcy * x.scale;
        const auto cross = R::Rgba(255, 80, 80, 255);
        R::DrawLine(hx - 8, hy, hx - 2, hy, cross);
        R::DrawLine(hx + 2, hy, hx + 8, hy, cross);
        R::DrawLine(hx, hy - 8, hx, hy - 2, cross);
        R::DrawLine(hx, hy + 2, hx, hy + 8, cross);

        R::ClearClip();

        // Selected-system data panel (right column, outside the map clip).
        const float px = x.r + 12.0f;
        float py = m_y + 26.0f;
        ChartData::Name(sel >= 0 ? sel : cur, nm, sizeof(nm));
        g_gameFont.SetColor(255, 209, 64, 255);
        g_gameFont.DrawText2D(px, py, 13, nm[0] ? nm : "---");
        py += 22;
        g_gameFont.SetColor(210, 210, 210, 255);
        const int lines = ChartData::DataLineCount();
        char line[80];
        for (int i = 0; i < lines; ++i)
        {
          ChartData::DataLine(i, line, sizeof(line));
          g_gameFont.DrawText2D(px, py, 11, line);
          py += 16;
        }

        // A short hint under the data panel.
        g_gameFont.SetColor(150, 160, 180, 255);
        g_gameFont.DrawText2D(px, m_y + m_h - 54, 10, "Click a system to select");
        g_gameFont.DrawText2D(px, m_y + m_h - 40, 10, "HYPERSPACE to jump");
      }

    private:
      // Uniform fit of the fixed PLOT_W x PLOT_H chart-canvas into the window's map area
      // (the client rect minus the right data-panel column and the button strip).
      struct Xform
      {
        float ox, oy, scale;   // chart px -> screen: screen = o + chartPx * scale
        float l, t, r, b;      // the map area in absolute window pixels
      };

      Xform MapTransform() const
      {
        const float panelW = 210.0f;
        float l = m_x + 8.0f;
        float t = m_y + 22.0f;
        float rr = m_x + m_w - panelW - 8.0f;
        float b = m_y + m_h - 40.0f;
        if (rr < l + 40.0f)
          rr = l + 40.0f;
        if (b < t + 40.0f)
          b = t + 40.0f;
        const float sx = (rr - l) / static_cast<float>(ChartData::PLOT_W);
        const float sy = (b - t) / static_cast<float>(ChartData::PLOT_H);
        const float s = sx < sy ? sx : sy;
        Xform x;
        x.scale = s;
        x.ox = l + ((rr - l) - ChartData::PLOT_W * s) * 0.5f;
        x.oy = t + ((b - t) - ChartData::PLOT_H * s) * 0.5f;
        x.l = l;
        x.t = t;
        x.r = rr;
        x.b = b;
        return x;
      }

      const char* ToggleCaption() const
      {
        return m_kind == ChartData::SHORT_RANGE ? "Galactic Chart" : "Short Range";
      }

      void ParkCursorOnCurrent()
      {
        ChartData::Begin(m_kind);
        const int cur = ChartData::CurrentIndex();
        if (cur >= 0 && ChartData::Visible(cur))
          ChartData::SetCursor(m_kind, ChartData::X(cur), ChartData::Y(cur));
        else
          ChartData::SetCursor(m_kind, ChartData::PLOT_W / 2, ChartData::PLOT_H / 2);
      }

      int m_kind;
  };

  void ChartActionButton::MouseUp()
  {
    if (!m_win)
      return;
    if (m_action == Jump)
      m_win->DoJump();
    else
      m_win->ToggleKind();
  }

  // ----- Station menu (the docked home) -------------------------------------

  // A button that fires a plain void() action (launch, or one of the Open*Window
  // entry points). The docked view is the camera-space 3D scene; this small menu window
  // floats over it as the station hub, replacing the legacy 512x514 commander-status
  // screen (the first screen off the letterbox on the docked side).
  class MenuActionButton : public GuiButton
  {
    public:
      explicit MenuActionButton(void (*_fn)())
        : m_fn(_fn)
      {
        m_centered = true;
      }
      void MouseUp() override
      {
        if (m_fn)
          m_fn();
      }

    private:
      void (*m_fn)();
  };

  class StationMenuWindow : public GuiWindow
  {
    public:
      StationMenuWindow()
        : GuiWindow("Station")
      {
        SetTitle("Station");
        Centre(this, 200, 250);
      }

      void Create() override
      {
        GuiWindow::Create();
        m_buttonOrder.clear();

        const int x = 12;
        const int w = static_cast<int>(m_w) - 24;
        const int h = 22;
        int y = 30;

        auto add = [&](const char* _name, const char* _caption, void (*_fn)()) {
          auto* b = NEW MenuActionButton(_fn);
          b->SetProperties(_name, x, y, w, h, _caption);
          RegisterButton(b);
          m_buttonOrder.push_back(b);
          y += 28;
        };

        // Launch leaves the station (launch_player closes this hub via CloseStationMenu);
        // the rest open the existing native screens.
        add("Launch", "Launch", launch_player);
        add("Market", "Market", OpenMarketWindow);
        add("Equip", "Equip Ship", OpenEquipWindow);
        add("Commander", "Commander", OpenCommanderWindow);
        add("Inventory", "Inventory", OpenInventoryWindow);
        add("Options", "Options", []() { GuiOverlay::Open(); });

        m_currentButton = 0;
      }
  };
}

void RegisterGameWindows()
{
  GuiOverlay::SetOptionsWindowFactory([]() -> GuiWindow* { return NEW OptionsMenuWindow(); });
}

void OpenMarketWindow()
{
  GuiOverlay::ShowWindow(std::string_view("Market"), []() -> GuiWindow* { return NEW MarketWindow(); });
}

void OpenCommanderWindow()
{
  GuiOverlay::ShowWindow(std::string_view("Commander"), []() -> GuiWindow* {
    return NEW InfoWindow("Commander", "Commander", cmdr_status_title, cmdr_status_line_count, cmdr_status_line, 380, 420);
  });
}

void OpenInventoryWindow()
{
  GuiOverlay::ShowWindow(std::string_view("Inventory"), []() -> GuiWindow* {
    return NEW InfoWindow("Inventory", "Inventory", nullptr, inventory_line_count, inventory_line, 360, 380);
  });
}

void OpenPlanetDataWindow()
{
  GuiOverlay::ShowWindow(std::string_view("PlanetData"), []() -> GuiWindow* {
    return NEW InfoWindow("PlanetData", "Planet Data", planet_data_title, planet_data_line_count, planet_data_line, 440, 440);
  });
}

void OpenEquipWindow()
{
  GuiOverlay::ShowWindow(std::string_view("Equip"), []() -> GuiWindow* { return NEW EquipWindow(); });
}

void OpenChartWindow(int kind)
{
  // Open (or focus) the single chart window, then apply the requested zoom preset so
  // F5/F6 switch an already-open chart between galactic and short-range.
  GuiOverlay::ShowWindow(std::string_view("Chart"), [kind]() -> GuiWindow* { return NEW ChartWindow(kind); });
  if (GuiWindow* w = Canvas::EclGetWindow(std::string_view("Chart")))
    static_cast<ChartWindow*>(w)->SetKind(kind);
}

void OpenStationMenu()
{
  GuiOverlay::ShowWindow(std::string_view("Station"), []() -> GuiWindow* { return NEW StationMenuWindow(); });
}

void CloseStationMenu()
{
  Canvas::EclRemoveWindow(std::string_view("Station"));
}
