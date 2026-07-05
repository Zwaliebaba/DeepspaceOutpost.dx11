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
#include "Canvas.h" // Canvas::Start/End - the shared 2D-pass bracket (Phase 2)

#include "gfx.h"
#include "Camera.h"

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

/* The frame's 3D scene (the dust starfield background + the models the game handed straight to
 * Scene3D via Scene3D::SubmitModel) is drawn by gfx_render_3d_scene(), which the game calls
 * directly at the end of its world draw - even with no models in view (staring at empty space
 * still shows the stars). Models live in Scene3D, not here. */
D3D11_RECT               g_scissor  = { 0, 0, Renderer::CANVAS_WIDTH, Renderer::CANVAS_HEIGHT };

/* Scene state. The 2D layer always fills the client window now (the letterbox is
 * retired - canvasPlacement is identity). (g_scene_w,g_scene_h) is the live client size
 * the CPU-projected HUD bits (stars/threed/space) project against; the projection lives
 * on the main Camera - see gfx_set_scene_fullwindow. The floated draw-origin is gone: the
 * flight HUD that used it now draws natively (RenderGameHud), so the remaining gfx2d
 * consumers (starfield, explosion debris, target reticle, message text, intro) all author
 * in plain client pixels. */
int                        g_scene_w = 512;
int                        g_scene_h = 384;

/* The 2D authoring space is the live client area: the letterbox is retired, so the 2D
 * batch and the 3D scene pass always fill the window 1:1 (canvasPlacement is identity).
 * Falls back to the fixed canvas size only before the Renderer is up. */
int canvasW() { Renderer* r = platform_renderer(); return r ? r->clientWidth()  : Renderer::CANVAS_WIDTH;  }
int canvasH() { Renderer* r = platform_renderer(); return r ? r->clientHeight() : Renderer::CANVAS_HEIGHT; }

/* Placement of the authored 2D canvas onto the back buffer: the single source of the
 * virtual size + destination offset + scale that both the 2D replay and the 3D scene
 * pass use (see gfx2d_flush). Native size, centred (D1): a fixed-size 2D screen (the retro
 * 512x514 canvas) renders 1:1 with black margins around it; the full-window scene/HUD
 * authors at the client size, so it fills the window (scale 1, no offset). Downscale only
 * when the window is smaller than the canvas, so nothing is lost; never upscale, so pixel
 * art stays crisp. */
struct CanvasPlacement { int vw, vh, dstX, dstY; float scale; };

CanvasPlacement canvasPlacement()
{
	Renderer* r = platform_renderer();
	const int vw = canvasW();
	const int vh = canvasH();
	const int cw = r ? r->clientWidth()  : vw;
	const int ch = r ? r->clientHeight() : vh;
	float scale = std::min(cw / static_cast<float>(vw), ch / static_cast<float>(vh));
	if (scale > 1.0f) scale = 1.0f; // native size max; shrink only if the window is smaller than the canvas
	const int dstX = static_cast<int>((cw - vw * scale) * 0.5f);
	const int dstY = static_cast<int>((ch - vh * scale) * 0.5f);
	return { vw, vh, dstX, dstY, scale };
}

/* The 2D batch renders through Neuron::Graphics::Render2D (see gfx2d_flush), so this
 * layer no longer owns any Direct3D shaders / buffers / pipeline state. */
std::map<std::string, Texture> g_textures;

/* Text no longer draws through this batch: all game text (HUD, charts, menus, the intro
 * titles / flight message / GAME OVER banner) is native now (TextRenderer / g_gameFont via
 * Render2D). So the shared bitmap-font sheet and its glyph plumbing are gone from here. */

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
		if (b.kind == Kind::Color && b.topo == topo && sameRect(b.scissor, g_scissor))
		{
			b.count += n;
			g_cverts.insert(g_cverts.end(), v, v + n);
			return;
		}
	}
	g_cmds.push_back({ Kind::Color, topo, static_cast<uint32_t>(g_cverts.size()),
					   static_cast<uint32_t>(n), g_scissor, nullptr });
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
					   6, g_scissor, srv });
	g_tverts.insert(g_tverts.end(), q, q + 6);
}

