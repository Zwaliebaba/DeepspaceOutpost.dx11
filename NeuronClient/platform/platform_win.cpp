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

#include <windows.h>
#include <mmsystem.h>

#include "platform_win.h"
#include "Renderer.h"
#include "gfx2d.h"
#include "audio_win.h"
#include "GuiOverlay.h"

#include "ClientEngine.h"
#include "EventManager.h"

#include "gfx.h"

/* Defined by the game logic (elite.cpp). The platform forces this when the window is
 * closed so the various for(;;) sequence loops unwind cleanly. */
extern int finish;

/* Game speed regulator (ms per frame) from newkind.cfg. */
extern int speed_cap;

namespace {

bool     g_quit          = false;
Renderer g_renderer;
bool     g_renderer_ready = false;

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
		 * test the `finish` flag in a few places, so closing the window mid-sequence
		 * must terminate the process directly. */
		finish = 1;
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
	if (g_renderer_ready)
	{
		gfx2d_flush();           /* draw the frame's 2D batch (letterboxed) to the back buffer */
		GuiOverlay::Update();
		/* The GUI draws full-window on top (client space), then present. No-op unless shown. */
		GuiOverlay::Render(g_renderer.clientWidth(), g_renderer.clientHeight());
		g_renderer.swap();
		/* Default the NEXT frame to the retro letterboxed canvas; the in-flight
		 * render path re-enables full-window mode each frame it draws. */
		gfx_set_scene_fullwindow(0);
	}
	platform_pump_messages();

	/* Regulate to speed_cap ms/frame so the game runs at the intended pace. */
	static LARGE_INTEGER freq = { 0 };
	static LARGE_INTEGER prev = { 0 };
	if (freq.QuadPart == 0)
		QueryPerformanceFrequency(&freq);

	int cap = (speed_cap > 0) ? speed_cap : 55;
	double target = cap / 1000.0;

	if (prev.QuadPart != 0)
	{
		for (;;)
		{
			LARGE_INTEGER now;
			QueryPerformanceCounter(&now);
			double elapsed = double(now.QuadPart - prev.QuadPart) / double(freq.QuadPart);
			if (elapsed >= target)
				break;
			if (target - elapsed > 0.003)
				Sleep(1);
		}
	}
	QueryPerformanceCounter(&prev);
}

/* The process entry point (wWinMain) lives in the game executable
 * (DeepspaceOutpost/WinMain.cpp): a static library's entry point is not pulled in by
 * the linker. It boots ClientEngine, then calls game_main(), which drives this layer
 * through the gfx.h lifecycle hooks above. */
