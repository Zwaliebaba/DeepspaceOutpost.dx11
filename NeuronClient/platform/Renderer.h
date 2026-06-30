/*
 * DeepspaceOutpost - DirectX 11 / XAudio2.
 *
 * Renderer.h
 *
 * Thin owner of the window's device/context/swap chain (adopted from
 * Neuron::Graphics::Core) plus the master palette. The 2D layer (Render2D) draws
 * straight to Core's back-buffer render target - the game's virtual 512x514 space
 * (or the client area in full-window flight) is letterboxed onto it by the viewport
 * in gfx2d_flush - so the renderer keeps no render targets of its own.
 */

#ifndef RENDERER_H
#define RENDERER_H

#include <windows.h>
#include <d3d11.h>
#include <winrt/base.h>
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

	/* Adopt the device/context/swap chain Neuron::Graphics::Core created (ClientEngine
	 * owns the lifetime) and load the palette. */
	bool initAdopt();
	void shutdown();

	int clientWidth()  const { return client_w_; }
	int clientHeight() const { return client_h_; }

	/* Present the current back buffer. */
	void swap();

	/* Around Core::WindowSizeChanged (WM_SIZE): unbind render targets before it runs,
	 * update the cached client size after. Core owns the back-buffer view and recreates
	 * it on resize, so there is nothing of ours to rebuild. */
	void onResizePre();
	void onResizePost(int clientWidth, int clientHeight);

	/* Master 256-colour palette (baked in scanner_palette.h). Index -> 0xAABBGGRR
	 * (R8G8B8A8_UNORM byte order); index 0 is the transparent colour key. */
	uint32_t paletteColour(int index) const { return palette_[index & 0xff]; }
	bool paletteLoaded() const { return palette_loaded_; }

	ID3D11Device*        device()    const { return device_.get(); }
	ID3D11DeviceContext* context()   const { return context_.get(); }
	IDXGISwapChain*      swapChain() const { return swap_chain_.get(); }

private:
	bool loadPalette();

	winrt::com_ptr<ID3D11Device>        device_;
	winrt::com_ptr<ID3D11DeviceContext> context_;
	winrt::com_ptr<IDXGISwapChain>      swap_chain_;

	HWND hwnd_ = nullptr;
	int  client_w_ = 0;
	int  client_h_ = 0;

	uint32_t palette_[256] = {};
	bool     palette_loaded_ = false;
};

/* Process-wide renderer instance, owned by the platform layer. Null until
 * gfx_graphics_startup() succeeds. */
Renderer* platform_renderer(void);

#endif /* RENDERER_H */
