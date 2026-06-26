/*
 * DeepspaceOutpost - DirectX 11 / XAudio2 port of Elite: The New Kind.
 *
 * Font.h
 *
 * Parses an Allegro grabber font sheet (verd2.pcx = ELITE_1 monochrome body,
 * verd4.pcx = ELITE_2 colour titles) into a glyph atlas. Cells are laid out on
 * a 32px grid, 16 columns x 6 rows = ASCII 32..127. Within each cell the
 * separator colour and the cell-fill colour are transparent; everything else
 * is glyph ink.
 */

#ifndef FONT_H
#define FONT_H

#include "Image.h"

struct Glyph
{
	int x = 0, y = 0, w = 0, h = 0;   /* source rect in the atlas */
};

class Font
{
public:
	/* colour_font=false: ink is stored white (tinted at draw time, ELITE_1).
	 * colour_font=true:  ink keeps its own palette colour (ELITE_2). */
	bool load(const char* path, bool colour_font);

	bool loaded() const { return loaded_; }
	bool isColour() const { return colour_; }
	int  height() const { return line_height_; }

	const Glyph& glyph(unsigned char c) const;
	int  textWidth(const char* s) const;

	const Image& atlas() const { return atlas_; }

private:
	Image  atlas_;
	Glyph  glyphs_[96];      /* ASCII 32..127 */
	int    line_height_ = 0;
	bool   colour_ = false;
	bool   loaded_ = false;
};

#endif /* FONT_H */
