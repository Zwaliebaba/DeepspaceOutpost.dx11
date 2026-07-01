/*
 * DeepspaceOutpost - DirectX 11 / XAudio2.
 *
 * gfx2d.cpp
 *
 * The gfx.h 2D contract on a submission-order batch renderer. Two vertex streams
 * (solid-colour and textured) feed a single command list, so lines/polygons,
 * sprites, the HUD bitmap and text all composite in the exact order the game draws
 * them. In gfx2d_flush() the batch replays through Neuron::Graphics::Render2D straight
 * onto the back buffer (letterboxed), and Neuron::Graphics::Core::Present() then shows it. An idle
 * frame with an empty batch is left unpresented so the last frame persists (see
 * gfx2d_flush). (Formerly gfx_dx11.cpp, which had its own Direct3D 11 pipeline.)
 *
 * Colours are palette indices resolved against scanner.bmp. Solid primitives are
 * opaque (index 0 -> opaque black); sprite/HUD art is .dds (alpha baked in). Text is
 * drawn from the shared .dds bitmap-font sheet (the same one the GUI's TextRenderer
 * uses), batched here so it composites and clips in draw order with the rest of the 2D.
 */

#include "pch.h"

#include "Renderer.h"
#include "gfx2d.h"
#include "TextureManager.h"
#include "Render2D.h"
#include "Scene3D.h"

#include "gfx.h"
#include "ViewMetrics.h"

#include <d3d11.h>
#include <winrt/base.h>
#include <algorithm>
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

enum class Kind { Color, Tex, Scene };
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

/* ---- batch state ---- */
std::vector<ColorVertex> g_cverts;
std::vector<TexVertex>   g_tverts;
std::vector<Cmd>         g_cmds;

/* 3D scene models for this frame (ships), collected via gfx2d_submit_model and
 * rendered through Scene3D at each scene marker (see gfx_finish_render / gfx2d_flush).
 * g_models_marked is the count already covered by a marker, so a second
 * StartRender/FinishRender bracket in the same frame renders only its own models. */
std::vector<Neuron::Render::ModelDraw> g_models;
uint32_t                               g_models_marked = 0;
D3D11_RECT               g_scissor  = { 0, 0, Renderer::kCanvasWidth, Renderer::kCanvasHeight };
bool                     g_xor_mode = false;

/* Full-window scene state. When the in-flight 3D fills the window, g_scene_full
 * is set for the frame and g_view carries the aspect-aware optics; the HUD is
 * floated by adding (g_origin_x,g_origin_y) to every emitted coordinate and to
 * the clip rect. In retro mode all three are inert (origin 0, legacy optics). */
int                        g_origin_x  = 0;
int                        g_origin_y  = 0;
bool                       g_scene_full = false;
Neuron::Client::ViewMetrics g_view = Neuron::Client::MakeViewMetrics(512, 384);

/* The virtual coordinate space the 2D batch is authored in: the retro 512x514 canvas,
 * or the live client area when the in-flight 3D fills the window. (Formerly the size of
 * an off-screen canvas texture; now just the projection space that gfx2d_flush
 * letterboxes straight onto the back buffer.) */
int canvasW() { Renderer* r = platform_renderer(); return (r && g_scene_full) ? r->clientWidth()  : Renderer::kCanvasWidth;  }
int canvasH() { Renderer* r = platform_renderer(); return (r && g_scene_full) ? r->clientHeight() : Renderer::kCanvasHeight; }

/* The 2D batch renders through Neuron::Graphics::Render2D (see gfx2d_flush), so this
 * layer no longer owns any Direct3D shaders / buffers / pipeline state. */
std::map<std::string, Texture> g_textures;

/* The shared .dds bitmap font: a 16-column x 14-row grid of cells starting at
 * ASCII 32, identical to the sheet the GUI's TextRenderer draws (so game text and
 * menu text match). It is loaded once at engine start via TextureManager; we just
 * borrow its SRV and feed glyph quads through this batch. Replaces the old
 * verd2/verd4 grabber-font atlas (platform/Font). */
std::shared_ptr<Neuron::Graphics::Texture> g_font_sheet;

