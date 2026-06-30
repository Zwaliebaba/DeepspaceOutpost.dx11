/*
 * DeepspaceOutpost - DirectX 11 / XAudio2.
 *
 * Renderer.cpp  (M1)
 */

#include "pch.h"

#include "Renderer.h"
#include "GraphicsCore.h" // Neuron::Graphics::Core (device unification)
#include "scanner_palette.h" // baked master 256-colour palette

#include <d3dcompiler.h>
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <iterator>
#include <vector>

using winrt::com_ptr;

namespace {

/* Fullscreen-triangle blit: samples the 512x513 canvas into the letterbox
 * viewport. No vertex/index buffers - positions come from SV_VertexID. */
const char* kPresentHLSL = R"(
Texture2D    gCanvas : register(t0);
SamplerState gSamp   : register(s0);

struct VSOut { float4 pos : SV_Position; float2 uv : TEXCOORD0; };

VSOut VSMain(uint id : SV_VertexID)
{
    VSOut o;
    float2 uv = float2((id << 1) & 2, id & 2);   // (0,0) (2,0) (0,2)
    o.uv  = uv;
    o.pos = float4(uv * float2(2.0, -2.0) + float2(-1.0, 1.0), 0.0, 1.0);
    return o;
}

float4 PSMain(VSOut i) : SV_Target
{
    return gCanvas.Sample(gSamp, i.uv);
}
)";

com_ptr<ID3DBlob> compileShader(const char* src, const char* entry, const char* target)
{
	com_ptr<ID3DBlob> code, errors;
	UINT flags = D3DCOMPILE_ENABLE_STRICTNESS;
#ifdef _DEBUG
	flags |= D3DCOMPILE_DEBUG | D3DCOMPILE_SKIP_OPTIMIZATION;
#endif
	HRESULT hr = D3DCompile(src, std::strlen(src), nullptr, nullptr, nullptr,
							entry, target, flags, 0, code.put(), errors.put());
	if (FAILED(hr))
	{
		if (errors)
			OutputDebugStringA(static_cast<const char*>(errors->GetBufferPointer()));
		return nullptr;
	}
	return code;
}

} // namespace

bool Renderer::init(HWND hwnd)
{
	hwnd_ = hwnd;

	RECT rc{};
	GetClientRect(hwnd, &rc);
	client_w_ = std::max<int>(1, rc.right - rc.left);
	client_h_ = std::max<int>(1, rc.bottom - rc.top);

	if (!createDeviceAndSwapChain(hwnd)) return false;
	if (!createBackBuffer())             return false;
	if (!createCanvasTarget())           return false;
	if (!createPresentPipeline())        return false;

	loadPalette();   /* non-fatal: falls back to a synthetic ramp */

	/* Start with an opaque black canvas; the game clears the play area itself. */
	const float black[4] = { 0.0f, 0.0f, 0.0f, 1.0f };
	context_->ClearRenderTargetView(canvas_rtv_.get(), black);
	return true;
}

bool Renderer::initAdopt()
{
	using namespace Neuron::Graphics;

	/* Take references on the device/context/swap chain Core created (ClientEngine owns
	 * the lifetime; these are non-owning aliases held for the canvas + present path). */
	device_.copy_from(Core::GetD3DDevice());
	context_.copy_from(Core::GetD3DDeviceContext());
	swap_chain_.copy_from(Core::GetSwapChain());
	if (!device_ || !context_ || !swap_chain_) return false;

	hwnd_ = Core::GetWindow();
	const auto size = Core::GetOutputSize();
	client_w_ = std::max<int>(1, static_cast<int>(size.Width));
	client_h_ = std::max<int>(1, static_cast<int>(size.Height));

	if (!createBackBuffer())      return false;
	if (!createCanvasTarget())    return false;
	if (!createPresentPipeline()) return false;

	loadPalette();   /* non-fatal: falls back to a synthetic ramp */

	const float black[4] = { 0.0f, 0.0f, 0.0f, 1.0f };
	context_->ClearRenderTargetView(canvas_rtv_.get(), black);
	return true;
}

