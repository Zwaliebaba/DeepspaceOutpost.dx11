#include <gtest/gtest.h>

#include "graphics/InstanceData.h"

using namespace Neuron::Graphics;

namespace
{
  // The identity basis + a translation, the shape ModelDraw hands over.
  const double kIdentity[3][3] = {{1, 0, 0}, {0, 1, 0}, {0, 0, 1}};
}

TEST(InstanceData, PacksBasisRowsAndTranslationInRowVectorLayout)
{
  const double rot[3][3] = {{1, 2, 3}, {4, 5, 6}, {7, 8, 9}};
  const double loc[3] = {10, 20, 30};
  const InstanceData d = PackInstance(rot, loc);

  // Rows 0..2 are the basis with a 0 homogeneous column.
  EXPECT_FLOAT_EQ(d.world[0], 1.0f);
  EXPECT_FLOAT_EQ(d.world[1], 2.0f);
  EXPECT_FLOAT_EQ(d.world[2], 3.0f);
  EXPECT_FLOAT_EQ(d.world[3], 0.0f);
  EXPECT_FLOAT_EQ(d.world[4], 4.0f);
  EXPECT_FLOAT_EQ(d.world[6], 6.0f);
  EXPECT_FLOAT_EQ(d.world[7], 0.0f);
  EXPECT_FLOAT_EQ(d.world[8], 7.0f);
  EXPECT_FLOAT_EQ(d.world[11], 0.0f);

  // Row 3 is the translation with homogeneous 1.
  EXPECT_FLOAT_EQ(d.world[12], 10.0f);
  EXPECT_FLOAT_EQ(d.world[13], 20.0f);
  EXPECT_FLOAT_EQ(d.world[14], 30.0f);
  EXPECT_FLOAT_EQ(d.world[15], 1.0f);
}

TEST(InstanceData, TransformingAModelPointReproducesRotatePlusTranslate)
{
  // Row-vector: p_world = p_model * W. With a 90-deg yaw basis and an offset, the
  // packed matrix must move a model point exactly where rotate-then-translate would.
  const double rot[3][3] = {{0, 0, 1}, {0, 1, 0}, {-1, 0, 0}}; // nose->+x becomes... a yaw
  const double loc[3] = {100, 0, -50};
  const InstanceData d = PackInstance(rot, loc);

  // Model point (1,0,0) times W (row-vector): sum over k of p[k]*row[k], + row3.
  const float px = 1.0f, py = 0.0f, pz = 0.0f;
  const float wx = px * d.world[0] + py * d.world[4] + pz * d.world[8] + d.world[12];
  const float wy = px * d.world[1] + py * d.world[5] + pz * d.world[9] + d.world[13];
  const float wz = px * d.world[2] + py * d.world[6] + pz * d.world[10] + d.world[14];
  // rot row 0 = (0,0,1) -> contributes (0,0,1); plus translation.
  EXPECT_FLOAT_EQ(wx, 0.0f + 100.0f);
  EXPECT_FLOAT_EQ(wy, 0.0f + 0.0f);
  EXPECT_FLOAT_EQ(wz, 1.0f - 50.0f);
}

TEST(InstanceData, DefaultTintKeepsPerFaceColours)
{
  const double loc[3] = {0, 0, 0};
  const InstanceData d = PackInstance(kIdentity, loc);
  EXPECT_LT(d.tint[3], 0.0f); // alpha < 0 -> "use the mesh colours"
}

TEST(InstanceData, OpaqueTintOverrideUnpacksRgbaInPaletteOrder)
{
  const double loc[3] = {0, 0, 0};
  // 0xAABBGGRR: R=0x10, G=0x20, B=0x30, A=0xFF.
  const InstanceData d = PackInstance(kIdentity, loc, 0xFF302010u);
  EXPECT_NEAR(d.tint[0], 0x10 / 255.0f, 1e-5f); // R
  EXPECT_NEAR(d.tint[1], 0x20 / 255.0f, 1e-5f); // G
  EXPECT_NEAR(d.tint[2], 0x30 / 255.0f, 1e-5f); // B
  EXPECT_NEAR(d.tint[3], 1.0f, 1e-5f);          // A
}

TEST(InstanceData, ZeroAlphaTintFallsBackToPerFaceColours)
{
  const double loc[3] = {0, 0, 0};
  const InstanceData d = PackInstance(kIdentity, loc, 0x00FFFFFFu); // a == 0
  EXPECT_LT(d.tint[3], 0.0f);
}