/* Monospaced cell metrics in canvas pixels. The body advance (~8px) matches the
 * layout the game already assumes (e.g. gfx_display_pretty_text wraps at width/8),
 * and the ~0.6 w:h ratio matches TextRenderer so the sheet looks the same here and
 * in the menus. The title size is the larger heading font (psize 140). */
struct FontSize { float charW, charH; };
constexpr FontSize kBodyFont  { 8.0f, 13.0f };
constexpr FontSize kTitleFont { 12.0f, 20.0f };

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
	x0 += g_origin_x; x1 += g_origin_x;
	y0 += g_origin_y; y1 += g_origin_y;
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

/* All primitive coordinates pass through the draw-origin so the HUD can be
 * floated over the full-window 3D without each widget knowing where it sits. */
void addPoint(int x, int y, uint32_t c)
{
	ColorVertex v{ x + g_origin_x + 0.5f, y + g_origin_y + 0.5f, c };
	pushColor(Topo::Points, &v, 1);
}
void addSegment(int x1, int y1, int x2, int y2, uint32_t c)
{
	x1 += g_origin_x; x2 += g_origin_x; y1 += g_origin_y; y2 += g_origin_y;
	ColorVertex v[2] = { { x1 + 0.5f, y1 + 0.5f, c }, { x2 + 0.5f, y2 + 0.5f, c } };
	pushColor(Topo::Lines, v, 2);
}
void addRect(int x1, int y1, int x2, int y2, uint32_t c)
{
	if (x2 < x1) std::swap(x1, x2);
	if (y2 < y1) std::swap(y1, y2);
	x1 += g_origin_x; x2 += g_origin_x; y1 += g_origin_y; y2 += g_origin_y;
	float l = (float)x1, t = (float)y1, r = (float)(x2 + 1), b = (float)(y2 + 1);
	ColorVertex q[6] = { {l,t,c},{r,t,c},{r,b,c}, {l,t,c},{r,b,c},{l,b,c} };
	pushColor(Topo::Tris, q, 6);
}
void addTri(int x1, int y1, int x2, int y2, int x3, int y3, uint32_t c)
{
	x1 += g_origin_x; x2 += g_origin_x; x3 += g_origin_x;
	y1 += g_origin_y; y2 += g_origin_y; y3 += g_origin_y;
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
// Sprites and the scanner HUD load through the native Neuron::Graphics::TextureManager
// (.dds, alpha baked in), so this layer no longer needs the legacy platform/Image
// decoder. The SRV is borrowed (AddRef'd) from the manager's cached Texture.
const Texture* getTexture(const char* path)
{
	auto it = g_textures.find(path);
	if (it != g_textures.end())
		return &it->second;

	Texture t;
	auto managed = Neuron::Graphics::TextureManager::LoadTexture(path);
	if (managed && managed->IsLoaded())
	{
		t.srv.copy_from(managed->GetShaderResourceView());
		t.w = static_cast<int>(managed->GetWidth());
		t.h = static_cast<int>(managed->GetHeight());
	}
	auto res = g_textures.emplace(path, std::move(t));
	return &res.first->second;
}

const char* spriteFile(int sprite_no)
{
	switch (sprite_no)
	{
		case IMG_GREEN_DOT:      return "greendot.dds";
		case IMG_RED_DOT:        return "reddot.dds";
		case IMG_BIG_S:          return "safe.dds";
		case IMG_ELITE_TXT:      return "elitetx3.dds";
		case IMG_BIG_E:          return "ecm.dds";
		case IMG_MISSILE_GREEN:  return "missgrn.dds";
		case IMG_MISSILE_YELLOW: return "missyell.dds";
		case IMG_MISSILE_RED:    return "missred.dds";
		case IMG_BLAKE:          return "blake.dds";
		case IMG_TARGET_LOCK:    return "Textures/TargetLock.dds";
		case IMG_CROSSHAIR:      return "Textures/Crosshair.dds";
		default:                 return nullptr;
	}
}

ID3D11ShaderResourceView* fontSheetSRV()
{
	/* TextureManager caches, so this is a hash lookup after the first call.
	 * ClientEngine loads this sheet at start-up (once the device is up), so by the
	 * time the game draws text it is resident. */
	if (!g_font_sheet)
		g_font_sheet = Neuron::Graphics::TextureManager::LoadTexture("Fonts/SpeccyFontENG.dds");
	return (g_font_sheet && g_font_sheet->IsLoaded()) ? g_font_sheet->GetShaderResourceView() : nullptr;
}

/* Emit one run of glyph quads at (x,y) in a single tint. Factored out of drawString
 * so a drop-shadow pass can be laid down before the coloured glyphs. */
void emitGlyphs(ID3D11ShaderResourceView* srv, const FontSize& fs, float x, float y, const char* s, uint32_t tint)
{
	/* Per-glyph tex cell on the 16-col x 14-row grid (16x16 px cells in the 256x224
	 * sheet) starting at ASCII 32. Sample the WHOLE cell on exact texel boundaries:
	 * with point sampling that reconstructs the native glyph pixels cleanly at any
	 * destination size. (The old fudged UVs - a 0.9 width crop plus sub-texel margins -
	 * pushed the sample window off the texel grid, so small body text sampled between
	 * source pixels and lost crispness; see TextRenderer::GetTexCoord* for the GUI's
	 * matching cells.) chars <= 32 (space + control) advance without drawing. */
	constexpr float CELL_W = 1.0f / 16.0f;   // 16 columns
	constexpr float CELL_H = 1.0f / 14.0f;   // 14 rows

	float pen = x;
	for (; *s; s++)
	{
		const unsigned char c = (unsigned char)*s;
		if (c > 32)
		{
			const float u0 = (c % 16) * CELL_W;
			const float v0 = ((c >> 4) - 2) * CELL_H;
			pushTexQuad(srv, pen, y, pen + fs.charW, y + fs.charH,
						u0, v0, u0 + CELL_W, v0 + CELL_H, tint);
		}
		pen += fs.charW;
	}
}

void drawString(const FontSize& fs, int x, int y, const char* s, uint32_t tint)
{
	ID3D11ShaderResourceView* srv = fontSheetSRV();
	if (!srv || !s) return;

	/* Single pass: the glyphs get a crisp outline in the shader at flush time. Commands
	 * bound to the font sheet are replayed through Render2D's text-outline program (see
	 * gfx2d_flush), so the text stays readable over the busy 3D backdrop without an
	 * extra offset-shadow geometry pass. */
	emitGlyphs(srv, fs, (float)x, (float)y, s, tint);
}

/* The depth-sorted painter's chain was retired once the 3D scene moved to the GPU
 * (Scene3D resolves visibility with the hardware z-buffer). gfx_render_polygon /
 * gfx_render_line now draw immediately as plain 2D, kept only for any 2D-projected
 * marker that still uses them. */

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
void gfx_clear_display(void)
{
	/* Full-window flight clears the whole canvas (the 3D fills it); retro mode
	 * keeps clearing just the legacy play area so the dashboard strip persists. */
	if (g_scene_full)
		addRect(0, 0, canvasW() - 1, canvasH() - 1, col_rgba(GFX_COL_BLACK));
	else
		addRect(1, 1, 510, 383, col_rgba(GFX_COL_BLACK));
}
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
	/* Follow the draw-origin so a floated HUD's clip moves with it, and clamp to
	 * the live canvas (which may be the full client area, not 512x514). */
	LONG l = tx + g_origin_x, t = ty + g_origin_y, r = bx + 1 + g_origin_x, b = by + 1 + g_origin_y;
	if (l < 0) l = 0; if (t < 0) t = 0;
	if (r > canvasW()) r = canvasW();
	if (b > canvasH()) b = canvasH();
	g_scissor = { l, t, r, b };
}

