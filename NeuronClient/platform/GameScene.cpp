/*
 * DeepspaceOutpost - DirectX 11 / XAudio2.
 *
 * GameScene.cpp
 *
 * The thin game<->engine scene/viewport seam (declared in GameScene.h). This is what
 * survived the retirement of the old gfx2d 2D batch renderer: all 2D drawing is native now
 * (Neuron::Graphics::Render2D via RenderGameHud / the GUI overlay), so the vertex batch, the
 * sprite/font plumbing and gfx2d_flush() are gone.
 *
 * What remains: the 3D scene pass (gfx_render_3d_scene -> Scene3D), the live scene/canvas size
 * and projection (gfx_set_scene_fullwindow / gfx_scene_size / gfx_canvas_size), and the
 * vestigial clip / clear no-ops the legacy screens still call. (The platform lifecycle
 * gfx_graphics_* lives in platform_win.cpp.)
 */

#include "pch.h"

#include "Renderer.h"
#include "GraphicsCore.h"
#include "Scene3D.h"
#include "Camera.h"

#include "GameScene.h"

#include <algorithm>

namespace {

/* Scene state. The 2D layer and the 3D scene pass always fill the client window 1:1 (the
 * letterbox is retired - canvasPlacement is identity). (g_scene_w,g_scene_h) is the live
 * client size the CPU-projected bits (stars/threed/space) project against; the projection
 * lives on the main Camera - see gfx_set_scene_fullwindow. */
int g_scene_w = 512;
int g_scene_h = 384;

/* The 2D authoring space is the live client area. Falls back to the fixed canvas size only
 * before the Renderer is up. */
int canvasW() { Renderer* r = platform_renderer(); return r ? r->clientWidth()  : Renderer::CANVAS_WIDTH;  }
int canvasH() { Renderer* r = platform_renderer(); return r ? r->clientHeight() : Renderer::CANVAS_HEIGHT; }

/* Placement of the authored scene onto the back buffer: virtual size + destination offset +
 * scale for the 3D scene pass. The full-window scene authors at the client size, so it fills
 * the window (scale 1, no offset); downscale only when the window is smaller than the canvas,
 * never upscale. */
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

} // namespace

/* =====================================================================
 *  gfx.h scene / viewport seam
 * ===================================================================== */

void gfx_clear_display(void)
{
	/* No-op: ClientEngine::Frame clears the whole back buffer each frame (before the 3D
	 * scene hook), and the 2D composites on top of the full-window 3D scene, so a 2D clear
	 * here would only paint over it. Kept as a call site for the legacy screens. */
}

/* Clip is vestigial now the 2D batch is gone (the native Render2D passes manage their own
 * scissor); kept as no-op call sites for the legacy screens until they are rehomed. */
void gfx_set_clip_region(int, int, int, int) {}
void gfx_set_scene_clip(void) {}

// Refresh the scene size to the live client area and re-issue the main Camera's projection
// for that viewport (the legacy vertical FOV at the current aspect) - safe to call every
// frame. The letterbox is retired, so `on` is vestigial.
void gfx_set_scene_fullwindow(int on)
{
	(void) on;
	Renderer* r = platform_renderer();
	g_scene_w = r ? r->clientWidth()  : 512;
	g_scene_h = r ? r->clientHeight() : 384;

	const float aspect = (g_scene_h > 0) ? static_cast<float>(g_scene_w) / static_cast<float>(g_scene_h) : 4.0f / 3.0f;
	Neuron::Client::MainCamera().SetProjParams(Neuron::Client::LEGACY_SCENE_FOV_Y, aspect,
											   Neuron::Client::SCENE_NEAR_Z, Neuron::Client::SCENE_FAR_Z);
}

// The scene canvas size for the current frame, in logical pixels (the space the CPU-projected
// bits in threed.cpp / space.cpp / stars.cpp project against).
void gfx_scene_size(int* w, int* h)
{
	if (w) *w = g_scene_w;
	if (h) *h = g_scene_h;
}

// The current 2D authoring canvas size in pixels = the live client area.
void gfx_canvas_size(int* w, int* h)
{
	if (w) *w = canvasW();
	if (h) *h = canvasH();
}

/* The 3D scene pass (dust starfield background -> depth-tested ships / planets / sun). The
 * game calls this directly at the end of its world draw, once all models are submitted
 * (Scene3D::SubmitModel) - so it drives the pass itself. No clear (ClientEngine::Frame clears
 * the back buffer once per frame, before the scene hook) and no 2D; the native HUD / menus /
 * GUI composite over it later. Runs unconditionally (drawing the dust even with no models in
 * view), and safely on a null rtv (device lost) - Scene3D still clears the frame's model list. */
void gfx_render_3d_scene(void)
{
	using Neuron::Graphics::Core;

	const CanvasPlacement cp = canvasPlacement();
	Neuron::Graphics::Scene3D::RenderModels(Core::GetRenderTargetView(), Core::GetDepthStencilView(),
											Neuron::Client::MainCamera(), cp.dstX, cp.dstY,
											static_cast<int>(cp.vw * cp.scale), static_cast<int>(cp.vh * cp.scale));
}
