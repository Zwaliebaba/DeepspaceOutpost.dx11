#include <gtest/gtest.h>

#include <cmath>

#include "input/MoveGizmo.h"

using namespace Neuron::Input;

namespace
{
  constexpr double kEps = 1e-6;
  void ExpectNear(GVec3 _a, double _bx, double _by, double _bz, double _eps = 1e-4)
  {
    EXPECT_NEAR(_a.x, _bx, _eps);
    EXPECT_NEAR(_a.y, _by, _eps);
    EXPECT_NEAR(_a.z, _bz, _eps);
  }
}

TEST(MoveGizmo, RayStraightDownHitsThePlaneAtGroundZero)
{
  // Ship at origin, plane normal +y (camera up). A ray from above aiming -y
  // straight down through (10,*,20) lands on the plane at (10,0,20).
  const GVec3 ship{0, 0, 0};
  const GVec3 up{0, 1, 0};
  const GVec3 rayO{10, 100, 20};
  const GVec3 rayD{0, -1, 0};
  const PlaneHit h = RayPlanePoint(rayO, rayD, ship, up);
  EXPECT_FALSE(h.clampedToRange);
  ExpectNear(h.point, 10, 0, 20);
}

TEST(MoveGizmo, PlaneRidesTheShipHeight)
{
  // Ship lifted to y=500; the plane is at y=500, so a downward ray lands there.
  const GVec3 ship{0, 500, 0};
  const GVec3 up{0, 1, 0};
  const PlaneHit h = RayPlanePoint({5, 900, -5}, {0, -1, 0}, ship, up);
  EXPECT_FALSE(h.clampedToRange);
  EXPECT_NEAR(h.point.y, 500.0, 1e-4);
}

TEST(MoveGizmo, GrazingRayClampsToMaxRangeNotInfinity)
{
  // Ray parallel to the plane (dir has no +y component) never intersects.
  const GVec3 ship{0, 0, 0};
  const GVec3 up{0, 1, 0};
  const GVec3 rayO{0, 10, 0};
  const GVec3 rayD{1, 0, 0}; // parallel to the plane
  const PlaneHit h = RayPlanePoint(rayO, rayD, ship, up, 500000.0);
  EXPECT_TRUE(h.clampedToRange);
  // Clamped along the in-plane projection (+x) at the max range.
  EXPECT_NEAR(Length(h.point - ship), 500000.0, 1.0);
  EXPECT_NEAR(h.point.x, 500000.0, 1.0);
}

TEST(MoveGizmo, RayPointingAwayFromPlaneClamps)
{
  // Plane behind the ray (t <= 0): aim upward while above the plane.
  const GVec3 ship{0, 0, 0};
  const GVec3 up{0, 1, 0};
  const PlaneHit h = RayPlanePoint({0, 10, 0}, {0.2, 1.0, 0.0}, ship, up);
  EXPECT_TRUE(h.clampedToRange);
}

TEST(MoveGizmo, DistantIntersectionClampsToMaxRange)
{
  // A shallow ray that DOES hit but very far out is pulled back to max range.
  const GVec3 ship{0, 0, 0};
  const GVec3 up{0, 1, 0};
  const GVec3 rayO{0, 1, 0};
  const GVec3 rayD{1000, -0.001, 0}; // shallow: reaches the plane ~1e6 out in x
  const PlaneHit h = RayPlanePoint(rayO, rayD, ship, up, 500000.0);
  EXPECT_TRUE(h.clampedToRange);
  EXPECT_NEAR(Length(h.point - ship), 500000.0, 1.0);
}

TEST(MoveGizmo, ElevationSlidesAlongTheNormal)
{
  const GVec3 up{0, 1, 0};
  const GVec3 pt{100, 0, 200};
  const GVec3 raised = ApplyElevation(pt, up, 250.0);
  ExpectNear(raised, 100, 250, 200);
  const GVec3 lowered = ApplyElevation(pt, up, -80.0);
  EXPECT_NEAR(lowered.y, -80.0, 1e-4);
}

TEST(MoveGizmo, ElevationUsesTheActualCameraUp)
{
  // Non-axis-aligned up: elevation moves along it, unit-normalized.
  GVec3 up{0, 3, 4}; // length 5
  const GVec3 pt{0, 0, 0};
  const GVec3 raised = ApplyElevation(pt, up, 10.0);
  EXPECT_NEAR(raised.y, 6.0, 1e-4);  // 10 * 3/5
  EXPECT_NEAR(raised.z, 8.0, 1e-4);  // 10 * 4/5
}

TEST(MoveGizmo, ReachClampIsChebyshevRelativeToShip)
{
  const GVec3 ship{1000, 2000, 3000};
  const GVec3 dest{1000 + 5000000.0, 2000 - 200.0, 3000 + 999999.0};
  const GVec3 c = ClampReach(dest, ship, 1000000.0);
  EXPECT_NEAR(c.x, ship.x + 1000000.0, kEps); // x clamped
  EXPECT_NEAR(c.y, ship.y - 200.0, kEps);     // y within reach, untouched
  EXPECT_NEAR(c.z, ship.z + 999999.0, kEps);  // z within reach, untouched
}

TEST(MoveGizmo, FullPipelineComposesPlaneElevationAndClamp)
{
  const GVec3 ship{0, 0, 0};
  const GVec3 up{0, 1, 0};
  // Straight-down ray to (300,*,400), lift 150 -> (300,150,400), within reach.
  const GVec3 t = ComputeMoveTarget({300, 100, 400}, {0, -1, 0}, ship, up, 150.0);
  ExpectNear(t, 300, 150, 400);
}
