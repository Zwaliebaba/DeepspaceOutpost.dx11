/*
 * DeepspaceOutpost - DirectX 11 / XAudio2.
 *
 * gfx_dx11.cpp  (M2 + M3)
 *
 * The gfx.h 2D contract on a Direct3D 11 batch renderer. Two vertex streams
 * (solid-colour and textured) feed a single submission-order command list, so
 * lines/polygons, sprites, the HUD bitmap and text all composite in the exact
 * order the game draws them. The batch replays into the persistent 512x514
 * canvas in gfx_dx11_flush(); Renderer::present() then blits it to the window.
 *
 * Colours are palette indices resolved against scanner.bmp. Solid primitives
 * are opaque (index 0 -> opaque black); sprites colour-key index 0 to
 * transparent. Text uses the verd2/verd4 font sheets (ELITE_1 tinted, ELITE_2
 * colour).
 */

#include "pch.h"

#include "Renderer.h"
#include "gfx_dx11.h"
#include "Image.h"
#include "Font.h"

#include "compat.h"
#include "gfx.h"

#include <d3d11.h>
#include <d3d11_1.h>
#include <d3dcompiler.h>
#include <winrt/base.h>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <map>
#include <string>
#include <vector>

using winrt::com_ptr;

extern char scanner_filename[256];

namespace {

struct ColorVertex { float x, y;          uint32_t rgba; };
struct TexVertex   { float x, y, u, v;    uint32_t rgba; };

enum class Kind { Color, Tex };
enum class Topo { Points, Lines, Tris };

struct Cmd
{
	Kind                      kind;
	Topo                      topo;
	uint32_t                  start;
	uint32_t                  count;
	D3D11_RECT                scissor;
	ID3D11ShaderResourceView* srv;     /* Tex only */
	bool                      xorop;   /* Color: draw via XOR logic op (cross-hairs) */
};

struct Texture
{
	com_ptr<ID3D11ShaderResourceView> srv;
	int w = 0, h = 0;
};

const char* kColorHLSL = R"(
cbuffer Cb : register(b0) { float2 gInvSize; float2 pad; };
struct VSIn  { float2 pos : POSITION; float4 col : COLOR; };
struct VSOut { float4 pos : SV_Position; float4 col : COLOR; };
VSOut VSMain(VSIn i){ VSOut o; float2 p=i.pos*gInvSize; o.pos=float4(p.x*2-1,1-p.y*2,0,1); o.col=i.col; return o; }
float4 PSMain(VSOut i):SV_Target { return i.col; }
)";

const char* kTexHLSL = R"(
cbuffer Cb : register(b0) { float2 gInvSize; float2 pad; };
Texture2D gTex : register(t0); SamplerState gSamp : register(s0);
struct VSIn  { float2 pos : POSITION; float2 uv : TEXCOORD0; float4 col : COLOR; };
struct VSOut { float4 pos : SV_Position; float2 uv : TEXCOORD0; float4 col : COLOR; };
VSOut VSMain(VSIn i){ VSOut o; float2 p=i.pos*gInvSize; o.pos=float4(p.x*2-1,1-p.y*2,0,1); o.uv=i.uv; o.col=i.col; return o; }
float4 PSMain(VSOut i):SV_Target { return gTex.Sample(gSamp,i.uv) * i.col; }
)";

/* ---- batch state ---- */
std::vector<ColorVertex> g_cverts;
std::vector<TexVertex>   g_tverts;
std::vector<Cmd>         g_cmds;
D3D11_RECT               g_scissor  = { 0, 0, Renderer::kCanvasWidth, Renderer::kCanvasHeight };
bool                     g_xor_mode = false;

/* ---- D3D resources (lazy) ---- */
bool                          g_inited = false;
com_ptr<ID3D11VertexShader>    g_cvs, g_tvs;
com_ptr<ID3D11PixelShader>     g_cps, g_tps;
com_ptr<ID3D11InputLayout>     g_clayout, g_tlayout;
com_ptr<ID3D11Buffer>          g_cb, g_cvb, g_tvb;
com_ptr<ID3D11RasterizerState> g_raster;
com_ptr<ID3D11BlendState>      g_blend_opaque, g_blend_alpha;
com_ptr<ID3D11BlendState1>     g_blend_xor;   /* logic-op XOR for cross-hairs (may be null) */
com_ptr<ID3D11SamplerState>    g_sampler;
size_t                        g_cvb_cap = 0, g_tvb_cap = 0;