void xor_mode(int on) { g_xor_mode = (on != 0); }

/* ---- full-window scene / floating HUD ---- */

// Select the canvas/projection mode for the frame about to be drawn: full-window
// (the in-flight 3D fills the client area, aspect-aware optics) or retro (the
// letterboxed 512x514 canvas for menus/charts/station). Recomputes the optics
// from the live client size each call, so it is safe to call every frame.
void gfx_set_scene_fullwindow(int on)
{
	Renderer* r = platform_renderer();
	if (on && r)
	{
		g_scene_full = true;
		g_view = Neuron::Client::MakeViewMetrics(r->clientWidth(), r->clientHeight());
	}
	else
	{
		g_scene_full = false;
		g_view = Neuron::Client::MakeViewMetrics(512, 384);   // legacy play-area optics
	}
}

// The aspect-aware optics for the current frame (used by the software projection
// in threed.cpp / stars.cpp).
const Neuron::Client::ViewMetrics& gfx_view_metrics(void) { return g_view; }

// Offset every subsequent emitted coordinate (and clip rect) by (x,y). Used to
// float the HUD; pass (0,0) to draw in the canvas's own space again.
void gfx_set_draw_origin(int x, int y) { g_origin_x = x; g_origin_y = y; }

// Where the legacy 512x514 HUD layout should be anchored this frame. Retro mode
// keeps it at the origin; full-window mode pins it to the bottom-centre of the
// client area so the dashboard floats over the 3D.
void gfx_hud_anchor(int* ox, int* oy)
{
	if (g_scene_full)
	{
		*ox = (canvasW() - 512) / 2;
		*oy = canvasH() - 514;
		if (*ox < 0) *ox = 0;
		if (*oy < 0) *oy = 0;
	}
	else
	{
		*ox = 0;
		*oy = 0;
	}
}

