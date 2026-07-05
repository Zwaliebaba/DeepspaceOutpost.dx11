#pragma once

// RenderTable - the NetType -> render descriptor indirection (client, Track H1).
//
// draw_ship() dispatched by a hand-written if-chain: planet -> lit sphere, sun ->
// billboard, otherwise -> solid mesh. That is the seam every later render track
// plugs into (H2 instancing, H3 iconic-LOD glyphs, H4 tinting), so it becomes a
// TABLE: a NetType resolves to a { kind, glyph, palette-row } descriptor. Adding a
// hull - F3's deployable outpost is the first - is then a data row here, not an edit
// to draw_ship or the mesh provider.
//
// Pure + dependency-light (only the legacy ship-type constants), so the lookup is
// unit-tested headlessly even though the rendering it drives is not.

#include "shipdata.h"   // SHIP_PLANET, SHIP_SUN, NO_OF_SHIPS

// How a NetType is drawn.
enum class RenderKind : int
{
  Hidden = 0,   // not drawn by the mesh path (out-of-range / non-visual types)
  Mesh,         // a solid low-poly hull (the common case)
  Planet,       // the lit green sphere (draw_planet)
  Sun,          // the billboard star (draw_sun)
};

struct RenderDescriptor
{
  RenderKind kind      = RenderKind::Hidden;
  int        glyphId   = 0;   // iconic-LOD glyph id (H3); 0 = the default ship glyph
  int        paletteRow = 0;  // tint row (H4); 0 = the mesh's own face colours
};

// The render descriptor for a NetType (the legacy ship-type int the client carries
// on each local_object). Planet and Sun have their own draw paths; 1..NO_OF_SHIPS
// are solid meshes; anything else is not drawn by this path.
[[nodiscard]] inline RenderDescriptor RenderFor(int _netType)
{
  RenderDescriptor d;
  if (_netType == SHIP_PLANET) { d.kind = RenderKind::Planet; return d; }
  if (_netType == SHIP_SUN)    { d.kind = RenderKind::Sun;    return d; }
  if (_netType >= 1 && _netType <= NO_OF_SHIPS) { d.kind = RenderKind::Mesh; d.glyphId = 1; return d; }
  return d;   // Hidden
}

// H3 iconic LOD: beyond this camera-space depth a hull is too small to read as a
// mesh, so it is drawn as a cheap iconic glyph instead (the tactical-digital look
// and the culling strategy in one). Pure decision, unit-tested; the glyph draw
// itself is the un-CI-testable client path.
inline constexpr double ICONIC_LOD_RANGE = 40000.0;

[[nodiscard]] inline bool ShouldDrawAsGlyph(double _camDepth, double _range = ICONIC_LOD_RANGE)
{
  return _camDepth > _range;
}
