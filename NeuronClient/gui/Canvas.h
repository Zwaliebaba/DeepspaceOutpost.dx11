#pragma once

#include "GuiButton.h"
#include "GuiWindow.h"
#include <string>
#include <string_view>
#include <vector>

class Canvas
{
public:

  static void Startup();
  static void Shutdown();

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
