#pragma once

// Render-free galactic-chart data API (implemented in docked.cpp).
//
// The native ChartWindow (GameWindows.cpp) draws the galactic / short-range charts
// through the GUI overlay's Render2D pass, so - like the other migrated screens - it
// must stay off the legacy game headers (gfx.h/elite.h define macros that don't mix
// with the winrt/GUI headers). This header exposes just the data + selection + teleport
// the window needs.
//
// All coordinates below live in a fixed "chart-canvas" pixel space (PLOT_W x PLOT_H).
// The window fits that space uniformly (single scale, centred) into its own content
// rect, so the projection math and the crosshair stay resolution-independent and circles
// stay circular. The window maps window-local pixels back through the same transform for
// hit-testing.

namespace ChartData
{
  // Chart zoom presets (was SCR_GALACTIC_CHART / SCR_SHORT_RANGE).
  enum Kind
  {
    GALACTIC = 0,
    SHORT_RANGE = 1
  };

  // The fixed chart-canvas the coordinates below are authored in (the legacy 512x384
  // play-area optics; the window scales it to fit). The chart centre is PLOT_W/2, PLOT_H/2,
  // and SCALE is the retro focal "2x" baked into the blob sizes and fuel-ring radius.
  // (These replace the old GFX_X_CENTRE / GFX_Y_CENTRE / GFX_SCALE gfx.h macros.)
  inline constexpr int PLOT_W = 512;
  inline constexpr int PLOT_H = 384;
  inline constexpr int SCALE = 2;

  bool Ready();               // is the replicated galaxy manifest present?
  int  Count();               // number of systems in the manifest

  // Project every system for `kind` into the internal per-frame cache. Call once per
  // frame before X()/Y()/Visible()/Name()/Blob().
  void Begin(int kind);
  int  X(int i);              // chart-canvas px (valid after Begin)
  int  Y(int i);
  bool Visible(int i);        // is the dot inside the plottable area?
  void Name(int i, char* buf, int buflen);   // capitalised system name
  int  Blob(int i);           // short-range blob radius in chart px (>= 1)

  int  CurrentIndex();        // system the ship is in, or -1
  int  SelectedIndex();       // last-picked system, or -1

  // Short-range fuel-range ring (chart-canvas px). Returns false for the galactic
  // chart (which draws no fuel ring).
  bool FuelCircle(int kind, int* cx, int* cy, int* r);

  void GetCursor(int* cx, int* cy);          // crosshair position (chart-canvas px)
  void SetCursor(int kind, int cx, int cy);  // clamp to the plot area + re-pick the nearest system

  void Jump(int kind);        // request a hyperspace jump to the system nearest the crosshair

  // Selected-system data panel (replicated manifest data: economy / government / tech /
  // population / productivity). Empty when nothing is selected.
  int  DataLineCount();
  void DataLine(int i, char* buf, int buflen);
}