void Renderer::shutdown()
{
	if (context_) context_->ClearState();
	/* com_ptr members release automatically. */
}

bool Renderer::createDeviceAndSwapChain(HWND hwnd)
{
	DXGI_SWAP_CHAIN_DESC sd{};
	sd.BufferCount       = 2;
	sd.BufferDesc.Width  = client_w_;
	sd.BufferDesc.Height = client_h_;
	sd.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
	sd.BufferDesc.RefreshRate.Numerator   = 60;
	sd.BufferDesc.RefreshRate.Denominator = 1;
	sd.BufferUsage  = DXGI_USAGE_RENDER_TARGET_OUTPUT;
	sd.OutputWindow = hwnd;
	sd.SampleDesc.Count = 1;
	sd.Windowed     = TRUE;
	sd.SwapEffect   = DXGI_SWAP_EFFECT_FLIP_DISCARD;

	const D3D_FEATURE_LEVEL wanted[] = { D3D_FEATURE_LEVEL_11_1, D3D_FEATURE_LEVEL_11_0 };
	D3D_FEATURE_LEVEL got{};

	HRESULT hr = D3D11CreateDeviceAndSwapChain(
		nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, 0,
		wanted, static_cast<UINT>(std::size(wanted)), D3D11_SDK_VERSION,
		&sd, swap_chain_.put(), device_.put(), &got, context_.put());

	if (FAILED(hr))
	{
		/* Retry without the FLIP model for older configurations. Release any
		 * partial outputs first so put() writes into null com_ptr slots. */
		swap_chain_ = nullptr;
		device_     = nullptr;
		context_    = nullptr;
		sd.BufferCount = 1;
		sd.SwapEffect  = DXGI_SWAP_EFFECT_DISCARD;
		hr = D3D11CreateDeviceAndSwapChain(
			nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, 0,
			wanted, static_cast<UINT>(std::size(wanted)), D3D11_SDK_VERSION,
			&sd, swap_chain_.put(), device_.put(), &got, context_.put());
	}

	return SUCCEEDED(hr);
}

bool Renderer::createBackBuffer()
{
	com_ptr<ID3D11Texture2D> back;
	if (FAILED(swap_chain_->GetBuffer(0, IID_PPV_ARGS(back.put()))))
		return false;
	return SUCCEEDED(device_->CreateRenderTargetView(back.get(), nullptr, back_rtv_.put()));
}

bool Renderer::createCanvasTarget()
{
	D3D11_TEXTURE2D_DESC td{};
	td.Width      = canvas_w_;
	td.Height     = canvas_h_;
	td.MipLevels  = 1;
	td.ArraySize  = 1;
	td.Format     = DXGI_FORMAT_R8G8B8A8_UNORM;
	td.SampleDesc.Count = 1;
	td.Usage      = D3D11_USAGE_DEFAULT;
	td.BindFlags  = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;

	if (FAILED(device_->CreateTexture2D(&td, nullptr, canvas_tex_.put())))   return false;
	if (FAILED(device_->CreateRenderTargetView(canvas_tex_.get(), nullptr, canvas_rtv_.put()))) return false;
	if (FAILED(device_->CreateShaderResourceView(canvas_tex_.get(), nullptr, canvas_srv_.put()))) return false;
	return true;
}

bool Renderer::recreateCanvas(int w, int h)
{
	if (w < 1) w = 1;
	if (h < 1) h = 1;

	/* Release the old views/texture so put() writes into null com_ptr slots. */
	canvas_srv_ = nullptr;
	canvas_rtv_ = nullptr;
	canvas_tex_ = nullptr;

	canvas_w_ = w;
	canvas_h_ = h;
	return createCanvasTarget();
}

void Renderer::ensureCanvasMode(bool fullWindow)
{
	const int w = fullWindow ? client_w_ : kCanvasWidth;
	const int h = fullWindow ? client_h_ : kCanvasHeight;
	if (fullWindow == canvas_full_ && w == canvas_w_ && h == canvas_h_)
		return;
	canvas_full_ = fullWindow;
	recreateCanvas(w, h);
}

