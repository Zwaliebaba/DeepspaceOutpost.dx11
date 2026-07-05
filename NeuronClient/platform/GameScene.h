#ifndef GAME_SCENE_H
#define GAME_SCENE_H

// The game<->engine seam (moved from the retired gfx.h; implemented in GameScene.cpp and
// platform_win.cpp). There is no 2D drawing here any more - all 2D is native Render2D
// (RenderGameHud / the GUI overlay). What remains is:
//   - the platform lifecycle (startup / per-frame present / shutdown),
//   - the 3D scene pass (gfx_render_3d_scene -> Scene3D),
//   - the live scene/viewport size + projection the CPU-projected bits read,
//   - vestigial clip / clear no-ops kept as call sites for the legacy screens.

// ---- Platform lifecycle (platform_win.cpp) ----
int  gfx_graphics_startup (void);
void gfx_graphics_shutdown (void);
void gfx_update_screen (void);

// ---- Vestigial 2D-era no-ops (the native passes own their own clear/scissor) ----
void gfx_clear_display (void);
void gfx_set_clip_region (int tx, int ty, int bx, int by);
void gfx_set_scene_clip (void);

/*
 * Render the fully-submitted 3D scene (dust starfield background -> depth-tested models)
 * onto the back buffer. The game calls this at the end of its world draw (once all
 * Scene3D::SubmitModel + dust are submitted), on the already-cleared back buffer, before
 * the native 2D HUD/menus composite over it. Even with no models in view it still draws the
 * dust, so empty space is not black.
 */
void gfx_render_3d_scene (void);

/*
 * Refresh the scene size to the live client area and re-issue the main Camera's projection
 * for that viewport (the legacy vertical field of view at the live aspect) - safe to call
 * every frame; the `on` argument is vestigial (the letterbox is retired). gfx_scene_size()
 * returns that client size - the space the CPU-projected bits (stars/threed/space) draw in.
 * gfx_canvas_size() returns the live 2D authoring size (also the client area).
 */
void gfx_set_scene_fullwindow (int on);
void gfx_scene_size (int *w, int *h);
void gfx_canvas_size (int *w, int *h);

#endif /* GAME_SCENE_H */
