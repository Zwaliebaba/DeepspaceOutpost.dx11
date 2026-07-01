#pragma once

// GameMain - the application object the engine (ClientEngine) drives.
//
// Trimmed, native version of the donor GameMain: a WinRT implementation type (so it
// can be created with winrt::make_self and held as a com_ptr) exposing the per-frame
// and lifecycle hooks. The donor's ASyncLoader / IDeviceNotify bases and the
// coroutine Startup() are intentionally dropped; Startup() is plain void.

#include <winrt/base.h>

namespace Neuron
{
  class GameMain : public winrt::implements<GameMain, winrt::Windows::Foundation::IInspectable>
  {
    public:
      GameMain() = default;
      virtual ~GameMain() = default;

      // Called once after the engine/device are up.
      virtual void Startup() {}
      virtual void Shutdown() {}

      // Per-frame hooks (the legacy game still runs through game_main() for now, so
      // these are stubs a future migration fills in). RenderCanvas owns the whole 2D
      // phase (game HUD replay + GUI overlay) and returns whether the frame painted
      // anything - the engine presents only when it did (idle frames persist).
      virtual void Update(float _deltaSeconds) {}
      virtual void RenderScene() {}
      virtual bool RenderCanvas() { return false; }

      // Window/app lifecycle notifications from the engine's window procedure.
      virtual void OnActivated() {}
      virtual void OnDeactivated() {}
      virtual void OnSuspending() {}
      virtual void OnResuming() {}
      virtual void OnWindowSizeChanged(int _width, int _height) {}
  };
}
