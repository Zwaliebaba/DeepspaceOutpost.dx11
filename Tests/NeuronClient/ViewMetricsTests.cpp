#include <gtest/gtest.h>

#include <cmath>

#include "ViewMetrics.h"

using namespace Neuron::Client;

TEST(ViewMetrics, ReproducesLegacyOpticsAtFourThree)
{
  // The legacy 3D play area is 512x384 canvas pixels: focal 512, centre (256,192).
  ViewMetrics v = MakeViewMetrics(512, 384);
  EXPECT_NEAR(v.focal, 512.0, 1e-9);
  EXPECT_NEAR(v.cx, 256.0, 1e-9);
  EXPECT_NEAR(v.cy, 192.0, 1e-9);

  // And a sample point projects bit-for-bit like the old inline math
  // (rx*256/rz + 128) * GFX_SCALE, with sy negated then offset.
  const double rx = 30.0, ry = -12.0, rz = 400.0;
  double legSx = (rx * 256.0) / rz; double legSy = (ry * 256.0) / rz; legSy = -legSy;
  legSx = (legSx + 128.0) * 2.0; legSy = (legSy + 96.0) * 2.0;

  double sx, sy;
  ProjectPoint(v, rx, ry, rz, sx, sy);
  EXPECT_NEAR(sx, legSx, 1e-9);
  EXPECT_NEAR(sy, legSy, 1e-9);
}

TEST(ViewMetrics, WidescreenKeepsVerticalFovAndWidensHorizontally)
{
  ViewMetrics w = MakeViewMetrics(1920, 1080);
  EXPECT_NEAR(w.cx, 960.0, 1e-9);
  EXPECT_NEAR(w.cy, 540.0, 1e-9);

  const double fovY = 2.0 * std::atan((w.height * 0.5) / w.focal);
  const double fovX = 2.0 * std::atan((w.width  * 0.5) / w.focal);
  // Vertical field of view is unchanged from the legacy optic...
  EXPECT_NEAR(fovY, 2.0 * std::atan(kLegacyTanHalfFovY), 1e-9);
  // ...and the wider window shows more world horizontally (no stretch).
  EXPECT_GT(fovX, fovY);
}

TEST(ViewMetrics, DeadAheadProjectsToTheViewportCentreAtAnyResolution)
{
  for (const auto wh : { std::pair{512, 384}, std::pair{1920, 1080}, std::pair{800, 1200} })
  {
    ViewMetrics v = MakeViewMetrics(wh.first, wh.second);
    double sx, sy;
    ProjectPoint(v, 0.0, 0.0, 1000.0, sx, sy);
    EXPECT_NEAR(sx, v.width * 0.5, 1e-9);
    EXPECT_NEAR(sy, v.height * 0.5, 1e-9);
  }
}

TEST(ViewMetrics, FrustumExtentMatchesTheProjectionEdges)
{
  ViewMetrics v = MakeViewMetrics(1920, 1080);
  const double z = 500.0;

  double sx, sy;
  ProjectPoint(v, HalfExtentX(v, z), 0.0, z, sx, sy);
  EXPECT_NEAR(sx, static_cast<double>(v.width), 1e-6);   // +half-extent -> right edge

  ProjectPoint(v, 0.0, HalfExtentY(v, z), z, sx, sy);
  EXPECT_NEAR(sy, 0.0, 1e-6);                             // +y (up) -> top edge (y grows down)
}
