/*
 * DeepspaceOutpost - process entry point.
 *
 * The Win32 / Direct3D 11 / XAudio2 platform layer now lives in NeuronClient.
 * Only the executable's entry point stays in the game target: an entry point
 * defined inside a static library is not pulled in by the linker, so wWinMain
 * has to live in the exe. It boots the engine, anchors the working directory to
 * the executable's folder (where CMake stages GameData), then hands control to
 * the game via game_main(). All windowing/rendering/audio/input is reached
 * through the engine's gfx.h / sound.h / keyboard contracts.
 */

#include "pch.h"

#include <windows.h>

#include "main.h"   /* game_main() */

/* The game opens its assets and config (newkind.cfg, scanner.bmp, *.nkc
 * commander files) with relative paths, so anchor the working directory to the
 * executable's folder where CMake stages GameData. */
static void set_working_dir_to_exe(void)
{
	wchar_t path[MAX_PATH];
	DWORD n = GetModuleFileNameW(nullptr, path, MAX_PATH);
	if (n == 0 || n >= MAX_PATH)
		return;
	for (DWORD i = n; i > 0; --i)
	{
		if (path[i - 1] == L'\\' || path[i - 1] == L'/')
		{
			path[i - 1] = L'\0';
			SetCurrentDirectoryW(path);
			return;
		}
	}
}

int WINAPI wWinMain(HINSTANCE, HINSTANCE, LPWSTR, int)
{
	CoreEngine::Startup();

	/* Render at native resolution rather than letting Windows bitmap-stretch
	 * a DPI-unaware window (crisper, correctly-sized canvas). */
	SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);

	set_working_dir_to_exe();
	int ret = game_main();

	CoreEngine::Shutdown();
	return ret;
}