std::map<std::string, Texture> g_textures;
Font     g_font[2];          /* 0 = ELITE_1 (mono), 1 = ELITE_2 (colour) */
Texture  g_font_tex[2];
bool     g_fonts_ready = false;

inline uint32_t col_rgba(int index)
{
	Renderer* r = platform_renderer();
	uint32_t c = r ? r->paletteColour(index) : 0xFFFFFFFFu;
	return c | 0xFF000000u;
}

inline bool sameRect(const D3D11_RECT& a, const D3D11_RECT& b)
{
	return a.left == b.left && a.top == b.top && a.right == b.right && a.bottom == b.bottom;
}

void pushColor(Topo topo, const ColorVertex* v, int n)
{
	if (!g_cmds.empty())
	{
		Cmd& b = g_cmds.back();
		if (b.kind == Kind::Color && b.topo == topo && b.xorop == g_xor_mode && sameRect(b.scissor, g_scissor))
		{
			b.count += n;
			g_cverts.insert(g_cverts.end(), v, v + n);
			return;
		}
	}
	g_cmds.push_back({ Kind::Color, topo, static_cast<uint32_t>(g_cverts.size()),
					   static_cast<uint32_t>(n), g_scissor, nullptr, g_xor_mode });
	g_cverts.insert(g_cverts.end(), v, v + n);
}

void pushTexQuad(ID3D11ShaderResourceView* srv,
				 float x0, float y0, float x1, float y1,
				 float u0, float v0, float u1, float v1, uint32_t tint)
{
	TexVertex q[6] = {
		{ x0, y0, u0, v0, tint }, { x1, y0, u1, v0, tint }, { x1, y1, u1, v1, tint },
		{ x0, y0, u0, v0, tint }, { x1, y1, u1, v1, tint }, { x0, y1, u0, v1, tint },
	};
	if (!g_cmds.empty())
	{
		Cmd& b = g_cmds.back();
		if (b.kind == Kind::Tex && b.srv == srv && sameRect(b.scissor, g_scissor))
		{
			b.count += 6;
			g_tverts.insert(g_tverts.end(), q, q + 6);
			return;
		}
	}
	g_cmds.push_back({ Kind::Tex, Topo::Tris, static_cast<uint32_t>(g_tverts.size()),
					   6, g_scissor, srv, false });
	g_tverts.insert(g_tverts.end(), q, q + 6);
}

void addPoint(int x, int y, uint32_t c) { ColorVertex v{ x + 0.5f, y + 0.5f, c }; pushColor(Topo::Points, &v, 1); }
void addSegment(int x1, int y1, int x2, int y2, uint32_t c)
{
	ColorVertex v[2] = { { x1 + 0.5f, y1 + 0.5f, c }, { x2 + 0.5f, y2 + 0.5f, c } };
	pushColor(Topo::Lines, v, 2);
}
void addRect(int x1, int y1, int x2, int y2, uint32_t c)
{
	if (x2 < x1) std::swap(x1, x2);
	if (y2 < y1) std::swap(y1, y2);
	float l = (float)x1, t = (float)y1, r = (float)(x2 + 1), b = (float)(y2 + 1);
	ColorVertex q[6] = { {l,t,c},{r,t,c},{r,b,c}, {l,t,c},{r,b,c},{l,b,c} };
	pushColor(Topo::Tris, q, 6);
}
void addTri(int x1, int y1, int x2, int y2, int x3, int y3, uint32_t c)
{
	ColorVertex t[3] = { {x1+0.5f,y1+0.5f,c},{x2+0.5f,y2+0.5f,c},{x3+0.5f,y3+0.5f,c} };
	pushColor(Topo::Tris, t, 3);
}
void drawLine(int x1, int y1, int x2, int y2, uint32_t c)
{
	if (y1 == y2)      addRect(x1, y1, x2, y1, c);
	else if (x1 == x2) addRect(x1, y1, x1, y2, c);
	else               addSegment(x1, y1, x2, y2, c);
}

