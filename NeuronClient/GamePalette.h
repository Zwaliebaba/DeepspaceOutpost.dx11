#ifndef GAME_PALETTE_H
#define GAME_PALETTE_H

// Named game palette indices and sprite ids (moved verbatim from the retired gfx.h).
//
// GFX_COL_* are indices into the master 256-colour palette (SCANNER_PALETTE /
// Renderer::paletteColour). They key both the 3D model face tables (shipface.cpp) and the
// native 2D HUD colour helpers (space.cpp hud_col). IMG_* are the sprite ids the native HUD
// resolves to .dds files (space.cpp hud_sprite_file). Kept index-based on purpose: the 3D
// renderer resolves the index once, so this stays a compact palette, not per-face RGBA.

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

#endif /* GAME_PALETTE_H */
