/*
 * DeepspaceOutpost - DirectX 11 / XAudio2.
 *
 * Renderer.h
 *
 * The canvas/palette companion to Neuron::Graphics::Core. Core is the single owner of
 * the D3D11 device/context/swap chain and the one place presentation and device-lost
 * recovery live; Renderer keeps no device objects of its own and defers to Core for
 * them. Its remaining jobs are the master palette and the cached client size the 2D
 * letterbox math needs. The 2D layer (Render2D) draws straight to Core's back-buffer
 * render target - the game's virtual 512x514 space (or the client area in full-window
 * flight) is letterboxed onto it by the viewport in gfx2d_flush.
 */

#ifndef RENDERER_H
#define RENDERER_H

#include <windows.h>
#include <cstdint>

class Renderer
{
public:
	/* The retro logical canvas the 2D UI (menus/charts/station/HUD) is authored
	 * against: a 512x384 play area plus the 512x129 HUD strip at y=385 (=514 tall).
	 * gfx2d draws in this virtual space (or the client area when the in-flight 3D
	 * fills the window) and letterboxes it straight onto the back buffer. */
	static constexpr int kCanvasWidth  = 512;
	static constexpr int kCanvasHeight = 514;

	/* Confirm Core's device is up and load the palette + cache the client size.
	 * (Core, owned by ClientEngine, created the device/swap chain already.) */
	bool initAdopt();
	void shutdown();

	int clientWidth()  const { return client_w_; }
	int clientHeight() const { return client_h_; }

	/* Around Core::WindowSizeChanged (WM_SIZE): unbind render targets before it runs,
	 * update the cached client size after. Core owns the back-buffer view and recreates
	 * it on resize, so there is nothing of ours to rebuild. */
	void onResizePre();
	void onResizePost(int clientWidth, int clientHeight);

	/* Master 256-colour palette (baked in scanner_palette.h). Index -> 0xAABBGGRR
	 * (R8G8B8A8_UNORM byte order); index 0 is the transparent colour key. */
	uint32_t paletteColour(int index) const { return palette_[index & 0xff]; }

private:
	bool loadPalette();

	int  client_w_ = 0;
	int  client_h_ = 0;

	uint32_t palette_[256] = {};
};

/* Process-wide renderer instance, owned by the platform layer. Null until
 * gfx_graphics_startup() succeeds. */
Renderer* platform_renderer(void);

#endif /* RENDERER_H */