bool Renderer::createPresentPipeline()
{
	com_ptr<ID3DBlob> vs = compileShader(kPresentHLSL, "VSMain", "vs_5_0");
	com_ptr<ID3DBlob> ps = compileShader(kPresentHLSL, "PSMain", "ps_5_0");
	if (!vs || !ps) return false;

	if (FAILED(device_->CreateVertexShader(vs->GetBufferPointer(), vs->GetBufferSize(), nullptr, present_vs_.put())))
		return false;
	if (FAILED(device_->CreatePixelShader(ps->GetBufferPointer(), ps->GetBufferSize(), nullptr, present_ps_.put())))
		return false;

	D3D11_SAMPLER_DESC sm{};
	sm.Filter   = D3D11_FILTER_MIN_MAG_MIP_POINT;   /* crisp pixel art */
	sm.AddressU = sm.AddressV = sm.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
	sm.ComparisonFunc = D3D11_COMPARISON_NEVER;
	sm.MaxLOD = D3D11_FLOAT32_MAX;
	if (FAILED(device_->CreateSamplerState(&sm, present_sampler_.put())))
		return false;

	/* No culling (the fullscreen triangle winding is arbitrary) and scissor
	 * disabled, so the canvas batch's scissor state can't clip the present. */
	D3D11_RASTERIZER_DESC rs{};
	rs.FillMode = D3D11_FILL_SOLID;
	rs.CullMode = D3D11_CULL_NONE;
	rs.ScissorEnable = FALSE;
	rs.DepthClipEnable = TRUE;
	return SUCCEEDED(device_->CreateRasterizerState(&rs, present_raster_.put()));
}

bool Renderer::loadPalette()
{
	/* The master 256-colour palette is baked into the engine (scanner_palette.h). It
	 * used to be read from the 8-bit scanner.bmp, but the art now ships as .dds (which
	 * carries no indexed palette), so there is no longer a file to parse - the table is
	 * the single source of truth. Entries are already in paletteColour()'s 0xAABBGGRR
	 * byte order; index 0 is the transparent colour key. */
	std::memcpy(palette_, kScannerPalette, sizeof(palette_));
	palette_loaded_ = true;
	return true;
}

void Renderer::computeLetterbox(D3D11_VIEWPORT& vp) const
{
	/* Full-window canvas (canvas == client) integer-scales by 1, i.e. fills the
	 * window with no bars; the retro canvas is letterboxed as before. */
	int scale = std::min(client_w_ / canvas_w_, client_h_ / canvas_h_);
	if (scale < 1) scale = 1;   /* tiny window: crop rather than vanish */

	int dstW = canvas_w_ * scale;
	int dstH = canvas_h_ * scale;

	vp.TopLeftX = static_cast<float>((client_w_ - dstW) / 2);
	vp.TopLeftY = static_cast<float>((client_h_ - dstH) / 2);
	vp.Width    = static_cast<float>(dstW);
	vp.Height   = static_cast<float>(dstH);
	vp.MinDepth = 0.0f;
	vp.MaxDepth = 1.0f;
}

void Renderer::clearCanvas(int palette_index)
{
	uint32_t c = paletteColour(palette_index);
	const float rgba[4] = {
		((c >>  0) & 0xff) / 255.0f,
		((c >>  8) & 0xff) / 255.0f,
		((c >> 16) & 0xff) / 255.0f,
		1.0f,
	};
	context_->ClearRenderTargetView(canvas_rtv_.get(), rgba);
}

void Renderer::bindCanvasTarget()
{
	ID3D11RenderTargetView* rtv = canvas_rtv_.get();
	context_->OMSetRenderTargets(1, &rtv, nullptr);

	D3D11_VIEWPORT vp{};
	vp.TopLeftX = 0.0f;
	vp.TopLeftY = 0.0f;
	vp.Width    = static_cast<float>(canvas_w_);
	vp.Height   = static_cast<float>(canvas_h_);
	vp.MinDepth = 0.0f;
	vp.MaxDepth = 1.0f;
	context_->RSSetViewports(1, &vp);
}

