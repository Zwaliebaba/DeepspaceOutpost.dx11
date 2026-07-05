#pragma once

// GameWindows - this game's GUI windows, built on the engine's GuiWindow/Canvas stack.
//
// These are the game-specific screens being migrated off the legacy gfx_display_*
// path: an Options menu, the Game Settings panel (value rows wired to the config
// globals + Save), and a Quit confirmation. GuiWindow/GuiButton live in the engine
// (NeuronClient) and know nothing about game state, so the game-specific windows live
// here and are handed to the F1/F11 overlay via GuiOverlay::SetOptionsWindowFactory.

// Register the game's GUI windows with the engine overlay. Call once from
// GameApp::Startup() (after the engine/Canvas are up).
void RegisterGameWindows();

// Open game screens on the GUI overlay, replacing their legacy gfx_display_* versions.
// The in-game F-key handlers route here (market F8, commander F9, inventory F10,
// planet data F7).
void OpenMarketWindow();
void OpenCommanderWindow();
void OpenInventoryWindow();
void OpenPlanetDataWindow();
void OpenEquipWindow();

// Open (or focus) the native galactic / short-range chart window. `kind` is a
// ChartData::Kind (ChartData.h): GALACTIC or SHORT_RANGE. Routed from F5/F6.
void OpenChartWindow(int kind);

// Open (or focus) the docked station menu (Launch + the native screens). The docked
// view is the camera-space 3D scene with this small hub window floating over it.
void OpenStationMenu();

// Close the docked station menu. Called when leaving the station (launch / hyperspace
// arrival), so the hub doesn't linger over the flight view.
void CloseStationMenu();
