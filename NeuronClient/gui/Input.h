#pragma once

// Minimal menu-input shim for the imported GUI (Phase 4).
//
// The donor GUI drove menu navigation through a full InputManager / ControlBindings
// stack (keyboard+gamepad driver layer). That whole subsystem is not imported; the
// GUI only needs a handful of menu control events, so this shim maps them onto the
// target's keyboard state (keyboard.h). NOTE: these reflect held-key state, not
// edge-triggered presses - acceptable for bring-up; revisit if menus repeat too fast.

#include "keyboard.h"

enum GuiControl
{
  ControlMenuUp,
  ControlMenuDown,
  ControlMenuLeft,
  ControlMenuRight,
  ControlMenuActivate,
  ControlMenuClose,
};

class InputManager
{
  public:
    bool controlEvent(int _type) const
    {
      switch (_type)
      {
      case ControlMenuUp:       return kbd_up_pressed != 0;
      case ControlMenuDown:     return kbd_down_pressed != 0;
      case ControlMenuLeft:     return kbd_left_pressed != 0;
      case ControlMenuRight:    return kbd_right_pressed != 0;
      case ControlMenuActivate: return kbd_enter_pressed != 0;
      case ControlMenuClose:    return kbd_escape_pressed != 0;
      default:                  return false;
      }
    }
};

inline InputManager g_inputManagerInstance;
inline InputManager* g_inputManager = &g_inputManagerInstance;