/* ---- texture helpers ---- */
Texture makeTexture(ID3D11Device* dev, const Image& img)
{
	Texture t;
	if (!img.ok()) return t;
	D3D11_TEXTURE2D_DESC td{};
	td.Width = img.width; td.Height = img.height; td.MipLevels = 1; td.ArraySize = 1;
	td.Format = DXGI_FORMAT_R8G8B8A8_UNORM; td.SampleDesc.Count = 1;
	td.Usage = D3D11_USAGE_IMMUTABLE; td.BindFlags = D3D11_BIND_SHADER_RESOURCE;
	D3D11_SUBRESOURCE_DATA sd{};
	sd.pSysMem = img.rgba.data();
	sd.SysMemPitch = img.width * 4;
	com_ptr<ID3D11Texture2D> tex;
	if (FAILED(dev->CreateTexture2D(&td, &sd, tex.put()))) return t;
	if (FAILED(dev->CreateShaderResourceView(tex.get(), nullptr, t.srv.put()))) return t;
	t.w = img.width; t.h = img.height;
	return t;
}

const Texture* getTexture(const char* path, bool key_index0)
{
	auto it = g_textures.find(path);
	if (it != g_textures.end())
		return &it->second;
	Renderer* r = platform_renderer();
	if (!r) return nullptr;
	Image img = load_image_rgba(path, key_index0);
	Texture t = makeTexture(r->device(), img);
	auto res = g_textures.emplace(path, std::move(t));
	return &res.first->second;
}

const char* spriteFile(int sprite_no)
{
	switch (sprite_no)
	{
		case IMG_GREEN_DOT:      return "greendot.bmp";
		case IMG_RED_DOT:        return "reddot.bmp";
		case IMG_BIG_S:          return "safe.bmp";
		case IMG_ELITE_TXT:      return "elitetx3.bmp";
		case IMG_BIG_E:          return "ecm.bmp";
		case IMG_MISSILE_GREEN:  return "missgrn.bmp";
		case IMG_MISSILE_YELLOW: return "missyell.bmp";
		case IMG_MISSILE_RED:    return "missred.bmp";
		case IMG_BLAKE:          return "blake.bmp";
		case IMG_TARGET_LOCK:    return "Textures/TargetLock.dds";
		default:                 return nullptr;
	}
}

void ensureFonts(ID3D11Device* dev)
{
	if (g_fonts_ready) return;
	g_fonts_ready = true;   /* attempt once */
	g_font[0].load("verd2.pcx", false);
	g_font[1].load("verd4.pcx", true);
	for (int i = 0; i < 2; i++)
		if (g_font[i].loaded())
			g_font_tex[i] = makeTexture(dev, g_font[i].atlas());
}

void drawString(int which, int x, int y, const char* s, uint32_t tint)
{
	Renderer* r = platform_renderer();
	if (!r || !s) return;
	ensureFonts(r->device());
	Font& f = g_font[which];
	if (!f.loaded() || !g_font_tex[which].srv)
		return;

	float aw = (float)g_font_tex[which].w;
	float ah = (float)g_font_tex[which].h;
	int pen = x;
	for (; *s; s++)
	{
		const Glyph& g = f.glyph((unsigned char)*s);
		if (*s != ' ' && g.w > 0 && g.h > 0)
		{
			pushTexQuad(g_font_tex[which].srv.get(),
						(float)pen, (float)y, (float)(pen + g.w), (float)(y + g.h),
						g.x / aw, g.y / ah, (g.x + g.w) / aw, (g.y + g.h) / ah, tint);
		}
		pen += g.w + 1;
	}
}

/* ---- depth-sorted 3D render chain (ported from alg_gfx.c) ---- */
constexpr int MAX_POLYS = 100;
struct PolyData { int z, no_points, face_colour, point_list[16], next; };
PolyData g_poly_chain[MAX_POLYS];
int g_start_poly = 0, g_total_polys = 0;

