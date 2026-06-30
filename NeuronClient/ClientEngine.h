#pragma once

// ClientEngine - the engine's front door (window + device + GameMain dispatch).
//
// Native, trimmed adaptation of the donor Neuron::Client::ClientEngine: it owns the
// Win32 window and brings up the native Direct3D 11 device (Neuron::Graphics::Core),
// the immediate renderer, the GUI canvas, fonts and strings. It does NOT pull in the
// donor's legacy D3D9 path (Direct3DInit), SystemInfo or Audio.
//
// Ownership: the engine creates the window + Core device/swap chain up front; the
// legacy platform layer's gfx_graphics_startup() then ADOPTS them into the Renderer
// (canvas + present) rather than creating a second device. The classic game loop in
// game_main() runs as before on top.

#include <windows.h>

#include "GameMain.h"
#include "GraphicsCore.h"

namespace Neuron::Client
{
  class ClientEngine
  {
    public:
      static void Startup(const wchar_t* _gameName, HINSTANCE _hInstance, int _nCmdShow);
      static void StartGame(const winrt::com_ptr<GameMain>& _gameMain);
      static void Shutdown();

      // Drive one frame's render through the GameMain lifecycle: Update (logic),
      // RenderScene (3D/HUD into the 2D batch), flush the batch to the back buffer,
      // RenderCanvas (2D UI on top), then present. The classic game_main() loop calls
      // this once per iteration via gfx_update_screen() for now; Update/RenderScene are
      // stubs until the game's per-frame work migrates onto those hooks. No-op without a
      // started game.
      static void Tick();

      // Handle a client-area resize (from the window procedure's WM_SIZE): rebuild the
      // Core swap chain + the Renderer back buffer and notify the game. No-op until the
      // window/device are fully up.
      static void OnResize(int _width, int _height);

      static HINSTANCE Instance() { return m_instance; }
      static HWND Window() { return m_hwnd; }
      static Windows::Foundation::Size OutputSize() { return Graphics::Core::GetOutputSize(); }

    protected:
      inline static HINSTANCE m_instance{};
      inline static HWND m_hwnd{};
      inline static winrt::com_ptr<GameMain> m_main;
      inline static bool m_windowReady{}; // device + swap chain created; safe to resize
  };
}

// The donor exposed ClientEngine unqualified at namespace scope; keep that so call
// sites (WinMain, GUI) can write ClientEngine::Window() etc.
using namespace Neuron::Client;
