/*
 * DeepspaceOutpost - DirectX 11 / XAudio2 port of Elite: The New Kind.
 *
 * Renderer.cpp  (M1)
 */

#include "Renderer.h"

#include <d3dcompiler.h>
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <iterator>
#include <vector>

using Microsoft::WRL::ComPtr;

/* scanner.bmp supplies both the HUD strip and the master 256-colour palette.
 * Its filename is read from newscan.cfg into this game-side global. */
extern char scanner_filename[256];

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

ComPtr<ID3DBlob> compileShader(const char* src, const char* entry, const char* target)
{
	ComPtr<ID3DBlob> code, errors;
	UINT flags = D3DCOMPILE_ENABLE_STRICTNESS;
#ifdef _DEBUG
	flags |= D3DCOMPILE_DEBUG | D3DCOMPILE_SKIP_OPTIMIZATION;
#endif
	HRESULT hr = D3DCompile(src, std::strlen(src), nullptr, nullptr, nullptr,
							entry, target, flags, 0, &code, &errors);
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
	context_->ClearRenderTargetView(canvas_rtv_.Get(), black);
	return true;
}

void Renderer::shutdown()
{
	if (context_) context_->ClearState();
	/* ComPtr members release automatically. */
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
		&sd, &swap_chain_, &device_, &got, &context_);

	if (FAILED(hr))
	{
		/* Retry without the FLIP model for older configurations. */
		sd.BufferCount = 1;
		sd.SwapEffect  = DXGI_SWAP_EFFECT_DISCARD;
		hr = D3D11CreateDeviceAndSwapChain(
			nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, 0,
			wanted, static_cast<UINT>(std::size(wanted)), D3D11_SDK_VERSION,
			&sd, &swap_chain_, &device_, &got, &context_);
	}

	return SUCCEEDED(hr);
}

bool Renderer::createBackBuffer()
{
	ComPtr<ID3D11Texture2D> back;
	if (FAILED(swap_chain_->GetBuffer(0, IID_PPV_ARGS(&back))))
		return false;
	return SUCCEEDED(device_->CreateRenderTargetView(back.Get(), nullptr, &back_rtv_));
}

bool Renderer::createCanvasTarget()
{
	D3D11_TEXTURE2D_DESC td{};
	td.Width      = kCanvasWidth;
	td.Height     = kCanvasHeight;
	td.MipLevels  = 1;
	td.ArraySize  = 1;
	td.Format     = DXGI_FORMAT_R8G8B8A8_UNORM;
	td.SampleDesc.Count = 1;
	td.Usage      = D3D11_USAGE_DEFAULT;
	td.BindFlags  = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;

	if (FAILED(device_->CreateTexture2D(&td, nullptr, &canvas_tex_)))   return false;
	if (FAILED(device_->CreateRenderTargetView(canvas_tex_.Get(), nullptr, &canvas_rtv_))) return false;
	if (FAILED(device_->CreateShaderResourceView(canvas_tex_.Get(), nullptr, &canvas_srv_))) return false;
	return true;
}

bool Renderer::createPresentPipeline()
{
	ComPtr<ID3DBlob> vs = compileShader(kPresentHLSL, "VSMain", "vs_5_0");
	ComPtr<ID3DBlob> ps = compileShader(kPresentHLSL, "PSMain", "ps_5_0");
	if (!vs || !ps) return false;

	if (FAILED(device_->CreateVertexShader(vs->GetBufferPointer(), vs->GetBufferSize(), nullptr, &present_vs_)))
		return false;
	if (FAILED(device_->CreatePixelShader(ps->GetBufferPointer(), ps->GetBufferSize(), nullptr, &present_ps_)))
		return false;

	D3D11_SAMPLER_DESC sm{};
	sm.Filter   = D3D11_FILTER_MIN_MAG_MIP_POINT;   /* crisp pixel art */
	sm.AddressU = sm.AddressV = sm.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
	sm.ComparisonFunc = D3D11_COMPARISON_NEVER;
	sm.MaxLOD = D3D11_FLOAT32_MAX;
	if (FAILED(device_->CreateSamplerState(&sm, &present_sampler_)))
		return false;

	/* No culling (the fullscreen triangle winding is arbitrary) and scissor
	 * disabled, so the canvas batch's scissor state can't clip the present. */
	D3D11_RASTERIZER_DESC rs{};
	rs.FillMode = D3D11_FILL_SOLID;
	rs.CullMode = D3D11_CULL_NONE;
	rs.ScissorEnable = FALSE;
	rs.DepthClipEnable = TRUE;
	return SUCCEEDED(device_->CreateRasterizerState(&rs, &present_raster_));
}