com_ptr<ID3DBlob> compile(const char* src, const char* entry, const char* target)
{
	UINT flags = D3DCOMPILE_ENABLE_STRICTNESS;
#ifdef _DEBUG
	flags |= D3DCOMPILE_DEBUG | D3DCOMPILE_SKIP_OPTIMIZATION;
#endif
	com_ptr<ID3DBlob> code, err;
	D3DCompile(src, std::strlen(src), nullptr, nullptr, nullptr, entry, target, flags, 0, code.put(), err.put());
	return code;
}

void initD3D(ID3D11Device* dev)
{
	com_ptr<ID3DBlob> cvs = compile(kColorHLSL, "VSMain", "vs_5_0");
	com_ptr<ID3DBlob> cps = compile(kColorHLSL, "PSMain", "ps_5_0");
	com_ptr<ID3DBlob> tvs = compile(kTexHLSL,   "VSMain", "vs_5_0");
	com_ptr<ID3DBlob> tps = compile(kTexHLSL,   "PSMain", "ps_5_0");
	if (!cvs || !cps || !tvs || !tps) return;

	dev->CreateVertexShader(cvs->GetBufferPointer(), cvs->GetBufferSize(), nullptr, g_cvs.put());
	dev->CreatePixelShader (cps->GetBufferPointer(), cps->GetBufferSize(), nullptr, g_cps.put());
	dev->CreateVertexShader(tvs->GetBufferPointer(), tvs->GetBufferSize(), nullptr, g_tvs.put());
	dev->CreatePixelShader (tps->GetBufferPointer(), tps->GetBufferSize(), nullptr, g_tps.put());

	const D3D11_INPUT_ELEMENT_DESC ce[] = {
		{ "POSITION", 0, DXGI_FORMAT_R32G32_FLOAT,  0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0 },
		{ "COLOR",    0, DXGI_FORMAT_R8G8B8A8_UNORM, 0, 8, D3D11_INPUT_PER_VERTEX_DATA, 0 },
	};
	dev->CreateInputLayout(ce, 2, cvs->GetBufferPointer(), cvs->GetBufferSize(), g_clayout.put());

	const D3D11_INPUT_ELEMENT_DESC te[] = {
		{ "POSITION", 0, DXGI_FORMAT_R32G32_FLOAT,   0, 0,  D3D11_INPUT_PER_VERTEX_DATA, 0 },
		{ "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT,   0, 8,  D3D11_INPUT_PER_VERTEX_DATA, 0 },
		{ "COLOR",    0, DXGI_FORMAT_R8G8B8A8_UNORM, 0, 16, D3D11_INPUT_PER_VERTEX_DATA, 0 },
	};
	dev->CreateInputLayout(te, 3, tvs->GetBufferPointer(), tvs->GetBufferSize(), g_tlayout.put());

	struct Cb { float invW, invH, p0, p1; }
	cb{ 1.0f / Renderer::kCanvasWidth, 1.0f / Renderer::kCanvasHeight, 0, 0 };
	D3D11_BUFFER_DESC cbd{};
	cbd.ByteWidth = sizeof(Cb); cbd.Usage = D3D11_USAGE_IMMUTABLE; cbd.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
	D3D11_SUBRESOURCE_DATA cbi{}; cbi.pSysMem = &cb;
	dev->CreateBuffer(&cbd, &cbi, g_cb.put());

	D3D11_RASTERIZER_DESC rs{};
	rs.FillMode = D3D11_FILL_SOLID; rs.CullMode = D3D11_CULL_NONE;
	rs.ScissorEnable = TRUE; rs.DepthClipEnable = TRUE;
	dev->CreateRasterizerState(&rs, g_raster.put());

	D3D11_BLEND_DESC bo{};
	bo.RenderTarget[0].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
	dev->CreateBlendState(&bo, g_blend_opaque.put());

	D3D11_BLEND_DESC ba{};
	ba.RenderTarget[0].BlendEnable = TRUE;
	ba.RenderTarget[0].SrcBlend = D3D11_BLEND_SRC_ALPHA;
	ba.RenderTarget[0].DestBlend = D3D11_BLEND_INV_SRC_ALPHA;
	ba.RenderTarget[0].BlendOp = D3D11_BLEND_OP_ADD;
	ba.RenderTarget[0].SrcBlendAlpha = D3D11_BLEND_ONE;
	ba.RenderTarget[0].DestBlendAlpha = D3D11_BLEND_INV_SRC_ALPHA;
	ba.RenderTarget[0].BlendOpAlpha = D3D11_BLEND_OP_ADD;
	ba.RenderTarget[0].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
	dev->CreateBlendState(&ba, g_blend_alpha.put());

	/* XOR logic-op blend for the chart cross-hairs (draw then erase). Requires
	 * D3D11.1 + OutputMergerLogicOp; if unsupported g_blend_xor stays null and
	 * cross-hairs fall back to opaque draw. Alpha is preserved (RGB write mask
	 * only) so the canvas stays opaque. */
	com_ptr<ID3D11Device1> dev1;
	if (SUCCEEDED(dev->QueryInterface(IID_PPV_ARGS(dev1.put()))))
	{
		D3D11_FEATURE_DATA_D3D11_OPTIONS opt{};
		if (SUCCEEDED(dev->CheckFeatureSupport(D3D11_FEATURE_D3D11_OPTIONS, &opt, sizeof(opt))) &&
			opt.OutputMergerLogicOp)
		{
			D3D11_BLEND_DESC1 bx{};
			bx.RenderTarget[0].LogicOpEnable = TRUE;
			bx.RenderTarget[0].LogicOp = D3D11_LOGIC_OP_XOR;
			bx.RenderTarget[0].RenderTargetWriteMask =
				D3D11_COLOR_WRITE_ENABLE_RED | D3D11_COLOR_WRITE_ENABLE_GREEN | D3D11_COLOR_WRITE_ENABLE_BLUE;
			dev1->CreateBlendState1(&bx, g_blend_xor.put());
		}
	}

	D3D11_SAMPLER_DESC sm{};
	sm.Filter = D3D11_FILTER_MIN_MAG_MIP_POINT;
	sm.AddressU = sm.AddressV = sm.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
	sm.ComparisonFunc = D3D11_COMPARISON_NEVER; sm.MaxLOD = D3D11_FLOAT32_MAX;
	dev->CreateSamplerState(&sm, g_sampler.put());

	g_inited = true;
}

bool ensureBuf(ID3D11Device* dev, com_ptr<ID3D11Buffer>& buf, size_t& cap, size_t needBytes)
{
	if (buf && needBytes <= cap) return true;
	size_t c = cap ? cap : 65536;
	while (c < needBytes) c *= 2;
	D3D11_BUFFER_DESC bd{};
	bd.ByteWidth = (UINT)c; bd.Usage = D3D11_USAGE_DYNAMIC;
	bd.BindFlags = D3D11_BIND_VERTEX_BUFFER; bd.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
	buf = nullptr;
	if (FAILED(dev->CreateBuffer(&bd, nullptr, buf.put()))) return false;
	cap = c;
	return true;
}

void upload(ID3D11DeviceContext* ctx, ID3D11Buffer* buf, const void* data, size_t bytes)
{
	if (bytes == 0) return;
	D3D11_MAPPED_SUBRESOURCE m{};
	if (SUCCEEDED(ctx->Map(buf, 0, D3D11_MAP_WRITE_DISCARD, 0, &m)))
	{
		std::memcpy(m.pData, data, bytes);
		ctx->Unmap(buf, 0);
	}
}

} // namespace

