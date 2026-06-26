/*
 * DeepspaceOutpost - DirectX 11 / XAudio2.
 *
 * Image.h
 *
 * Loaders for the loose 8-bit asset files in GameData: Windows BMP (sprites,
 * scanner HUD) and ZSoft PCX (the verd2/verd4 font sheets). Both formats are
 * 8-bit palettised in this game.
 */

#ifndef IMAGE_H
#define IMAGE_H

#include <cstdint>
#include <vector>

/* Decoded true-colour image, 0xAABBGGRR per pixel (R8G8B8A8_UNORM order). */
struct Image
{
	int                   width  = 0;
	int                   height = 0;
	std::vector<uint32_t> rgba;
	bool ok() const { return width > 0 && height > 0; }
};

/* Decoded 8-bit palettised image (used by the font sheet parser, which needs
 * raw indices to separate the cell-fill / separator / ink colours). */
struct IndexedImage
{
	int                  width  = 0;
	int                  height = 0;
	std::vector<uint8_t> idx;       /* width*height, top-down */
	uint8_t              palette[256][3] = {};   /* R,G,B */
	bool ok() const { return width > 0 && height > 0; }
	uint8_t at(int x, int y) const { return idx[static_cast<size_t>(y) * width + x]; }
};

/* Load a BMP or PCX (by extension) to RGBA. When key_index0 is true, source
 * palette index 0 becomes fully transparent (Allegro sprite colour-key). */
Image load_image_rgba(const char* path, bool key_index0);

/* Load a BMP or PCX as raw 8-bit indices + palette. */
IndexedImage load_indexed(const char* path);

#endif /* IMAGE_H */