// Set the clip rect to the 3D play area for the current mode: the whole canvas
// in full-window flight, or the legacy 1,1..510,383 rectangle in retro.
void gfx_set_scene_clip(void)
{
	if (g_scene_full)
		gfx_set_clip_region(0, 0, canvasW() - 1, canvasH() - 1);
	else
		gfx_set_clip_region(1, 1, 510, 383);
}

/* ---- text ---- */
void gfx_display_text(int x, int y, const char* txt)
{
	drawString(kBodyFont, x, y, txt, col_rgba(GFX_COL_WHITE));
}
void gfx_display_colour_text(int x, int y, const char* txt, int col)
{
	drawString(kBodyFont, x, y, txt, col_rgba(col));
}
void gfx_display_centre_text(int y, const char* str, int psize, int col)
{
	/* Centre on the live canvas: 256 in retro, the window middle in full-window
	 * flight (so in-flight messages stay centred when the 3D fills the screen). */
	const int mid = g_scene_full ? canvasW() / 2 : 256;
	/* psize 140 selects the larger heading font; both are the one .dds sheet now
	 * (the old ELITE_2 multicolour title sheet is gone), tinted by the caller's
	 * colour. Monospaced, so the width is simply chars * cell width. */
	const FontSize& fs = (psize == 140) ? kTitleFont : kBodyFont;
	const int w = static_cast<int>(std::strlen(str) * fs.charW);
	drawString(fs, mid - w / 2, y, str, col_rgba(col));
}

void gfx_display_pretty_text(int tx, int ty, int bx, int /*by*/, const char* txt)
{
	/* 'by' (bottom bound) is unused: the original wraps by width only. */
	char strbuf[100];
	const char* str = txt;
	int   len = (int)std::strlen(txt);
	int   maxlen = (bx - tx) / 8;
	if (maxlen <= 0) maxlen = 1;
	/* A line copies pos+1 chars plus a NUL into strbuf, and pos can reach maxlen, so
	 * clamp maxlen to the buffer to keep a wide (bx-tx) from overflowing it. */
	if (maxlen > static_cast<int>(sizeof(strbuf)) - 2) maxlen = static_cast<int>(sizeof(strbuf)) - 2;

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

		drawString(kBodyFont, tx, ty, strbuf, col_rgba(GFX_COL_WHITE));
		ty += 8 * GFX_SCALE;
	}
}

/* ---- sprites / HUD ---- */
void gfx_draw_sprite(int sprite_no, int x, int y)
{
	const char* fn = spriteFile(sprite_no);
	if (!fn) return;
	const Texture* t = getTexture(fn);
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
	const Texture* t = getTexture(fn);
	if (!t || !t->srv) return;
	pushTexQuad(t->srv.get(), (float)x, (float)y, (float)(x + w), (float)(y + h),
				0.0f, 0.0f, 1.0f, 1.0f, 0xFFFFFFFFu);
}