/* =====================================================================
 *  gfx.h primitives
 * ===================================================================== */

void gfx_plot_pixel(int x, int y, int col)      { addPoint(x, y, col_rgba(col)); }
void gfx_fast_plot_pixel(int x, int y, int col) { addPoint(x, y, col_rgba(col)); }
void gfx_draw_line(int x1, int y1, int x2, int y2)               { drawLine(x1, y1, x2, y2, col_rgba(GFX_COL_WHITE)); }
void gfx_draw_colour_line(int x1, int y1, int x2, int y2, int c) { drawLine(x1, y1, x2, y2, col_rgba(c)); }
void gfx_draw_triangle(int x1, int y1, int x2, int y2, int x3, int y3, int c) { addTri(x1,y1,x2,y2,x3,y3,col_rgba(c)); }
void gfx_draw_rectangle(int tx, int ty, int bx, int by, int c)   { addRect(tx, ty, bx, by, col_rgba(c)); }
void gfx_clear_display(void)   { addRect(1, 1, 510, 383, col_rgba(GFX_COL_BLACK)); }
void gfx_clear_text_area(void) { addRect(1, 340, 510, 383, col_rgba(GFX_COL_BLACK)); }
void gfx_clear_area(int tx, int ty, int bx, int by) { addRect(tx, ty, bx, by, col_rgba(GFX_COL_BLACK)); }

