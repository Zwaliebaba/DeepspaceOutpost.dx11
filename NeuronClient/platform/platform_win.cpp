/*
 * DeepspaceOutpost - DirectX 11 / XAudio2.
 *
 * platform_win.cpp
 *
 * Win32 windowing, message pump and process entry point (M0 scaffolding).
 *
 * For milestone M0 this provides a real, responsive Win32 window and the
 * screen-presentation hooks the game loop calls (gfx_graphics_startup,
 * gfx_update_screen, etc.). Rendering is a no-op until M1 introduces the
 * Direct3D 11 device and swap chain here.
 */

/* WIN32_LEAN_AND_MEAN / NOMINMAX are defined by the build (CMake). */
#include "pch.h"

#include <windows.h>

#include "platform_win.h"
#include "Renderer.h"
#include "gfx_dx11.h"
#include "input_win.h"
#include "audio_win.h"
#include "GuiOverlay.h"

#include <mmsystem.h>

#include "compat.h"
#include "gfx.h"

/* Defined by the game logic (elite.cpp). The platform forces this when the
 * window is closed so the various for(;;) sequence loops unwind cleanly. */
extern int finish;

/* Game speed regulator (ms per frame) from newkind.cfg. The original gated the
 * whole game loop on a timer at this rate; without it the game (and the intro
 * ship's rotation) runs at full unthrottled speed. */
extern int speed_cap;

namespace {

const wchar_t* kWindowClass = L"DeepspaceOutpostWindow";
const wchar_t* kWindowTitle = L"Deepspace Outpost";

/* Initial client size. The game renders a fixed 512x513 logical canvas; the
 * window is sized to a comfortable integer-scaled multiple of that for now. */
constexpr int kClientWidth  = 1024;
constexpr int kClientHeight = 1026;

HWND     g_hwnd          = nullptr;
bool     g_quit          = false;
Renderer g_renderer;
bool     g_renderer_ready = false;

LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam)
{
	switch (msg)
	{
		case WM_CLOSE:
			DestroyWindow(hwnd);
			return 0;

		case WM_DESTROY:
			g_quit = true;
			PostQuitMessage(0);
			return 0;

		case WM_SIZE:
			if (g_renderer_ready && wparam != SIZE_MINIMIZED)
				g_renderer.resize(LOWORD(lparam), HIWORD(lparam));
			return 0;

		case WM_KEYDOWN:
			input_on_key(wparam, true);
			return 0;

		case WM_KEYUP:
			input_on_key(wparam, false);
			return 0;

		case WM_CHAR:
			input_on_char(wparam);
			return 0;

		case MM_MCINOTIFY:
			if (wparam == MCI_NOTIFY_SUCCESSFUL)
				snd_midi_notify();   /* loop background music */
			return 0;
	}

	return DefWindowProcW(hwnd, msg, wparam, lparam);
}

} // namespace

HWND platform_window(void)
{
	return g_hwnd;
}

Renderer* platform_renderer(void)
{
	return g_renderer_ready ? &g_renderer : nullptr;
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
		/* The original game loop and its nested intro/escape/game-over
		 * sequences only test the `finish` flag in a few places, so closing
		 * the window mid-sequence must terminate the process directly. */
		finish = 1;
		ExitProcess(0);
	}
}

/* ---- gfx.h: screen / lifecycle hooks (drawing primitives live in gfx_dx11/stub) ---- */

int gfx_graphics_startup(void)
{
	HINSTANCE hinst = GetModuleHandleW(nullptr);

	WNDCLASSEXW wc{};
	wc.cbSize        = sizeof(wc);
	wc.style         = CS_HREDRAW | CS_VREDRAW | CS_OWNDC;
	wc.lpfnWndProc   = WndProc;
	wc.hInstance     = hinst;
	wc.hCursor       = LoadCursor(nullptr, IDC_ARROW);
	wc.hbrBackground = reinterpret_cast<HBRUSH>(GetStockObject(BLACK_BRUSH));
	wc.lpszClassName = kWindowClass;

	if (!RegisterClassExW(&wc))
		return 1;

	RECT rc{ 0, 0, kClientWidth, kClientHeight };
	DWORD style = WS_OVERLAPPEDWINDOW;
	AdjustWindowRect(&rc, style, FALSE);

	g_hwnd = CreateWindowExW(
		0, kWindowClass, kWindowTitle, style,
		CW_USEDEFAULT, CW_USEDEFAULT,
		rc.right - rc.left, rc.bottom - rc.top,
		nullptr, nullptr, hinst, nullptr);

	if (!g_hwnd)
		return 1;

	ShowWindow(g_hwnd, SW_SHOW);
	UpdateWindow(g_hwnd);

	if (!g_renderer.init(g_hwnd))
	{
		MessageBoxW(g_hwnd, L"Failed to initialise Direct3D 11.", kWindowTitle, MB_ICONERROR);
		return 1;
	}
	g_renderer_ready = true;

	/* Bring up the native GraphicsCore/ImmediateRenderer GUI overlay on the same
	 * device (best-effort; leaves the GUI disabled on any failure). Toggle with F1. */
	GuiOverlay::Startup();
	return 0;
}

void gfx_graphics_shutdown(void)
{
	GuiOverlay::Shutdown();
	if (g_renderer_ready)
	{
		g_renderer.shutdown();
		g_renderer_ready = false;
	}
	if (g_hwnd)
	{
		DestroyWindow(g_hwnd);
		g_hwnd = nullptr;
	}
	UnregisterClassW(kWindowClass, GetModuleHandleW(nullptr));
}

void gfx_update_screen(void)
{
	if (g_renderer_ready)
	{
		gfx_dx11_flush();        /* replay the frame's 2D batch into the canvas */
		/* Native GUI overlay (F1): drawn into the same canvas, on top of the game's
		 * 2D, before the letterboxed present. No-op unless shown. */
		GuiOverlay::Update();
		GuiOverlay::Render(g_renderer.canvasWidth(), g_renderer.canvasHeight());
		g_renderer.present();    /* blit the canvas to the window */
		/* Default the NEXT frame to the retro letterboxed canvas; the in-flight
		 * render path re-enables full-window mode each frame it draws. This keeps
		 * menus/charts/station on the classic 512x514 canvas automatically. */
		gfx_set_scene_fullwindow(0);
	}
	platform_pump_messages();

	/* Regulate to speed_cap ms/frame so the game runs at the intended pace
	 * (replaces the Allegro install_int frame timer). */
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
				Sleep(1);            /* coarse wait, leave a margin */
			/* else: busy-spin the final ~3ms for accuracy */
		}
	}
	QueryPerformanceCounter(&prev);
}

/* The process entry point (wWinMain) lives in the game executable
 * (DeepspaceOutpost/WinMain.cpp), not here: a static library's entry point is
 * not pulled in by the linker. It calls game_main(), which drives this layer
 * through the gfx.h lifecycle hooks above. */
