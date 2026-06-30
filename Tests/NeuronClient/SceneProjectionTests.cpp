#include <gtest/gtest.h>

#include <cmath>

#include "SceneProjection.h"
#include "ViewMetrics.h"

using namespace Neuron::Client;

namespace
{
  // The GPU projection must land each point on the SAME pixel ProjectPoint does.
  // We compare the matrix+viewport path against ProjectPoint - the legacy oracle -
  // over a grid of camera-space points, at several viewport sizes.
  void ExpectMatchesProjectPoint(const ViewMetrics& _v, double _zNear, double _zFar)
  {
    const Matrix4 proj = MakeScenePerspective(_v, _zNear, _zFar);

    for (double z = 50.0; z <= 5000.0; z *= 2.0)
    {
      for (double x = -400.0; x <= 400.0; x += 100.0)
      {
        for (double y = -300.0; y <= 300.0; y += 100.0)
        {
          double refSx = 0.0, refSy = 0.0;
          ProjectPoint(_v, x, y, z, refSx, refSy);

          double sx = 0.0, sy = 0.0, depth = 0.0;
          ProjectThroughMatrix(proj, _v, x, y, z, sx, sy, depth);

          EXPECT_NEAR(sx, refSx, 1e-6) << "x=" << x << " y=" << y << " z=" << z;
          EXPECT_NEAR(sy, refSy, 1e-6) << "x=" << x << " y=" << y << " z=" << z;
        }
      }
    }
  }
}

TEST(SceneProjection, MatchesProjectPointAtLegacyFourThree)
{
  ExpectMatchesProjectPoint(MakeViewMetrics(512, 384), 1.0, 1.0e6);
}

TEST(SceneProjection, MatchesProjectPointAtWidescreen)
{
  ExpectMatchesProjectPoint(MakeViewMetrics(1920, 1080), 1.0, 1.0e6);
}

TEST(SceneProjection, MatchesProjectPointWithOffCentrePrincipalPoint)
{
  // A deliberately off-centre optic (cx != width/2, cy != height/2): the (0,2)/(1,2)
  // skew terms must keep the projection aligned with ProjectPoint.
  ViewMetrics v;
  v.width = 640;
  v.height = 480;
  v.focal = 500.0;
  v.cx = 300.0; // not 320
  v.cy = 250.0; // not 240
  ExpectMatchesProjectPoint(v, 1.0, 1.0e6);
}

TEST(SceneProjection, DeadAheadProjectsToThePrincipalPoint)
{
  const ViewMetrics v = MakeViewMetrics(1280, 720);
  const Matrix4 proj = MakeScenePerspective(v, 1.0, 1.0e6);

  double sx = 0.0, sy = 0.0, depth = 0.0;
  ProjectThroughMatrix(proj, v, 0.0, 0.0, 1000.0, sx, sy, depth);

  EXPECT_NEAR(sx, v.cx, 1e-6);
  EXPECT_NEAR(sy, v.cy, 1e-6);
}

TEST(SceneProjection, DepthIsZeroAtNearOneAtFarAndMonotonic)
{
  const ViewMetrics v = MakeViewMetrics(1920, 1080);
  const double zNear = 1.0, zFar = 100000.0;
  const Matrix4 proj = MakeScenePerspective(v, zNear, zFar);

  double sx = 0.0, sy = 0.0, depth = 0.0;

  ProjectThroughMatrix(proj, v, 0.0, 0.0, zNear, sx, sy, depth);
  EXPECT_NEAR(depth, 0.0, 1e-9);

  ProjectThroughMatrix(proj, v, 0.0, 0.0, zFar, sx, sy, depth);
  EXPECT_NEAR(depth, 1.0, 1e-9);

  // Strictly increasing with depth (closer objects win the z-test).
  double prev = -1.0;
  for (double z = zNear; z <= zFar; z *= 2.0)
  {
    ProjectThroughMatrix(proj, v, 12.0, -8.0, z, sx, sy, depth);
    EXPECT_GT(depth, prev) << "z=" << z;
    EXPECT_GE(depth, 0.0);
    EXPECT_LE(depth, 1.0);
    prev = depth;
  }
}
