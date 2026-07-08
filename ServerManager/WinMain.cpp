/*
 * ServerManager - process entry point (sm.md).
 *
 * A standalone Win32 GUI app that connects to a running dedicated server over its
 * SEPARATE management port and shows a live status dashboard (who is connected, what
 * is happening, server health). It reuses the same client engine as the game
 * (ClientEngine window + native Direct3D 11 device + Canvas/GuiWindow), minus the game:
 * ClientEngine::Frame drives a ManagerApp (a Neuron::GameMain) once per frame.
 *
 * The entry point must live in the exe: a static library's entry point is not pulled
 * in by the linker.
 */

#include "pch.h"

#include <windows.h>

#include "FileSys.h"        // Neuron::FileSys::SetHomeDirectory
#include "ClientEngine.h"
#include "ManagerApp.h"

// Legacy-compat definitions: the engine's platform layer (platform_win.cpp, still
// shared with the game) references these two game-loop globals via `extern`. The game
// exe defines them in elite.cpp; a non-game engine host must supply them itself so the
// platform object links. `finish` is forced to 1 on window close; `speed_cap` is the
// legacy frame-time regulator (unused by ServerManager, which paces via Frame()).
int finish = 0;
int speed_cap = 33;

int WINAPI wWinMain(HINSTANCE _hInstance, HINSTANCE /*_hPrevInstance*/, LPWSTR /*_cmdLine*/, int _iCmdShow)
{
  // Anchor to the executable's folder so servermanager.cfg and GameData assets (fonts,
  // strings, textures) resolve relative to it, exactly like the game client does.
  wchar_t filename[MAX_PATH];
  GetModuleFileNameW(nullptr, filename, MAX_PATH);
  std::wstring path(filename);
  path = path.substr(0, path.find_last_of(L'\\'));
  SetCurrentDirectoryW(path.c_str());
  Neuron::FileSys::SetHomeDirectory(path);

  SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);

  ClientEngine::Startup(L"Deep Space Outpost - Server Manager", _hInstance, _iCmdShow);

  auto app = winrt::make_self<DSOManager::ManagerApp>();
  ClientEngine::StartGame(app);

  // Drive the engine frame loop at ~30 Hz. platform_pump_messages() (inside Frame)
  // ExitProcess()es when the window is closed, so this loop does not return normally;
  // the Shutdown below is for completeness.
  for (;;)
    ClientEngine::Frame(33);

  ClientEngine::Shutdown();
  return 0;
}