void gfx_draw_scanner(void)
{
	/* The configured scanner image (and the default) name a .bmp - Renderer still
	 * reads scanner.bmp for the master palette - but the HUD sprite itself now loads
	 * as .dds through the TextureManager, so map the extension across. */
	const char* cfg = (scanner_filename[0] != '\0') ? scanner_filename : "scanner.bmp";
	std::string fn = cfg;
	if (const size_t dot = fn.find_last_of('.'); dot != std::string::npos)
		fn.replace(dot, std::string::npos, ".dds");
	else
		fn += ".dds";
	const Texture* t = getTexture(fn.c_str());
	if (!t || !t->srv) return;
	pushTexQuad(t->srv.get(), 0.0f, 385.0f, (float)t->w, (float)(385 + t->h),
				0.0f, 0.0f, 1.0f, 1.0f, 0xFFFFFFFFu);
}

/* ---- 3D scene submission (depth via the GPU z-buffer, no CPU painter's sort) ---- */
void gfx_start_render(void) { /* no-op: the painter's chain was retired (see Scene3D). */ }

/* Draw immediately as a flat 2D polygon; the depth key is ignored (the GPU z-buffer
 * orders the 3D scene now). poly_list is 2 ints (x,y) per point, same as gfx_polygon. */
void gfx_render_polygon(int num_points, int* point_list, int face_colour, int /*zavg*/)
{
	gfx_polygon(num_points, point_list, face_colour);
}

void gfx_render_line(int x1, int y1, int x2, int y2, int /*dist*/, int col)
{
	gfx_draw_colour_line(x1, y1, x2, y2, col);
}

void gfx2d_submit_model(const Neuron::Render::ModelDraw& _model)
{
	g_models.push_back(_model);
}

void gfx_finish_render(void)
{
	/* Mark where the GPU 3D scene renders in submission order: the models collected
	 * since the last marker draw here - after the 2D background just emitted, before the
	 * HUD that follows. gfx2d_flush runs the depth-tested Scene3D pass at this point. */
	if (g_models.size() > g_models_marked)
	{
		Cmd c{};
		c.kind = Kind::Scene;
		c.topo = Topo::Tris;
		c.start = g_models_marked;
		c.count = static_cast<uint32_t>(g_models.size()) - g_models_marked;
		c.scissor = g_scissor;
		c.srv = nullptr;
		c.xorop = false;
		g_cmds.push_back(c);
		g_models_marked = static_cast<uint32_t>(g_models.size());
	}
}

/* =====================================================================
 *  Flush
 * ===================================================================== */