// The only surviving colour primitive is the single-pixel plot (gfx_plot_pixel: the
// starfield and the ship-death debris spray). Lines / rects / triangles went with the
// flight HUD, which now draws natively (RenderGameHud), so their batch helpers are gone.
void addPoint(int x, int y, uint32_t c)
{
	ColorVertex v{ x + 0.5f, y + 0.5f, c };
	pushColor(Topo::Points, &v, 1);
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

} // namespace

/* =====================================================================
 *  gfx.h primitives
 * ===================================================================== */

void gfx_plot_pixel(int x, int y, int col)      { addPoint(x, y, col_rgba(col)); }
void gfx_clear_display(void)
{
	/* No-op: ClientEngine::Frame clears the whole back buffer each frame (before the 3D
	 * scene hook), and the 2D layer composites on top of the full-window 3D scene, so a 2D
	 * black rect here would only paint over it. Kept as a call site for the legacy screens
	 * that still invoke it. (Was the retro-canvas play-area clear before the letterbox retired.) */
}

/* The line / rectangle / circle primitives (and gfx_clear_text_area) drew the flight-HUD
 * dashboard and the old letterboxed screens; both are retired (the HUD is native now), so
 * they are gone. gfx_plot_pixel above is the last colour primitive - the starfield and the
 * ship-death debris spray. */

void gfx_set_clip_region(int tx, int ty, int bx, int by)
{
	/* Clamp to the live canvas (which may be the full client area, not 512x514). */
	LONG l = tx, t = ty, r = bx + 1, b = by + 1;
	if (l < 0) l = 0; if (t < 0) t = 0;
	if (r > canvasW()) r = canvasW();
	if (b > canvasH()) b = canvasH();
	g_scissor = { l, t, r, b };
}

/* ---- full-window scene / floating HUD ---- */

// Select the canvas/projection mode for the frame about to be drawn: full-window
// (the in-flight 3D fills the client area) or retro (the letterboxed 512x514
// canvas for menus/charts/station). Re-issues the main Camera's projection for
// the live viewport each call - the legacy vertical field of view at the current
// aspect ratio - so it is safe to call every frame.
void gfx_set_scene_fullwindow(int on)
{
	// The letterbox is retired: the scene always fills the client window. `on` is kept
	// for the callers' signature but no longer selects a retro mode. Update the scene size
	// to the live client area and re-issue the main Camera's projection for that viewport
	// (the legacy vertical FOV at the current aspect) - safe to call every frame.
	(void) on;
	Renderer* r = platform_renderer();
	g_scene_w = r ? r->clientWidth()  : 512;
	g_scene_h = r ? r->clientHeight() : 384;

	const float aspect = (g_scene_h > 0) ? static_cast<float>(g_scene_w) / static_cast<float>(g_scene_h) : 4.0f / 3.0f;
	Neuron::Client::MainCamera().SetProjParams(Neuron::Client::LEGACY_SCENE_FOV_Y, aspect,
											   Neuron::Client::SCENE_NEAR_Z, Neuron::Client::SCENE_FAR_Z);
}

// The scene canvas size for the current frame, in logical pixels (the space the
// CPU-projected HUD bits in threed.cpp / space.cpp / stars.cpp draw in).
void gfx_scene_size(int* w, int* h)
{
	if (w) *w = g_scene_w;
	if (h) *h = g_scene_h;
}

// The current 2D authoring canvas size in pixels = the live client area (the letterbox
// is retired). Screens read this to place content relative to the window edges.
void gfx_canvas_size(int* w, int* h)
{
	if (w) *w = canvasW();
	if (h) *h = canvasH();
}

// Set the clip rect to the 3D play area for the current mode: the whole canvas
// in full-window flight, or the legacy 1,1..510,383 rectangle in retro.
void gfx_set_scene_clip(void)
{
	// Always full-window now (the letterbox is retired): clip to the whole client area.
	gfx_set_clip_region(0, 0, canvasW() - 1, canvasH() - 1);
}

