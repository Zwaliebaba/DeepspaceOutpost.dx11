#pragma once

#include "GuiButton.h"
#include "GuiWindow.h"
#include <d3d11.h>
#include <string>
#include <string_view>
#include <vector>

class Canvas
{
public:

  static void Startup();
  static void Shutdown();

  // --- 2D pass bracket (Phase 2 render-loop refactor) -----------------------
  // Open / close the client's single 2D pass. Start opens a Render2D batch targeting the
  // window (same parameters as Render2D::Begin: a virtual authoring space placed on the
  // target at (dstX,dstY) scaled by dstScale); End flushes it. The game HUD batch and the
  // GUI windows draw between one Start/End so all 2D composites in submission order in a
  // single pass. (The 2D-pass owner now lives on Canvas, per the design decision.)
  static void Start(ID3D11RenderTargetView* rtv, int virtualW, int virtualH, int dstX = 0, int dstY = 0,
                    float dstScale = 1.0f, D3D11_FILTER filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR);
  static void End();

  static void EclUpdateMouse(int mouseX, int mouseY, bool lmb, bool rmb);
  static void EclUpdateKeyboard(int keyCode, bool shift, bool ctrl, bool alt);
  static void Render();
  static void EclUpdate();

  static void EclRegisterWindow(GuiWindow* window, const GuiWindow* parent = nullptr);
  static void EclRemoveWindow(std::string_view name);
  static void EclRegisterPopup(GuiWindow* window);
  static void EclRemovePopup();

  static void EclBringWindowToFront(std::string_view name);
  static void EclSetWindowPosition(std::string_view name, int x, int y);
  static void EclSetWindowSize(std::string_view name, int w, int h);

  static int EclGetWindowIndex(std::string_view name);
  static GuiWindow*EclGetWindow(std::string_view name);
  static GuiWindow*EclGetWindow(int x, int y);

  static bool EclMouseInWindow(const GuiWindow* window);
  static bool EclMouseInButton(const GuiWindow* window, const GuiButton* button);
  static bool EclIsTextEditing();

  static void EclRegisterTooltipCallback(void (*_callback)(GuiWindow*, GuiButton*));

  static void EclMaximizeWindow(std::string_view name);
  static void EclUnMaximize();

  static std::string EclGetCurrentButton();
  static std::string EclGetCurrentClickedButton();

  static std::string EclGetCurrentFocus();
  static void EclSetCurrentFocus(std::string_view name);

  static std::vector<GuiWindow*>*EclGetWindows();
};
