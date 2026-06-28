/*
 * DeepspaceOutpost - DirectX 11 / XAudio2.
 *
 * Image.cpp
 */

#include "pch.h"

#include "Image.h"

#include <cctype>
#include <cstdio>
#include <cstring>
#include <string>

namespace {

std::vector<uint8_t> read_file(const char* path)
{
	std::vector<uint8_t> buf;
	FILE* fp = std::fopen(path, "rb");
	if (!fp) return buf;
	std::fseek(fp, 0, SEEK_END);
	long sz = std::ftell(fp);
	std::fseek(fp, 0, SEEK_SET);
	if (sz > 0)
	{
		buf.resize(static_cast<size_t>(sz));
		if (std::fread(buf.data(), 1, buf.size(), fp) != buf.size())
			buf.clear();
	}
	std::fclose(fp);
	return buf;
}

uint32_t rd32(const std::vector<uint8_t>& b, size_t o)
{
	return static_cast<uint32_t>(b[o]) | (b[o + 1] << 8) | (b[o + 2] << 16) | (b[o + 3] << 24);
}

bool ends_with_ci(const char* s, const char* ext)
{
	std::string a(s); std::string e(ext);
	if (a.size() < e.size()) return false;
	for (size_t i = 0; i < e.size(); i++)
		if (std::tolower(a[a.size() - e.size() + i]) != std::tolower(e[i]))
			return false;
	return true;
}

/* ---- 8-bit BMP (bottom-up) ---- */
IndexedImage load_bmp_indexed(const std::vector<uint8_t>& b)
{
	IndexedImage img;
	if (b.size() < 54 || b[0] != 'B' || b[1] != 'M')
		return img;

	uint32_t dataOff = rd32(b, 10);
	uint32_t infoSz  = rd32(b, 14);
	int      w       = static_cast<int>(rd32(b, 18));
	int      hRaw    = static_cast<int>(rd32(b, 22));
	uint16_t bpp     = static_cast<uint16_t>(b[28] | (b[29] << 8));
	if (bpp != 8 || w <= 0)
		return img;

	bool bottomUp = hRaw > 0;
	int  h = bottomUp ? hRaw : -hRaw;

	size_t palOff = 14 + infoSz;
	uint32_t clrUsed = rd32(b, 46);
	uint32_t count = clrUsed ? clrUsed : 256;
	for (uint32_t i = 0; i < 256 && i < count; i++)
	{
		if (palOff + i * 4 + 2 >= b.size()) break;
		img.palette[i][2] = b[palOff + i * 4 + 0];  /* B */
		img.palette[i][1] = b[palOff + i * 4 + 1];  /* G */
		img.palette[i][0] = b[palOff + i * 4 + 2];  /* R */
	}

	int rowPad = (w + 3) & ~3;
	if (dataOff + static_cast<size_t>(rowPad) * h > b.size())
		return img;

	img.width = w; img.height = h;
	img.idx.resize(static_cast<size_t>(w) * h);
	for (int y = 0; y < h; y++)
	{
		int srcRow = bottomUp ? (h - 1 - y) : y;
		const uint8_t* src = &b[dataOff + static_cast<size_t>(srcRow) * rowPad];
		std::memcpy(&img.idx[static_cast<size_t>(y) * w], src, w);
	}
	return img;
}

/* ---- 8-bit PCX (RLE, top-down) ---- */
IndexedImage load_pcx_indexed(const std::vector<uint8_t>& b)
{
	IndexedImage img;
	if (b.size() < 128 || b[0] != 0x0A)
		return img;

	int xmin = b[4]  | (b[5]  << 8);
	int ymin = b[6]  | (b[7]  << 8);
	int xmax = b[8]  | (b[9]  << 8);
	int ymax = b[10] | (b[11] << 8);
	int nplanes = b[65];
	int bpr     = b[66] | (b[67] << 8);
	int w = xmax - xmin + 1;
	int h = ymax - ymin + 1;
	if (w <= 0 || h <= 0 || nplanes != 1)
		return img;

	std::vector<uint8_t> raw;
	raw.reserve(static_cast<size_t>(bpr) * h);
	size_t i = 128;
	size_t total = static_cast<size_t>(bpr) * h;
	while (raw.size() < total && i < b.size())
	{
		uint8_t c = b[i++];
		if ((c & 0xC0) == 0xC0)
		{
			int cnt = c & 0x3F;
			if (i >= b.size()) break;
			uint8_t val = b[i++];
			for (int k = 0; k < cnt && raw.size() < total; k++)
				raw.push_back(val);
		}
		else
		{
			raw.push_back(c);
		}
	}

	/* Palette: trailing 768 bytes preceded by a 0x0C marker. */
	if (b.size() >= 769 && b[b.size() - 769] == 0x0C)
	{
		size_t p = b.size() - 768;
		for (int k = 0; k < 256; k++)
		{
			img.palette[k][0] = b[p + k * 3 + 0];
			img.palette[k][1] = b[p + k * 3 + 1];
			img.palette[k][2] = b[p + k * 3 + 2];
		}
	}

	img.width = w; img.height = h;
	img.idx.resize(static_cast<size_t>(w) * h);
	for (int y = 0; y < h; y++)
		for (int x = 0; x < w; x++)
			img.idx[static_cast<size_t>(y) * w + x] =
				(static_cast<size_t>(y) * bpr + x < raw.size()) ? raw[static_cast<size_t>(y) * bpr + x] : 0;
	return img;
}

// Minimal DDS reader: UNCOMPRESSED 32-bpp RGBA/BGRA only (enough for the HUD
// textures we author, e.g. Textures/TargetLock.dds). Block-compressed (DXTn/BCn)
// DDS is not decoded here and returns an empty image. The byte order is derived
// from the channel masks, so both A8R8G8B8 (BGRA in memory) and R8G8B8A8 load.
Image load_dds(const std::vector<uint8_t>& b)
{
	Image img;
	if (b.size() < 128 || b[0] != 'D' || b[1] != 'D' || b[2] != 'S' || b[3] != ' ')
		return img;

	auto rd32 = [&](size_t o) -> uint32_t {
		return static_cast<uint32_t>(b[o]) | (static_cast<uint32_t>(b[o + 1]) << 8) |
		       (static_cast<uint32_t>(b[o + 2]) << 16) | (static_cast<uint32_t>(b[o + 3]) << 24);
	};

	const uint32_t height = rd32(12);
	const uint32_t width  = rd32(16);
	const uint32_t pfFlags  = rd32(80);   // DDS_PIXELFORMAT.dwFlags
	const uint32_t fourCC   = rd32(84);
	const uint32_t bitCount = rd32(88);

	const uint32_t DDPF_RGB = 0x40;
	if (fourCC != 0 || (pfFlags & DDPF_RGB) == 0 || bitCount != 32)
		return img;   // compressed or non-32bpp: unsupported

	const uint32_t rMask = rd32(92), gMask = rd32(96), bMask = rd32(100), aMask = rd32(104);
	if (width == 0 || height == 0 || b.size() < 128 + static_cast<size_t>(width) * height * 4)
		return img;

	auto lowShift = [](uint32_t m) -> int {
		int s = 0;
		if (m == 0) return 0;
		while ((m & 0xFF) == 0) { m >>= 8; s += 8; }
		return s;
	};
	const int rs = lowShift(rMask), gs = lowShift(gMask), bs = lowShift(bMask), as = lowShift(aMask);

	img.width  = static_cast<int>(width);
	img.height = static_cast<int>(height);
	img.rgba.resize(static_cast<size_t>(width) * height);

	const uint8_t* p = b.data() + 128;   // pixels follow the 128-byte header (no mips needed)
	for (size_t i = 0; i < img.rgba.size(); i++)
	{
		const uint32_t px = static_cast<uint32_t>(p[i * 4]) | (static_cast<uint32_t>(p[i * 4 + 1]) << 8) |
		                    (static_cast<uint32_t>(p[i * 4 + 2]) << 16) | (static_cast<uint32_t>(p[i * 4 + 3]) << 24);
		const uint32_t R = (px >> rs) & 0xFF;
		const uint32_t G = (px >> gs) & 0xFF;
		const uint32_t B = (px >> bs) & 0xFF;
		const uint32_t A = aMask ? ((px >> as) & 0xFF) : 0xFF;
		img.rgba[i] = R | (G << 8) | (B << 16) | (A << 24);   // 0xAABBGGRR
	}
	return img;
}

} // namespace

IndexedImage load_indexed(const char* path)
{
	std::vector<uint8_t> b = read_file(path);
	if (b.empty()) return {};
	if (ends_with_ci(path, ".pcx")) return load_pcx_indexed(b);
	return load_bmp_indexed(b);
}

Image load_image_rgba(const char* path, bool key_index0)
{
	// True-colour DDS (e.g. the HUD target-lock reticle) loads directly to RGBA;
	// the colour-key only applies to the 8-bit palettised BMP/PCX sprites.
	if (ends_with_ci(path, ".dds"))
		return load_dds(read_file(path));

	IndexedImage ii = load_indexed(path);
	Image img;
	if (!ii.ok())
		return img;

	img.width = ii.width;
	img.height = ii.height;
	img.rgba.resize(ii.idx.size());
	for (size_t i = 0; i < ii.idx.size(); i++)
	{
		uint8_t v = ii.idx[i];
		uint32_t a = (key_index0 && v == 0) ? 0u : 0xFF000000u;
		img.rgba[i] = static_cast<uint32_t>(ii.palette[v][0]) |
		              (static_cast<uint32_t>(ii.palette[v][1]) << 8) |
		              (static_cast<uint32_t>(ii.palette[v][2]) << 16) | a;
	}
	return img;
}
