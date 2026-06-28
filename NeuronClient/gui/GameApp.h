#pragma once

// Minimal app shim for the imported GUI (Phase 4).
//
// The donor GUI referenced a large GameApp singleton; the imported widgets only
// touch g_app->m_requestQuit (GameExitButton). This shim provides just that. Wire
// m_requestQuit into the game's main loop when the GUI is activated (Phase 5).

struct GameApp
{
  bool m_requestQuit = false;
};

inline GameApp g_appInstance;
inline GameApp* g_app = &g_appInstance;
