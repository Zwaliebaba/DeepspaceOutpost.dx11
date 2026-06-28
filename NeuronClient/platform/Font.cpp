/*
 * DeepspaceOutpost - DirectX 11 / XAudio2.
 *
 * Font.cpp
 */

#include "pch.h"

#include "Font.h"

#include <vector>

bool Font::load(const char* path, bool colour_font)
{
	loaded_ = false;
	colour_ = colour_font;

	IndexedImage img = load_indexed(path);
	if (!img.ok())
		return false;

	const int W = img.width, H = img.height;
	const uint8_t sep = img.at(0, 0);   /* separator between cells */

	auto rowHasInk = [&](int y) {
		for (int x = 0; x < W; x++)
			if (img.at(x, y) != sep) return true;
		return false;
	};

	/* Row bands. */
	std::vector<std::pair<int,int>> rows;
	for (int y = 0, s = -1; y <= H; y++)
	{
		bool ink = (y < H) && rowHasInk(y);
		if (ink && s < 0) s = y;
		else if (!ink && s >= 0) { rows.push_back({ s, y - 1 }); s = -1; }
	}

	/* The cell-fill colour: top-left of the first cell (the space glyph, which
	 * is entirely cell-fill). Both separator and cell-fill render transparent. */
	uint8_t fill = sep;
	if (!rows.empty())
	{
		int ry0 = rows[0].first;
		for (int x = 0; x < W; x++)
			if (img.at(x, ry0) != sep) { fill = img.at(x, ry0); break; }
	}

	/* Build the RGBA atlas (same dimensions as the sheet). */
	atlas_.width = W;
	atlas_.height = H;
	atlas_.rgba.assign(static_cast<size_t>(W) * H, 0u);
	for (int y = 0; y < H; y++)
	{
		for (int x = 0; x < W; x++)
		{
			uint8_t v = img.at(x, y);
			if (v == sep || v == fill)
				continue;                       /* transparent */
			uint32_t rgb = colour_
				? (static_cast<uint32_t>(img.palette[v][0]) |
				   (static_cast<uint32_t>(img.palette[v][1]) << 8) |
				   (static_cast<uint32_t>(img.palette[v][2]) << 16))
				: 0x00FFFFFFu;                  /* white, tinted at draw time */
			atlas_.rgba[static_cast<size_t>(y) * W + x] = rgb | 0xFF000000u;
		}
	}

	/* Cells: scan each row band for column runs that contain non-separator
	 * pixels. Map them in reading order to ASCII 32.. */
	for (Glyph& g : glyphs_) g = {};
	int code = 32;
	line_height_ = 0;
	for (auto [ry0, ry1] : rows)
	{
		int bandH = ry1 - ry0 + 1;
		if (bandH > line_height_) line_height_ = bandH;

		auto colHasInk = [&](int x) {
			for (int y = ry0; y <= ry1; y++)
				if (img.at(x, y) != sep) return true;
			return false;
		};

		for (int x = 0, s = -1; x <= W; x++)
		{
			bool ink = (x < W) && colHasInk(x);
			if (ink && s < 0) s = x;
			else if (!ink && s >= 0)
			{
				if (code < 128)
					glyphs_[code - 32] = { s, ry0, x - s, bandH };
				code++;
				s = -1;
			}
		}
	}

	loaded_ = true;
	return true;
}

const Glyph& Font::glyph(unsigned char c) const
{
	static const Glyph empty{};
	if (c < 32 || c > 127) return empty;
	return glyphs_[c - 32];
}

int Font::textWidth(const char* s) const
{
	int w = 0;
	for (; s && *s; s++)
		w += glyph(static_cast<unsigned char>(*s)).w + 1;
	return w > 0 ? w - 1 : 0;
}
