#include <gtest/gtest.h>

#include "RenderTable.h"
#include "shipdata.h"

TEST(RenderTable, PlanetAndSunGetTheirOwnKinds)
{
  EXPECT_EQ(RenderFor(SHIP_PLANET).kind, RenderKind::Planet);
  EXPECT_EQ(RenderFor(SHIP_SUN).kind, RenderKind::Sun);
}

TEST(RenderTable, RealHullsAreMeshes)
{
  EXPECT_EQ(RenderFor(SHIP_VIPER).kind, RenderKind::Mesh);
  EXPECT_EQ(RenderFor(SHIP_MISSILE).kind, RenderKind::Mesh);
  EXPECT_EQ(RenderFor(SHIP_CORIOLIS).kind, RenderKind::Mesh);
  EXPECT_EQ(RenderFor(NO_OF_SHIPS).kind, RenderKind::Mesh);   // the last valid hull id
}

TEST(RenderTable, OutOfRangeTypesAreHidden)
{
  EXPECT_EQ(RenderFor(0).kind, RenderKind::Hidden);
  EXPECT_EQ(RenderFor(NO_OF_SHIPS + 1).kind, RenderKind::Hidden);
  EXPECT_EQ(RenderFor(-99).kind, RenderKind::Hidden);   // a stray negative that isn't planet/sun
}

TEST(RenderTable, MeshDefaultsCarryNoTintOrGlyphOverride)
{
  const RenderDescriptor d = RenderFor(SHIP_VIPER);
  EXPECT_EQ(d.glyphId, 0);
  EXPECT_EQ(d.paletteRow, 0);
}