bool Renderer::loadPalette()
{
	palette_loaded_ = false;

	const char* fname = (scanner_filename[0] != '\0') ? scanner_filename : "scanner.bmp";

	FILE* fp = std::fopen(fname, "rb");
	if (!fp)
	{
		/* Fallback: greyscale ramp so the build is still usable. */
		for (int i = 0; i < 256; i++)
			palette_[i] = static_cast<uint32_t>(i) * 0x00010101u | 0xFF000000u;
		palette_[0] = 0x00000000u;
		return false;
	}

	std::vector<uint8_t> buf;
	std::fseek(fp, 0, SEEK_END);
	long sz = std::ftell(fp);
	std::fseek(fp, 0, SEEK_SET);
	if (sz > 0)
	{
		buf.resize(static_cast<size_t>(sz));
		if (std::fread(buf.data(), 1, buf.size(), fp) != buf.size())
			buf.clear();
	}
	std::fclose(fp);

	/* BITMAPFILEHEADER (14) + BITMAPINFOHEADER (>=40). Palette follows the
	 * info header; entries are RGBQUAD (B,G,R,reserved). */
	if (buf.size() >= 54 && buf[0] == 'B' && buf[1] == 'M')
	{
		auto u32 = [&](size_t off) {
			return static_cast<uint32_t>(buf[off]) | (buf[off + 1] << 8) |
			       (buf[off + 2] << 16) | (buf[off + 3] << 24);
		};
		uint32_t infoSize = u32(14);
		uint16_t bpp      = static_cast<uint16_t>(buf[28] | (buf[29] << 8));
		uint32_t clrUsed  = u32(46);
		if (bpp == 8)
		{
			uint32_t count = clrUsed ? clrUsed : 256;
			count = std::min<uint32_t>(count, 256);
			size_t pal = 14 + infoSize;
			if (pal + count * 4 <= buf.size())
			{
				for (uint32_t i = 0; i < 256; i++)
					palette_[i] = 0xFF000000u;   /* default opaque black */
				for (uint32_t i = 0; i < count; i++)
				{
					uint8_t b = buf[pal + i * 4 + 0];
					uint8_t g = buf[pal + i * 4 + 1];
					uint8_t r = buf[pal + i * 4 + 2];
					palette_[i] = static_cast<uint32_t>(r) |
					              (static_cast<uint32_t>(g) << 8) |
					              (static_cast<uint32_t>(b) << 16) |
					              0xFF000000u;
				}
				palette_[0] &= 0x00FFFFFFu;   /* index 0 = transparent key */
				palette_loaded_ = true;
			}
		}
	}

	if (!palette_loaded_)
	{
		for (int i = 0; i < 256; i++)
			palette_[i] = static_cast<uint32_t>(i) * 0x00010101u | 0xFF000000u;
		palette_[0] = 0x00000000u;
	}
	return palette_loaded_;
}

void Renderer::computeLetterbox(D3D11_VIEWPORT& vp) const
{
	int scale = std::min(client_w_ / kCanvasWidth, client_h_ / kCanvasHeight);
	if (scale < 1) scale = 1;   /* tiny window: crop rather than vanish */

	int dstW = kCanvasWidth  * scale;
	int dstH = kCanvasHeight * scale;

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
	context_->ClearRenderTargetView(canvas_rtv_.Get(), rgba);
}

void Renderer::bindCanvasTarget()
{
	ID3D11RenderTargetView* rtv = canvas_rtv_.Get();
	context_->OMSetRenderTargets(1, &rtv, nullptr);

	D3D11_VIEWPORT vp{};
	vp.TopLeftX = 0.0f;
	vp.TopLeftY = 0.0f;
	vp.Width    = static_cast<float>(kCanvasWidth);
	vp.Height   = static_cast<float>(kCanvasHeight);
	vp.MinDepth = 0.0f;
	vp.MaxDepth = 1.0f;
	context_->RSSetViewports(1, &vp);
}

void Renderer::present()
{
	if (!swap_chain_) return;

	const float black[4] = { 0.0f, 0.0f, 0.0f, 1.0f };
	ID3D11RenderTargetView* rtv = back_rtv_.Get();
	context_->OMSetRenderTargets(1, &rtv, nullptr);
	context_->ClearRenderTargetView(rtv, black);

	D3D11_VIEWPORT vp{};
	computeLetterbox(vp);
	context_->RSSetViewports(1, &vp);
	context_->RSSetState(present_raster_.Get());

	context_->IASetInputLayout(nullptr);
	context_->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
	context_->VSSetShader(present_vs_.Get(), nullptr, 0);
	context_->PSSetShader(present_ps_.Get(), nullptr, 0);
	ID3D11ShaderResourceView* srv = canvas_srv_.Get();
	ID3D11SamplerState* smp = present_sampler_.Get();
	context_->PSSetShaderResources(0, 1, &srv);
	context_->PSSetSamplers(0, 1, &smp);
	context_->Draw(3, 0);

	/* Unbind the canvas SRV so it can be used as an RT again next frame. */
	ID3D11ShaderResourceView* nullsrv = nullptr;
	context_->PSSetShaderResources(0, 1, &nullsrv);

	swap_chain_->Present(1, 0);
}

void Renderer::resize(int clientWidth, int clientHeight)
{
	if (!swap_chain_) return;
	if (clientWidth <= 0 || clientHeight <= 0) return;
	if (clientWidth == client_w_ && clientHeight == client_h_) return;

	client_w_ = clientWidth;
	client_h_ = clientHeight;

	back_rtv_.Reset();
	context_->OMSetRenderTargets(0, nullptr, nullptr);
	swap_chain_->ResizeBuffers(0, client_w_, client_h_, DXGI_FORMAT_UNKNOWN, 0);
	createBackBuffer();
}