void gfx_draw_circle(int cx, int cy, int radius, int col)
{
	if (radius < 1) { addPoint(cx, cy, col_rgba(col)); return; }
	uint32_t c = col_rgba(col);
	int seg = radius * 2; if (seg < 12) seg = 12; if (seg > 96) seg = 96;
	int px = cx + radius, py = cy;
	for (int i = 1; i <= seg; i++)
	{
		double a = 6.28318530718 * i / seg;
		int nx = cx + (int)std::lround(std::cos(a) * radius);
		int ny = cy + (int)std::lround(std::sin(a) * radius);
		addSegment(px, py, nx, ny, c); px = nx; py = ny;
	}
}

void gfx_draw_filled_circle(int cx, int cy, int radius, int col)
{
	if (radius < 1) { addPoint(cx, cy, col_rgba(col)); return; }
	uint32_t c = col_rgba(col);
	int seg = radius * 2; if (seg < 12) seg = 12; if (seg > 96) seg = 96;
	int px = cx + radius, py = cy;
	for (int i = 1; i <= seg; i++)
	{
		double a = 6.28318530718 * i / seg;
		int nx = cx + (int)std::lround(std::cos(a) * radius);
		int ny = cy + (int)std::lround(std::sin(a) * radius);
		addTri(cx, cy, px, py, nx, ny, c); px = nx; py = ny;
	}
}

void gfx_polygon(int num_points, int* poly_list, int face_colour)
{
	if (num_points < 3) return;
	uint32_t c = col_rgba(face_colour);
	int x0 = poly_list[0], y0 = poly_list[1];
	for (int i = 1; i < num_points - 1; i++)
		addTri(x0, y0, poly_list[i*2], poly_list[i*2+1], poly_list[i*2+2], poly_list[i*2+3], c);
}

void gfx_set_clip_region(int tx, int ty, int bx, int by)
{
	LONG l = tx, t = ty, r = bx + 1, b = by + 1;
	if (l < 0) l = 0; if (t < 0) t = 0;
	if (r > Renderer::kCanvasWidth)  r = Renderer::kCanvasWidth;
	if (b > Renderer::kCanvasHeight) b = Renderer::kCanvasHeight;
	g_scissor = { l, t, r, b };
}

void xor_mode(int on) { g_xor_mode = (on != 0); }

/* ---- text ---- */
void gfx_display_text(int x, int y, const char* txt)
{
	drawString(0, x, y, txt, col_rgba(GFX_COL_WHITE));
}
void gfx_display_colour_text(int x, int y, const char* txt, int col)
{
	drawString(0, x, y, txt, col_rgba(col));
}
void gfx_display_centre_text(int y, const char* str, int psize, int col)
{
	if (psize == 140)   /* ELITE_2 colour title font, drawn in its own colours */
	{
		int w = g_font[1].loaded() ? g_font[1].textWidth(str) : 0;
		drawString(1, 256 - w / 2, y, str, 0xFFFFFFFFu);
	}
	else                /* ELITE_1 body font, tinted */
	{
		int w = g_font[0].loaded() ? g_font[0].textWidth(str) : 0;
		drawString(0, 256 - w / 2, y, str, col_rgba(col));
	}
}

