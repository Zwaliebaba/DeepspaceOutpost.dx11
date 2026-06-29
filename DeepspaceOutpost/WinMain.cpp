/*
 * DeepspaceOutpost - process entry point.
 *
 * Boots the client engine (ClientEngine, in NeuronClient), which owns the window and
 * the native Direct3D 11 device, then hands a GameApp to it and runs the classic
 * game loop via game_main(). The platform layer's gfx_graphics_startup() adopts the
 * engine's device for the legacy gfx_* render path. The entry point must live in the
 * exe: a static library's entry point is not pulled in by the linker.
 */

#include "pch.h"

#include <windows.h>
#if defined(_DEBUG)
#include <crtdbg.h>
#endif

#include "GameApp.h"      /* GameApp : Neuron::GameMain, + ClientEngine */
#include "main.h"         /* game_main() */

int WINAPI wWinMain(HINSTANCE _hInstance, HINSTANCE /*_hPrevInstance*/, LPWSTR /*_cmdLine*/, int _iCmdShow)
{
#if defined(_DEBUG)
  _CrtSetDbgFlag(_CRTDBG_ALLOC_MEM_DF | _CRTDBG_LEAK_CHECK_DF);
#endif

  /* Anchor to the executable's folder. The game opens GameData assets with paths
   * relative to the current directory (so SetCurrentDirectory is required), and
   * FileSys uses the home directory for its own reads. */
  wchar_t filename[MAX_PATH];
  GetModuleFileNameW(nullptr, filename, MAX_PATH);
  std::wstring path(filename);
  path = path.substr(0, path.find_last_of(L'\\'));
  SetCurrentDirectoryW(path.c_str());
  FileSys::SetHomeDirectory(path);

  /* Render at native resolution rather than letting Windows bitmap-stretch a
   * DPI-unaware window. */
  SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);

  ClientEngine::Startup(L"Deep Space Outpost", _hInstance, _iCmdShow);

  auto main = winrt::make_self<GameApp>();
  ClientEngine::StartGame(main);

  game_main();

  ClientEngine::Shutdown();
  main = nullptr;

  return 0;
}
