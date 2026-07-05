/*
 * DeepspaceOutpost - DirectX 11 / XAudio2.
 *
 * platform_win.cpp
 *
 * The window and the Direct3D 11 device are now owned by ClientEngine (NeuronClient).
 * This file keeps the legacy gfx.h presentation hooks the classic game loop calls:
 * it ADOPTS the engine's device into the Renderer (canvas + letterboxed present),
 * pumps the message queue, and drives the per-frame flush + GUI overlay + present.
 */

#include "pch.h"

#include <atomic>
#include <windows.h>
#include <mmsystem.h>

#include "platform_win.h"
#include "Renderer.h"
#include "audio_win.h"
#include "sound.h"
#include "GameScene.h"   // gfx_graphics_startup / gfx_graphics_shutdown / gfx_update_screen (defined here)

#include "ClientEngine.h"
#include "EventManager.h"

/* Defined by the game logic (elite.cpp). The platform forces this when the window is
 * closed so the various for(;;) sequence loops unwind cleanly. */
extern int finish;

/* Game speed regulator (ms per frame); a fixed client default (elite.cpp). */
extern int speed_cap;

namespace {

bool     g_quit          = false;
Renderer g_renderer;
bool     g_renderer_ready = false;
static std::atomic_bool g_forced_shutdown = false;

/* EventManager processor for platform messages the legacy code relies on. Returns -1
 * for messages it doesn't handle so the EventManager chain / DefWindowProc continue. */
LRESULT CALLBACK PlatformWndProc(HWND, UINT msg, WPARAM wparam, LPARAM)
{
	if (msg == MM_MCINOTIFY)
	{
		if (wparam == MCI_NOTIFY_SUCCESSFUL)
			snd_midi_notify();   /* loop background music */
		return 0;
	}
	return -1;
}

} // namespace

HWND platform_window(void)
{
	return ClientEngine::Window();
}

Renderer* platform_renderer(void)
{
	return g_renderer_ready ? &g_renderer : nullptr;
}

void platform_request_quit(void)
{
	/* Mirror the user closing the window: WM_CLOSE -> DestroyWindow -> WM_DESTROY,
	 * which posts WM_QUIT so the next pump unwinds and exits. */
	if (HWND hwnd = ClientEngine::Window())
		PostMessageW(hwnd, WM_CLOSE, 0, 0);
}

void platform_pump_messages(void)
{
	MSG msg;
	while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE))
	{
		TranslateMessage(&msg);
		DispatchMessageW(&msg);
		if (msg.message == WM_QUIT)
			g_quit = true;
	}

	if (g_quit)
	{
		/* The original game loop and its nested intro/escape/game-over sequences only
		 * test the `finish` flag in a few places. Release platform and D3D resources before
		 * the forced process exit so the debug runtimes do not report every live device object. */
		finish = 1;
		if (!g_forced_shutdown.exchange(true))
		{
			/* Match the normal game_main/WinMain teardown order: stop legacy sound first,
			 * clear the adopted renderer flag, then release the ClientEngine-owned D3D objects. */
			snd_sound_shutdown();
			if (g_renderer_ready)
			{
				g_renderer.shutdown();
				g_renderer_ready = false;
			}
			ClientEngine::Shutdown();
		}
		ExitProcess(0);
	}
}

/* ---- gfx.h: screen / lifecycle hooks (drawing primitives live in gfx2d) ---- */

int gfx_graphics_startup(void)
{
	/* The window + device already exist (ClientEngine::Startup ran from wWinMain).
	 * Adopt them into the Renderer (canvas + present pipeline) and register the
	 * platform message processor. */
	EventManager::AddEventProcessor(PlatformWndProc);

	if (!g_renderer.initAdopt())
	{
		MessageBoxW(ClientEngine::Window(), L"Failed to initialise the Direct3D 11 renderer.", L"Deepspace Outpost", MB_ICONERROR);
		return 1;
	}
	g_renderer_ready = true;
	return 0;
}

void gfx_graphics_shutdown(void)
{
	EventManager::RemoveEventProcessor(PlatformWndProc);
	if (g_renderer_ready)
	{
		g_renderer.shutdown();
		g_renderer_ready = false;
	}
	/* The window and Core device are owned by ClientEngine and torn down in
	 * ClientEngine::Shutdown() after game_main() returns. */
}

void gfx_update_screen(void)
{
	/* The engine owns the whole frame now: GameMain lifecycle (Update/RenderScene/
	   RenderCanvas) + flush + present, then the OS message pump and frame pacing. This
	   gfx.h hook just hands it the game's speed_cap (the platform owns that config value;
	   the engine owns the loop mechanics). */
	ClientEngine::Frame(speed_cap);
}

/* The process entry point (wWinMain) lives in the game executable
 * (DeepspaceOutpost/WinMain.cpp): a static library's entry point is not pulled in by
 * the linker. It boots ClientEngine, then calls game_main(), which drives this layer
 * through the gfx.h lifecycle hooks above. */
