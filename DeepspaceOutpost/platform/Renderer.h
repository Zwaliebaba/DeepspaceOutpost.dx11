/*
 * DeepspaceOutpost - DirectX 11 / XAudio2 port of Elite: The New Kind.
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
#include <wrl/client.h>
#include <cstdint>

class Renderer
{
public:
	/* The logical canvas the whole game is authored against: 512x384 play area
	 * plus the 512x129 HUD strip starting at y=385 (385 + 129 = 514). */
	static constexpr int kCanvasWidth  = 512;
	static constexpr int kCanvasHeight = 514;

	bool init(HWND hwnd);
	void shutdown();

	/* Resize the swap chain to the new client area (from WM_SIZE). */
	void resize(int clientWidth, int clientHeight);

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
	ID3D11Device*        device()  const { return device_.Get(); }
	ID3D11DeviceContext* context() const { return context_.Get(); }

private:
	bool createDeviceAndSwapChain(HWND hwnd);
	bool createBackBuffer();
	bool createCanvasTarget();
	bool createPresentPipeline();
	bool loadPalette();
	void computeLetterbox(D3D11_VIEWPORT& vp) const;

	Microsoft::WRL::ComPtr<ID3D11Device>           device_;
	Microsoft::WRL::ComPtr<ID3D11DeviceContext>    context_;
	Microsoft::WRL::ComPtr<IDXGISwapChain>         swap_chain_;
	Microsoft::WRL::ComPtr<ID3D11RenderTargetView> back_rtv_;

	Microsoft::WRL::ComPtr<ID3D11Texture2D>          canvas_tex_;
	Microsoft::WRL::ComPtr<ID3D11RenderTargetView>   canvas_rtv_;
	Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> canvas_srv_;

	Microsoft::WRL::ComPtr<ID3D11VertexShader>   present_vs_;
	Microsoft::WRL::ComPtr<ID3D11PixelShader>    present_ps_;
	Microsoft::WRL::ComPtr<ID3D11SamplerState>   present_sampler_;
	Microsoft::WRL::ComPtr<ID3D11RasterizerState> present_raster_;

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