/* ---- sprites / HUD ---- */
void gfx_draw_sprite(int sprite_no, int x, int y)
{
	const char* fn = spriteFile(sprite_no);
	if (!fn) return;
	const Texture* t = getTexture(fn);
	if (!t || !t->srv) return;
	if (x == -1) x = (canvasW() - t->w) / 2; // centre on the live canvas (client-space aware)
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

/* =====================================================================
 *  Scene pass + 2D flush
 * ===================================================================== */

/* The 3D scene pass (dust starfield background -> depth-tested ships / planets / sun). The game
 * calls this directly at the end of its world draw (update_local_objects / render_replicated_objects),
 * once all models are submitted (Scene3D::SubmitModel) and the dust is set - so it drives the
 * pass itself, with no separate scene-marker flag. No clear (ClientEngine::Frame clears the
 * back buffer once per frame, before the scene hook) and no 2D; the HUD / menus / GUI composite
 * over it later in gfx2d_flush. Runs unconditionally (drawing the dust even with no models in
 * view), and safely on a null rtv (device lost) - Scene3D still clears the frame's model list. */
void gfx_render_3d_scene(void)
{
	using Neuron::Graphics::Core;

	const CanvasPlacement cp = canvasPlacement();
	Neuron::Graphics::Scene3D::RenderModels(Core::GetRenderTargetView(), Core::GetDepthStencilView(),
											Neuron::Client::MainCamera(), cp.dstX, cp.dstY,
											static_cast<int>(cp.vw * cp.scale), static_cast<int>(cp.vh * cp.scale));
}

void gfx2d_flush(void)
{
	using Neuron::Graphics::Core;
	using Neuron::Graphics::Render2D;

	Renderer* r = platform_renderer();
	if (!r) { g_cverts.clear(); g_tverts.clear(); g_cmds.clear(); return; }

	/* 2D only. The back buffer is cleared once per frame by ClientEngine::Frame (before the
	 * scene hook), and the 3D scene pass is drawn by the game via gfx_render_3d_scene() during
	 * RenderScene - this just composites the 2D HUD / menus / GUI on top of it (no re-clear).
	 * Every screen redraws every frame, so there is no empty frame to skip and the caller always
	 * presents. */
	const CanvasPlacement cp = canvasPlacement();
	const int vw = cp.vw;
	const int vh = cp.vh;
	const int dstX = cp.dstX;
	const int dstY = cp.dstY;
	const float scale = cp.scale;

	ID3D11RenderTargetView* rtv = Core::GetRenderTargetView();

	if (!g_cmds.empty() && rtv)
	{
		/* Replay the submission-ordered command list through Render2D, letterboxed onto
		 * the back buffer. Point sampling keeps the sprites / HUD / bitmap font crisp.
		 * (XOR for the chart cross-hairs is dropped - the logic-op path never worked and
		 * is slated for a texture.) */
		Canvas::Start(rtv, vw, vh, dstX, dstY, static_cast<float>(scale), D3D11_FILTER_MIN_MAG_MIP_POINT);

		/* Only sprites and single-pixel plots reach this batch now (text went native), so
		 * every command replays through the default col*texture program. */
		static std::vector<Render2D::Vertex> scratch; // reused across frames (single-threaded)
		for (const Cmd& c : g_cmds)
		{
			Render2D::SetClip(c.scissor.left, c.scissor.top, c.scissor.right - c.scissor.left,
							  c.scissor.bottom - c.scissor.top);

			scratch.clear();
			scratch.reserve(c.count);
			Render2D::SetProgram(Render2D::DefaultProgram);

			if (c.kind == Kind::Tex)
			{
				for (uint32_t i = 0; i < c.count; i++)
				{
					const TexVertex& v = g_tverts[c.start + i];
					scratch.push_back({v.x, v.y, v.u, v.v, v.rgba});
				}
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
				Render2D::Submit(topo, scratch.data(), static_cast<int>(scratch.size()), nullptr);
			}
		}

		Canvas::End();
	}

	g_cverts.clear();
	g_tverts.clear();
	g_cmds.clear();
}