void gfx_display_pretty_text(int tx, int ty, int bx, int /*by*/, const char* txt)
{
	/* 'by' (bottom bound) is unused: the original wraps by width only. */
	char strbuf[100];
	const char* str = txt;
	int   len = (int)std::strlen(txt);
	int   maxlen = (bx - tx) / 8;
	if (maxlen <= 0) maxlen = 1;

	while (len > 0)
	{
		int pos = maxlen;
		if (pos > len) pos = len;
		while (pos > 0 && str[pos] != ' ' && str[pos] != ',' && str[pos] != '.' && str[pos] != '\0')
			pos--;
		if (pos <= 0) pos = (len < maxlen) ? len : maxlen;

		len = len - pos - 1;
		char* bp = strbuf;
		for (int i = 0; i <= pos; i++)
			*bp++ = *str++;
		*bp = '\0';

		drawString(0, tx, ty, strbuf, col_rgba(GFX_COL_WHITE));
		ty += 8 * GFX_SCALE;
	}
}

/* ---- sprites / HUD ---- */
void gfx_draw_sprite(int sprite_no, int x, int y)
{
	const char* fn = spriteFile(sprite_no);
	if (!fn) return;
	const Texture* t = getTexture(fn, true);
	if (!t || !t->srv) return;
	if (x == -1) x = (256 * GFX_SCALE - t->w) / 2;
	pushTexQuad(t->srv.get(), (float)x, (float)y, (float)(x + t->w), (float)(y + t->h),
				0.0f, 0.0f, 1.0f, 1.0f, 0xFFFFFFFFu);
}

/* As gfx_draw_sprite but stretched to an explicit w x h (used for the missile
 * target reticle, which is sized to the locked ship's on-screen extent). */
void gfx_draw_sprite_scaled(int sprite_no, int x, int y, int w, int h)
{
	const char* fn = spriteFile(sprite_no);
	if (!fn) return;
	const Texture* t = getTexture(fn, true);
	if (!t || !t->srv) return;
	pushTexQuad(t->srv.get(), (float)x, (float)y, (float)(x + w), (float)(y + h),
				0.0f, 0.0f, 1.0f, 1.0f, 0xFFFFFFFFu);
}

void gfx_draw_scanner(void)
{
	const char* fn = (scanner_filename[0] != '\0') ? scanner_filename : "scanner.bmp";
	const Texture* t = getTexture(fn, false);
	if (!t || !t->srv) return;
	pushTexQuad(t->srv.get(), 0.0f, 385.0f, (float)t->w, (float)(385 + t->h),
				0.0f, 0.0f, 1.0f, 1.0f, 0xFFFFFFFFu);
}

/* gfx_request_file lives in dialog_win.cpp (Win32 common dialog). */

/* ---- depth-sorted render chain ---- */
void gfx_start_render(void) { g_start_poly = 0; g_total_polys = 0; }

void gfx_render_polygon(int num_points, int* point_list, int face_colour, int zavg)
{
	if (g_total_polys == MAX_POLYS) return;
	int x = g_total_polys++;
	g_poly_chain[x].no_points = num_points;
	g_poly_chain[x].face_colour = face_colour;
	g_poly_chain[x].z = zavg;
	g_poly_chain[x].next = -1;
	for (int i = 0; i < 16; i++) g_poly_chain[x].point_list[i] = point_list[i];
	if (x == 0) return;
	if (zavg > g_poly_chain[g_start_poly].z) { g_poly_chain[x].next = g_start_poly; g_start_poly = x; return; }
	int i = g_start_poly;
	for (; g_poly_chain[i].next != -1; i = g_poly_chain[i].next)
	{
		int nx = g_poly_chain[i].next;
		if (zavg > g_poly_chain[nx].z) { g_poly_chain[i].next = x; g_poly_chain[x].next = nx; return; }
	}
	g_poly_chain[i].next = x;
}

void gfx_render_line(int x1, int y1, int x2, int y2, int dist, int col)
{
	int pl[4] = { x1, y1, x2, y2 };
	gfx_render_polygon(2, pl, col, dist);
}