void Renderer::blitCanvasToBackBuffer()
{
	if (!swap_chain_) return;

	const float black[4] = { 0.0f, 0.0f, 0.0f, 1.0f };
	ID3D11RenderTargetView* rtv = back_rtv_.get();
	context_->OMSetRenderTargets(1, &rtv, nullptr);
	context_->ClearRenderTargetView(rtv, black);

	D3D11_VIEWPORT vp{};
	computeLetterbox(vp);
	context_->RSSetViewports(1, &vp);
	context_->RSSetState(present_raster_.get());

	/* Bind our own opaque/no-depth output-merger state rather than inheriting whatever
	 * the 2D batch left set: this blit is a straight copy of the canvas. */
	context_->OMSetBlendState(nullptr, nullptr, 0xFFFFFFFF);
	context_->OMSetDepthStencilState(nullptr, 0);

	context_->IASetInputLayout(nullptr);
	context_->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
	context_->VSSetShader(present_vs_.get(), nullptr, 0);
	context_->PSSetShader(present_ps_.get(), nullptr, 0);
	ID3D11ShaderResourceView* srv = canvas_srv_.get();
	ID3D11SamplerState* smp = present_sampler_.get();
	context_->PSSetShaderResources(0, 1, &srv);
	context_->PSSetSamplers(0, 1, &smp);
	context_->Draw(3, 0);

	/* Unbind the canvas SRV so it can be used as an RT again next frame. */
	ID3D11ShaderResourceView* nullsrv = nullptr;
	context_->PSSetShaderResources(0, 1, &nullsrv);

	/* Leave the back buffer bound with a FULL client-area viewport so a full-window
	 * overlay (the GUI) drawn next renders in client space, not the letterbox. */
	D3D11_VIEWPORT full{};
	full.TopLeftX = 0.0f;
	full.TopLeftY = 0.0f;
	full.Width    = static_cast<float>(client_w_);
	full.Height   = static_cast<float>(client_h_);
	full.MinDepth = 0.0f;
	full.MaxDepth = 1.0f;
	context_->RSSetViewports(1, &full);
}

void Renderer::swap()
{
	if (swap_chain_) swap_chain_->Present(1, 0);
}

void Renderer::present()
{
	blitCanvasToBackBuffer();
	swap();
}

void Renderer::onResizePre()
{
	/* Release our view of the swap-chain back buffer so Core::WindowSizeChanged's
	 * ResizeBuffers can succeed (no outstanding references to the buffers). */
	back_rtv_ = nullptr;
	context_->OMSetRenderTargets(0, nullptr, nullptr);
}

void Renderer::onResizePost(int clientWidth, int clientHeight)
{
	client_w_ = std::max<int>(1, clientWidth);
	client_h_ = std::max<int>(1, clientHeight);
	createBackBuffer();   /* recreate back_rtv_ from the resized swap chain */
	/* The retro 512x514 canvas is fixed and letterboxed, so it does not resize. */
}

void Renderer::resize(int clientWidth, int clientHeight)
{
	if (!swap_chain_) return;
	if (clientWidth <= 0 || clientHeight <= 0) return;
	if (clientWidth == client_w_ && clientHeight == client_h_) return;

	client_w_ = clientWidth;
	client_h_ = clientHeight;

	back_rtv_ = nullptr;
	context_->OMSetRenderTargets(0, nullptr, nullptr);
	swap_chain_->ResizeBuffers(0, client_w_, client_h_, DXGI_FORMAT_UNKNOWN, 0);
	createBackBuffer();

	/* In full-window mode the canvas tracks the client area, so grow/shrink it
	 * with the window. (Retro mode keeps its fixed 512x514 canvas, letterboxed.) */
	if (canvas_full_)
		recreateCanvas(client_w_, client_h_);
}
