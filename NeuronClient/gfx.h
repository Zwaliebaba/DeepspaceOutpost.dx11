#ifndef GFX_H
#define GFX_H

#include "ViewMetrics.h"

#ifdef RES_512_512

#define GFX_SCALE		(2)
#define GFX_X_OFFSET	(0)
#define GFX_Y_OFFSET	(0)
#define GFX_X_CENTRE	(256)
#define GFX_Y_CENTRE	(192)

#define GFX_VIEW_TX		1
#define GFX_VIEW_TY		1
#define GFX_VIEW_BX		509
#define GFX_VIEW_BY		381

#endif

#ifdef RES_800_600

/*
 * The DX11 port renders into a fixed 512x514 logical canvas and the present
 * step centres/letterboxes it in the window, so the historical pixel offsets
 * that centred the canvas within an 800x600 Allegro surface are now zero. The
 * game logic (and threed.c) add these to coordinates; keeping them at 0 makes
 * every coordinate canvas-local and consistent across all primitives.
 */
#define GFX_SCALE		(2)
#define GFX_X_OFFSET	(0)
#define GFX_Y_OFFSET	(0)
#define GFX_X_CENTRE	(256)
#define GFX_Y_CENTRE	(192)

#define GFX_VIEW_TX		1
#define GFX_VIEW_TY		1
#define GFX_VIEW_BX		509
#define GFX_VIEW_BY		381

#endif

#ifndef GFX_SCALE

#define GFX_SCALE		(1)
#define GFX_X_OFFSET	(0)
#define GFX_Y_OFFSET	(0)
#define GFX_X_CENTRE	(128)
#define GFX_Y_CENTRE	(96)

#define GFX_VIEW_TX		1
#define GFX_VIEW_TY		1
#define GFX_VIEW_BX		253
#define GFX_VIEW_BY		191

#endif
 


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
#define GFX_COL_YELLOW_3	89
#define GFX_COL_YELLOW_4	160
#define GFX_COL_YELLOW_5	251

#define GFX_ORANGE_1		76
#define GFX_ORANGE_2		77
#define GFX_ORANGE_3		122

#define GFX_COL_GREEN_1		2
#define GFX_COL_GREEN_2		17
#define GFX_COL_GREEN_3		86

#define GFX_COL_PINK_1		183

#define IMG_GREEN_DOT		1
#define IMG_RED_DOT			2
#define IMG_BIG_S			3
#define IMG_ELITE_TXT		4
#define IMG_BIG_E			5
#define IMG_DICE			6
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
void gfx_draw_line (int x1, int y1, int x2, int y2);
void gfx_draw_colour_line (int x1, int y1, int x2, int y2, int line_colour);
void gfx_draw_triangle (int x1, int y1, int x2, int y2, int x3, int y3, int col);
void gfx_draw_rectangle (int tx, int ty, int bx, int by, int col);
void gfx_display_text (int x, int y, const char *txt);
void gfx_display_colour_text (int x, int y, const char *txt, int col);
void gfx_display_centre_text (int y, const char *str, int psize, int col);
void gfx_clear_display (void);
void gfx_clear_text_area (void);
void gfx_clear_area (int tx, int ty, int bx, int by);
void gfx_display_pretty_text (int tx, int ty, int bx, int by, const char *txt);
void gfx_draw_scanner (void);
void gfx_set_clip_region (int tx, int ty, int bx, int by);
void gfx_draw_sprite (int sprite_no, int x, int y);
void gfx_draw_sprite_scaled (int sprite_no, int x, int y, int w, int h);
void gfx_render_line (int x1, int y1, int x2, int y2, int dist, int col);
void gfx_finish_render (void);

/*
 * Toggle XOR drawing mode for subsequent line draws (used to draw/erase the
 * chart cross-hairs). Provided by the platform graphics layer; replaces the
 * Allegro xor_mode() the game previously pulled in via allegro.h.
 */
void xor_mode (int on);

/*
 * Full-window 3D scene + floating HUD (client modernization).
 *
 * gfx_set_scene_fullwindow() picks, per frame, whether the in-flight 3D fills
 * the whole window (aspect-aware optics) or the legacy letterboxed 512x514
 * canvas is used (menus/charts/station). gfx_view_metrics() returns the optics
 * the software projection should use. gfx_set_scene_clip() sets the play-area
 * clip for the current mode. gfx_hud_anchor()/gfx_set_draw_origin() float the
 * legacy HUD layout to the bottom-centre of the window when full-window.
 */
void gfx_set_scene_fullwindow (int on);
const Neuron::Client::ViewMetrics& gfx_view_metrics (void);
void gfx_set_draw_origin (int x, int y);
void gfx_hud_anchor (int *ox, int *oy);
void gfx_set_scene_clip (void);

/*
 * General layout anchor (client-space UI migration, Phase 1). Computes the draw
 * origin (top-left, in current-canvas pixels) that places a w x h layout block at
 * `where` within the current canvas rect - the client area in full-window/client-space
 * mode, the 512x514 canvas in retro - plus a (dx,dy) nudge in canvas pixels (+x right,
 * +y down). The block is authored in its own 0..w / 0..h local space: call
 * gfx_set_draw_origin(*ox,*oy), draw it, then gfx_set_draw_origin(0,0). Origins are
 * clamped to >= 0 so an oversized block stays pinned to the top-left. gfx_hud_anchor is
 * the bottom-centre 512x514 case of this.
 */
enum gfx_anchor_point
{
	GFX_ANCHOR_TOP_LEFT,    GFX_ANCHOR_TOP,     GFX_ANCHOR_TOP_RIGHT,
	GFX_ANCHOR_LEFT,        GFX_ANCHOR_CENTRE,  GFX_ANCHOR_RIGHT,
	GFX_ANCHOR_BOTTOM_LEFT, GFX_ANCHOR_BOTTOM,  GFX_ANCHOR_BOTTOM_RIGHT
};
void gfx_anchor (gfx_anchor_point where, int w, int h, int dx, int dy, int *ox, int *oy);

/* Current 2D authoring canvas size in pixels: the client area in full-window/client-space
 * mode, the fixed 512x514 canvas in retro. Screens migrating to client-space read this to
 * anchor content to the window edges. Either pointer may be null. */
void gfx_canvas_size (int *w, int *h);

#endif