bool gfx2d_flush(bool forcePresent)
{
	using Neuron::Graphics::Core;
	using Neuron::Graphics::Render2D;

	Renderer* r = platform_renderer();
	if (!r) { g_cverts.clear(); g_tverts.clear(); g_cmds.clear(); g_models.clear(); g_models_marked = 0; return false; }

	/* Nothing drawn this frame and no forced repaint: leave the back buffer alone so the
	 * previously presented frame stays on screen. The menu/station screens repaint only
	 * on demand, and FLIP_DISCARD keeps no retained content, so clearing+presenting an
	 * empty batch here is what made those screens flash to black on idle frames. */
	if (g_cmds.empty() && !forcePresent)
	{
		g_cverts.clear(); g_tverts.clear(); g_cmds.clear(); g_models.clear(); g_models_marked = 0;
		return false;
	}

	/* The batch is authored in the virtual space (retro 512x514, or the client area in
	 * full-window flight). Letterbox it onto the back buffer with an integer scale so the
	 * pixel art stays crisp; full-window flight gives scale 1 and no bars. No off-screen
	 * canvas / blit: the viewport scales the virtual space straight onto the back buffer. */
	const int vw = canvasW();
	const int vh = canvasH();
	const int cw = r->clientWidth();
	const int ch = r->clientHeight();
	int scale = std::min(cw / vw, ch / vh);
	if (scale < 1) scale = 1;
	const int dstX = (cw - vw * scale) / 2;
	const int dstY = (ch - vh * scale) / 2;

	ID3D11RenderTargetView* rtv = Core::GetRenderTargetView();
	ID3D11DeviceContext* ctx = Core::GetD3DDeviceContext();

	/* Clear the whole back buffer (letterbox bars + anything the batch does not paint)
	 * before this frame's content. */
	if (rtv && ctx)
	{
		const float black[4] = {0.0f, 0.0f, 0.0f, 1.0f};
		ctx->ClearRenderTargetView(rtv, black);
	}

	if (!g_cmds.empty() && rtv)
	{
		/* Replay the submission-ordered command list through Render2D, letterboxed onto
		 * the back buffer. Point sampling keeps the sprites / HUD / bitmap font crisp.
		 * (XOR for the chart cross-hairs is dropped - the logic-op path never worked and
		 * is slated for a texture.) */
		Render2D::Begin(rtv, vw, vh, dstX, dstY, static_cast<float>(scale), D3D11_FILTER_MIN_MAG_MIP_POINT);

		/* Text commands (those bound to the font sheet) replay through the built-in
		 * text-outline program for a shader-side outline; sprites/HUD and colored prims
		 * use the default program. Configure the outline from the sheet's texel size. */
		ID3D11ShaderResourceView* fontSrv = fontSheetSRV();
		if (g_font_sheet && g_font_sheet->IsLoaded() && g_font_sheet->GetWidth() > 0.0f &&
			g_font_sheet->GetHeight() > 0.0f)
			Render2D::SetTextOutline(0xFF000000u, 1.0f / g_font_sheet->GetWidth(), 1.0f / g_font_sheet->GetHeight(),
									 1.0f);

		static std::vector<Render2D::Vertex> scratch; // reused across frames (single-threaded)
		for (const Cmd& c : g_cmds)
		{
			if (c.kind == Kind::Scene)
			{
				/* End the 2D background batch drawn so far, run the depth-tested 3D scene
				 * pass (ships), then resume a fresh 2D batch for the HUD that follows -
				 * same target, no re-clear, so it composites on top. */
				Render2D::End();
				Neuron::Graphics::Scene3D::RenderModels(rtv, Core::GetDepthStencilView(), g_view, dstX, dstY,
														vw * scale, vh * scale, g_models.data() + c.start,
														static_cast<int>(c.count));
				Render2D::Begin(rtv, vw, vh, dstX, dstY, static_cast<float>(scale), D3D11_FILTER_MIN_MAG_MIP_POINT);
				if (g_font_sheet && g_font_sheet->IsLoaded() && g_font_sheet->GetWidth() > 0.0f &&
					g_font_sheet->GetHeight() > 0.0f)
					Render2D::SetTextOutline(0xFF000000u, 1.0f / g_font_sheet->GetWidth(),
											 1.0f / g_font_sheet->GetHeight(), 1.0f);
				continue;
			}

			Render2D::SetClip(c.scissor.left, c.scissor.top, c.scissor.right - c.scissor.left,
							  c.scissor.bottom - c.scissor.top);

			scratch.clear();
			scratch.reserve(c.count);

			if (c.kind == Kind::Tex)
			{
				for (uint32_t i = 0; i < c.count; i++)
				{
					const TexVertex& v = g_tverts[c.start + i];
					scratch.push_back({v.x, v.y, v.u, v.v, v.rgba});
				}
				Render2D::SetProgram(c.srv == fontSrv ? Render2D::TextOutlineProgram() : Render2D::DefaultProgram);
				Render2D::Submit(Render2D::Topo::Tris, scratch.data(), static_cast<int>(scratch.size()), c.srv);
			}
			else
			{
				for (uint32_t i = 0; i < c.count; i++)
				{
					const ColorVertex& v = g_cverts[c.start + i];
					scratch.push_back({v.x, v.y, 0.0f, 0.0f, v.rgba});
				}
				const Render2D::Topo topo = (c.topo == Topo::Points) ? Render2D::Topo::Points
										  : (c.topo == Topo::Lines)  ? Render2D::Topo::Lines
																	 : Render2D::Topo::Tris;
				Render2D::SetProgram(Render2D::DefaultProgram);
				Render2D::Submit(topo, scratch.data(), static_cast<int>(scratch.size()), nullptr);
			}
		}

		Render2D::End();
	}

	g_cverts.clear();
	g_tverts.clear();
	g_cmds.clear();
	g_models.clear();
	g_models_marked = 0;
	return true;
}
