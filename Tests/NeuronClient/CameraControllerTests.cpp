#include <gtest/gtest.h>

#include <cmath>

#include "CameraController.h"

using namespace Neuron::Client;

namespace
{
  double Dist3(const double* _a, double _x, double _y, double _z)
  {
    const double dx = _a[0] - _x, dy = _a[1] - _y, dz = _a[2] - _z;
    return std::sqrt(dx * dx + dy * dy + dz * dz);
  }

  CameraInput Frame(float _dt = 1.0f / 60.0f)
  {
    CameraInput in;
    in.dt = _dt;
    in.viewportH = 720.0f;
    in.tanHalfFovY = 0.375f;
    return in;
  }
}

// A snap (SetTarget) moves the focus point immediately and cancels any ease.
TEST(OrbitController, SetTargetSnaps)
{
  OrbitCameraController orbit;
  const double target[3] = {1000.0, 200.0, -3000.0};
  orbit.SetTarget(target);
  EXPECT_NEAR(Dist3(orbit.TargetWorld(), 1000.0, 200.0, -3000.0), 0.0, 1e-6);
}

// FocusOn eases the focus point onto the new target over ~FOCUS_ANIM_SECONDS,
// arriving (not overshooting) after enough frames.
TEST(OrbitController, FocusOnEasesToTarget)
{
  OrbitCameraController orbit;
  const double start[3] = {0.0, 0.0, 0.0};
  orbit.SetTarget(start);

  const double dest[3] = {10000.0, 0.0, 0.0};
  orbit.FocusOn(dest);

  // One short frame in: moved toward the target but not yet arrived.
  orbit.Update(Frame(0.1f));
  const double after1 = orbit.TargetWorld()[0];
  EXPECT_GT(after1, 0.0);
  EXPECT_LT(after1, 10000.0);

  // Drive well past the animation duration: it settles exactly on the target.
  for (int i = 0; i < 120; ++i)
    orbit.Update(Frame(0.1f));
  EXPECT_NEAR(orbit.TargetWorld()[0], 10000.0, 1e-3);
}

// A manual pan cancels an in-flight focus ease (the player took over).
TEST(OrbitController, PanCancelsFocusAnim)
{
  OrbitCameraController orbit;
  const double start[3] = {0.0, 0.0, 0.0};
  orbit.SetTarget(start);
  const double dest[3] = {10000.0, 0.0, 0.0};
  orbit.FocusOn(dest);

  CameraInput in = Frame();
  in.panDX = 20.0f;                 // a pan drag this frame
  orbit.Update(in);

  // The ease is cancelled: further idle frames no longer march toward dest.
  const double afterPan = orbit.TargetWorld()[0];
  for (int i = 0; i < 60; ++i)
    orbit.Update(Frame());
  EXPECT_NEAR(orbit.TargetWorld()[0], afterPan, 1e-6);
  EXPECT_LT(orbit.TargetWorld()[0], 10000.0);   // never reached the focus target
}

// The wheel dollies the orbit distance multiplicatively and clamps.
TEST(OrbitController, WheelZoomClamps)
{
  OrbitCameraController orbit;
  orbit.SetOrbit(0.0f, -0.3f, 900.0);

  CameraInput in = Frame();
  in.wheelSteps = -50.0f;           // zoom way out
  orbit.Update(in);
  EXPECT_LE(orbit.Distance(), 60000.0 + 1e-6);

  in.wheelSteps = 200.0f;           // zoom way in
  orbit.Update(in);
  EXPECT_GE(orbit.Distance(), 150.0 - 1e-6);
}
