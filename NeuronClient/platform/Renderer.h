/*
 * DeepspaceOutpost - DirectX 11 / XAudio2.
 *
 * Renderer.h
 *
 * Direct3D 11 renderer. The game draws into a fixed 512x513 logical canvas
 * (play area 512x384 plus the 512x129 HUD strip). That canvas is an off-screen
 * render target which is presented to the swap chain back buffer, aspect
 * preserved and centred (letterboxed), so the OS window can be any size.
 *
 * M1 scope: device/swapchain, off-screen RT, palette load, letterboxed present
 * and a coloured clear. The 2D primitive batch (lines/polys/sprites/text) is
 * layered on in M2/M3 via the accessors at the bottom.
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
	 * against: 512x384 play area plus the 512x129 HUD strip at y=385 (=514). The
	 * canvas is letterboxed onto the window in this mode. In "scene full-window"
	 * mode the canvas instead tracks the client area so the in-flight 3D fills the
	 * whole window; the HUD is then floated on top via a draw-origin offset. */
	static constexpr int kCanvasWidth  = 512;
	static constexpr int kCanvasHeight = 514;

	bool init(HWND hwnd);
	void shutdown();

	/* Resize the swap chain to the new client area (from WM_SIZE). */
	void resize(int clientWidth, int clientHeight);

	/* Current off-screen canvas size (retro 512x514, or the client area in scene
	 * full-window mode). The gfx batch's coordinate space and the projection use
	 * these, not the kCanvas* constants. */
	int canvasWidth()  const { return canvas_w_; }
	int canvasHeight() const { return canvas_h_; }
	int clientWidth()  const { return client_w_; }
	int clientHeight() const { return client_h_; }

	/* Switch the canvas between retro (letterboxed 512x514) and full-window (sized
	 * to the client area). Recreates the off-screen target only when the effective
	 * size changes; cheap to call every frame. */
	void ensureCanvasMode(bool fullWindow);

	/* Clear the whole off-screen canvas to a palette colour. */
	void clearCanvas(int palette_index);

	/* Bind the canvas as the render target with a full-canvas viewport, ready
	 * for the 2D primitive batch (gfx_dx11) to draw into. */
	void bindCanvasTarget();

	/* Blit the canvas to the back buffer (letterboxed) and present it. */
	void present();

	/* Master 256-colour palette loaded from scanner.bmp. Index -> 0xAABBGGRR
	 * (R8G8B8A8_UNORM byte order). Index 0 is forced fully transparent. */
	uint32_t paletteColour(int index) const { return palette_[index & 0xff]; }
	bool paletteLoaded() const { return palette_loaded_; }

	/* Accessors for the 2D primitive layer added in later milestones. */
	ID3D11Device*        device()  const { return device_.get(); }
	ID3D11DeviceContext* context() const { return context_.get(); }

private:
	bool createDeviceAndSwapChain(HWND hwnd);
	bool createBackBuffer();
	bool createCanvasTarget();
	bool recreateCanvas(int w, int h);
	bool createPresentPipeline();
	bool loadPalette();
	void computeLetterbox(D3D11_VIEWPORT& vp) const;

	winrt::com_ptr<ID3D11Device>           device_;
	winrt::com_ptr<ID3D11DeviceContext>    context_;
	winrt::com_ptr<IDXGISwapChain>         swap_chain_;
	winrt::com_ptr<ID3D11RenderTargetView> back_rtv_;

	winrt::com_ptr<ID3D11Texture2D>          canvas_tex_;
	winrt::com_ptr<ID3D11RenderTargetView>   canvas_rtv_;
	winrt::com_ptr<ID3D11ShaderResourceView> canvas_srv_;

	winrt::com_ptr<ID3D11VertexShader>   present_vs_;
	winrt::com_ptr<ID3D11PixelShader>    present_ps_;
	winrt::com_ptr<ID3D11SamplerState>   present_sampler_;
	winrt::com_ptr<ID3D11RasterizerState> present_raster_;

	HWND hwnd_ = nullptr;
	int  client_w_ = 0;
	int  client_h_ = 0;

	/* Current canvas size and mode. Defaults to the retro letterboxed canvas. */
	int  canvas_w_ = kCanvasWidth;
	int  canvas_h_ = kCanvasHeight;
	bool canvas_full_ = false;

	uint32_t palette_[256] = {};
	bool     palette_loaded_ = false;
};

/* Process-wide renderer instance, owned by the platform layer. Null until
 * gfx_graphics_startup() succeeds. */
Renderer* platform_renderer(void);

#endif /* RENDERER_H */
