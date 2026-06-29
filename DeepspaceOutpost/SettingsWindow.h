#pragma once

// SettingsWindow - the game's real "Game Settings" panel as a GuiWindow.
//
// First screen migrated off the legacy gfx_display_* path (options.cpp's
// game_settings_screen) onto the imported GuiWindow/Canvas stack. Its rows are
// value-cycling buttons wired directly to the game's config globals (wireframe,
// anti_alias_gfx, ...) plus a Save button that calls write_config_file(). It is
// reached from the F1 overlay's main menu via the engine's options-window factory.
//
// Layering: GuiWindow/GuiButton live in the engine (NeuronClient) and know nothing
// about game state, so this game-specific window lives in the game target and is
// handed to the overlay through GuiOverlay::SetOptionsWindowFactory.

#include "GuiWindow.h"

class SettingsWindow : public GuiWindow
{
  public:
    SettingsWindow();
    void Create() override;
};

// Register the game's GUI windows with the engine overlay. Call once from
// GameApp::Startup() (after the engine/Canvas are up).
void RegisterGameWindows();
