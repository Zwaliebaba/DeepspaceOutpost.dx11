#ifndef GFX_H
#define GFX_H

// The retro canvas metrics (GFX_SCALE / GFX_X_CENTRE / GFX_Y_CENTRE) moved to native
// ChartData constants (ChartData.h) - the only thing that still used them - and the
// letterbox that needed the 512x514 canvas is retired, so they are gone from here.

#define GFX_COL_BLACK		0
#define GFX_COL_DARK_RED	28
#define GFX_COL_WHITE		255
#define GFX_COL_GOLD		39
#define GFX_COL_RED			49
#define GFX_COL_CYAN		11

#define GFX_COL_GREY_1		248
#define GFX_COL_GREY_2		235
#define GFX_COL_GREY_3		234
#define GFX_COL_GREY_4		237

#define GFX_COL_BLUE_1		45
#define GFX_COL_BLUE_2		46
#define GFX_COL_BLUE_3		133
#define GFX_COL_BLUE_4		4

#define GFX_COL_RED_3		1
#define GFX_COL_RED_4		71

#define GFX_COL_WHITE_2		242

#define GFX_COL_YELLOW_1	37
#define GFX_COL_YELLOW_2	39
#define GFX_COL_YELLOW_5	251

#define GFX_COL_GREEN_1		2
#define GFX_COL_GREEN_2		17
#define GFX_COL_GREEN_3		86

#define GFX_COL_PINK_1		183

#define IMG_GREEN_DOT		1
#define IMG_RED_DOT			2
#define IMG_BIG_S			3
#define IMG_ELITE_TXT		4
#define IMG_BIG_E			5
#define IMG_MISSILE_GREEN	7
#define IMG_MISSILE_YELLOW	8
#define IMG_MISSILE_RED		9
#define IMG_BLAKE			10
#define IMG_TARGET_LOCK		11
#define IMG_CROSSHAIR		12


int gfx_graphics_startup (void);
void gfx_graphics_shutdown (void);
void gfx_update_screen (void);
void gfx_plot_pixel (int x, int y, int col);
void gfx_draw_filled_circle (int cx, int cy, int radius, int circle_colour);
void gfx_draw_circle (int cx, int cy, int radius, int circle_colour);
void gfx_draw_colour_line (int x1, int y1, int x2, int y2, int line_colour);
void gfx_draw_rectangle (int tx, int ty, int bx, int by, int col);
void gfx_display_text (int x, int y, const char *txt);
void gfx_display_colour_text (int x, int y, const char *txt, int col);
void gfx_display_centre_text (int y, const char *str, int psize, int col);
void gfx_clear_display (void);
void gfx_clear_text_area (void);
void gfx_draw_scanner (void);
void gfx_set_clip_region (int tx, int ty, int bx, int by);
void gfx_draw_sprite (int sprite_no, int x, int y);
void gfx_draw_sprite_scaled (int sprite_no, int x, int y, int w, int h);

/*
 * Render the fully-submitted 3D scene (dust starfield background -> depth-tested models) onto
 * the back buffer. The game calls this at the very end of its world draw (once all
 * Scene3D::SubmitModel + dust have been submitted for the frame), on the already-cleared
 * back buffer, before the 2D HUD/menus composite over it. Even when no models are in view it
 * still draws the dust, so empty space is not black. Replaces the old
 * gfx_finish_render() + g_haveScene flag handshake: the game now drives the pass directly.
 */
void gfx_render_3d_scene (void);

/*
 * Full-window 3D scene + floating HUD (client modernization).
 *
 * The letterbox is retired: the 2D batch and the 3D scene always fill the client window
 * 1:1 (see gfx2d.cpp canvasPlacement). gfx_set_scene_fullwindow() now just refreshes the
 * scene size to the live client area and re-issues the main Camera's projection for that
 * viewport (the legacy vertical field of view at the live aspect ratio) - safe to call
 * every frame; the `on` argument is vestigial. gfx_scene_size() returns that client size -
 * the space the CPU-projected HUD bits draw in. gfx_set_scene_clip() clips to the whole
 * window. gfx_set_draw_origin() floats a layout block by offsetting every emitted
 * coordinate (the flight HUD dashboard uses it to sit bottom-centre).
 */
void gfx_set_scene_fullwindow (int on);
void gfx_scene_size (int *w, int *h);
void gfx_set_draw_origin (int x, int y);
void gfx_set_scene_clip (void);

/* Current 2D authoring canvas size in pixels = the live client area (the letterbox is
 * retired). Screens read this to anchor content to the window edges. Either pointer may
 * be null. */
void gfx_canvas_size (int *w, int *h);

#endif
