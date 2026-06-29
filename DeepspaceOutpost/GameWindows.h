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

// Open the market-prices window on the GUI overlay (the F8 in-game entry routes here
// instead of the legacy gfx_display_* display_market_prices screen).
void OpenMarketWindow();