void gfx_finish_render(void)
{
	if (g_total_polys == 0) return;
	for (int i = g_start_poly; i != -1; i = g_poly_chain[i].next)
	{
		int n = g_poly_chain[i].no_points;
		int* pl = g_poly_chain[i].point_list;
		int col = g_poly_chain[i].face_colour;
		if (n == 2) gfx_draw_colour_line(pl[0], pl[1], pl[2], pl[3], col);
		else        gfx_polygon(n, pl, col);
	}
}

/* =====================================================================
 *  Flush
 * ===================================================================== */
void gfx_dx11_flush(void)
{
	Renderer* r = platform_renderer();
	if (!r) { g_cverts.clear(); g_tverts.clear(); g_cmds.clear(); return; }

	ID3D11Device*        dev = r->device();
	ID3D11DeviceContext* ctx = r->context();
	if (!g_inited) initD3D(dev);

	if (g_inited && !g_cmds.empty())
	{
		ensureBuf(dev, g_cvb, g_cvb_cap, g_cverts.size() * sizeof(ColorVertex));
		ensureBuf(dev, g_tvb, g_tvb_cap, g_tverts.size() * sizeof(TexVertex));
		upload(ctx, g_cvb.get(), g_cverts.data(), g_cverts.size() * sizeof(ColorVertex));
		upload(ctx, g_tvb.get(), g_tverts.data(), g_tverts.size() * sizeof(TexVertex));

		r->bindCanvasTarget();
		ID3D11Buffer* cb = g_cb.get();
		ID3D11SamplerState* smp = g_sampler.get();
		ctx->RSSetState(g_raster.get());

		Kind curKind = Kind::Color; bool first = true;
		ID3D11BlendState* curBlend = nullptr;
		for (const Cmd& c : g_cmds)
		{
			if (first || c.kind != curKind)
			{
				curKind = c.kind; first = false;
				if (c.kind == Kind::Color)
				{
					UINT stride = sizeof(ColorVertex), off = 0;
					ID3D11Buffer* vb = g_cvb.get();
					ctx->IASetInputLayout(g_clayout.get());
					ctx->IASetVertexBuffers(0, 1, &vb, &stride, &off);
					ctx->VSSetShader(g_cvs.get(), nullptr, 0);
					ctx->PSSetShader(g_cps.get(), nullptr, 0);
					ctx->VSSetConstantBuffers(0, 1, &cb);
				}
				else
				{
					UINT stride = sizeof(TexVertex), off = 0;
					ID3D11Buffer* vb = g_tvb.get();
					ctx->IASetInputLayout(g_tlayout.get());
					ctx->IASetVertexBuffers(0, 1, &vb, &stride, &off);
					ctx->VSSetShader(g_tvs.get(), nullptr, 0);
					ctx->PSSetShader(g_tps.get(), nullptr, 0);
					ctx->VSSetConstantBuffers(0, 1, &cb);
					ctx->PSSetSamplers(0, 1, &smp);
					ctx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
				}
			}

			/* Per-command blend: textured = alpha, cross-hairs = XOR, else opaque. */
			ID3D11BlendState* blend =
				(c.kind == Kind::Tex)            ? g_blend_alpha.get() :
				(c.xorop && g_blend_xor)         ? static_cast<ID3D11BlendState*>(g_blend_xor.get()) :
				                                   g_blend_opaque.get();
			if (blend != curBlend)
			{
				ctx->OMSetBlendState(blend, nullptr, 0xffffffff);
				curBlend = blend;
			}

			ctx->RSSetScissorRects(1, &c.scissor);

			if (c.kind == Kind::Color)
			{
				switch (c.topo)
				{
					case Topo::Points: ctx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_POINTLIST);    break;
					case Topo::Lines:  ctx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_LINELIST);     break;
					case Topo::Tris:   ctx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST); break;
				}
			}
			else
			{
				ID3D11ShaderResourceView* srv = c.srv;
				ctx->PSSetShaderResources(0, 1, &srv);
			}

			ctx->Draw(c.count, c.start);
		}

		ID3D11ShaderResourceView* nullsrv = nullptr;
		ctx->PSSetShaderResources(0, 1, &nullsrv);
	}

	g_cverts.clear();
	g_tverts.clear();
	g_cmds.clear();
}
