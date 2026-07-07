#include <gtest/gtest.h>

#include "GameLogic.h"

using namespace Neuron;

TEST(FlightInput, ResolveMapsAxesThroughCaps)
{
  GameLogic::Flight f;
  GameLogic::FlightCaps caps;   // defaults: maxRoll/Pitch = 31/256, maxSpeed = 60

  GameLogic::ResolveIntent(f, GameLogic::FlightIntent{ /*roll*/ 1.0, /*pitch*/ 0.0, /*throttle*/ 1.0 }, caps);
  EXPECT_TRUE(f.roll == 31.0 / 256.0);
  EXPECT_TRUE(f.pitch == 0.0);
  EXPECT_TRUE(f.speed == 60.0);

  GameLogic::ResolveIntent(f, GameLogic::FlightIntent{ -1.0, 0.5, 0.5 }, caps);
  EXPECT_TRUE(f.roll == -(31.0 / 256.0));
  EXPECT_TRUE(f.pitch == 0.5 * (31.0 / 256.0));
  EXPECT_TRUE(f.speed == 30.0);
}

TEST(FlightInput, OutOfRangeRequestsAreClampedToTheEnvelope)
{
  GameLogic::Flight f;
  GameLogic::FlightCaps caps;

  // A hostile/overdriven client asking for 5x throttle and 3x roll gets bounded.
  GameLogic::ResolveIntent(f, GameLogic::FlightIntent{ /*roll*/ 3.0, /*pitch*/ -9.0, /*throttle*/ 5.0 }, caps);
  EXPECT_TRUE(f.roll == 31.0 / 256.0);          // clamped to +max
  EXPECT_TRUE(f.pitch == -(31.0 / 256.0));      // clamped to -max
  EXPECT_TRUE(f.speed == 60.0);                 // clamped to maxSpeed

  // Negative throttle floors at zero (no reverse via throttle).
  GameLogic::ResolveIntent(f, GameLogic::FlightIntent{ 0.0, 0.0, -2.0 }, caps);
  EXPECT_TRUE(f.speed == 0.0);
}

TEST(FlightInput, ThrottleDrivesForwardMotionThroughTick)
{
  ECS::Registry world;
  ECS::EntityId ship = world.Create();
  world.Add<GameLogic::WorldTransform>(ship, GameLogic::WorldTransform{ { 0, 0, 0 } });
  world.Add<GameLogic::Flight>(ship, GameLogic::Flight{});                 // nose = +z
  world.Add<GameLogic::FlightIntent>(ship, GameLogic::FlightIntent{ 0.0, 0.0, 1.0 });
  // no FlightCaps -> default envelope (maxSpeed 60)

  for (int i = 0; i < 5; ++i)
    GameLogic::Tick(world);

  // StepFlightInput sets speed = 60 from the throttle each tick; 5 ticks * 60.
  EXPECT_TRUE((world.Get<GameLogic::WorldTransform>(ship).position == Math::Vector3i64{ 0, 0, 300 }));
}

TEST(FlightInput, PerShipCapsBoundTopSpeed)
{
  ECS::Registry world;
  ECS::EntityId ship = world.Create();
  world.Add<GameLogic::WorldTransform>(ship, GameLogic::WorldTransform{ { 0, 0, 0 } });
  world.Add<GameLogic::Flight>(ship, GameLogic::Flight{});
  world.Add<GameLogic::FlightIntent>(ship, GameLogic::FlightIntent{ 0.0, 0.0, 10.0 });  // full throttle, over-driven
  world.Add<GameLogic::FlightCaps>(ship, GameLogic::FlightCaps{ 0.1, 0.1, /*maxSpeed*/ 10.0 });

  for (int i = 0; i < 5; ++i)
    GameLogic::Tick(world);

  // Throttle clamps to 1.0, * this ship's maxSpeed 10 = 10/tick; 5 ticks = 50.
  EXPECT_TRUE((world.Get<GameLogic::WorldTransform>(ship).position == Math::Vector3i64{ 0, 0, 50 }));
}

TEST(FlightInput, LaunchCruiseFliesOutThenReleases)
{
  ECS::Registry world;
  ECS::EntityId ship = world.Create();
  world.Add<GameLogic::WorldTransform>(ship, GameLogic::WorldTransform{ { 0, 0, 0 } });
  world.Add<GameLogic::Flight>(ship, GameLogic::Flight{});                 // nose = +z
  world.Add<GameLogic::FlightIntent>(ship, GameLogic::FlightIntent{});     // player throttle 0
  // no FlightCaps -> default envelope (maxSpeed 60)
  world.Add<GameLogic::LaunchCruise>(ship,
      GameLogic::LaunchCruise{ { 0, 0, 0 }, GameLogic::LAUNCH_OFFSET, 0.35 });

  // Even with the player's throttle at zero, the launch cruise carries the hull
  // outward each tick until it clears the bay, then removes itself.
  bool released = false;
  for (int i = 0; i < 400 && !released; ++i)
  {
    GameLogic::Tick(world);
    released = !world.Has<GameLogic::LaunchCruise>(ship);
  }
  ASSERT_TRUE(released);   // it finished the fly-out and handed control back
  EXPECT_GE(world.Get<GameLogic::WorldTransform>(ship).position.z, GameLogic::LAUNCH_OFFSET);
  EXPECT_EQ(world.Get<GameLogic::FlightIntent>(ship).throttle, 0.0);   // control returned, at rest
}

TEST(FlightInput, PitchIntentCurvesPathLikeDirectControl)
{
  ECS::Registry world;
  ECS::EntityId ship = world.Create();
  world.Add<GameLogic::WorldTransform>(ship, GameLogic::WorldTransform{ { 0, 0, 0 } });
  world.Add<GameLogic::Flight>(ship, GameLogic::Flight{});
  world.Add<GameLogic::FlightIntent>(ship, GameLogic::FlightIntent{ 0.0, /*pitch*/ 1.0, /*throttle*/ 1.0 });
  world.Add<GameLogic::FlightCaps>(ship, GameLogic::FlightCaps{ 0.1, /*maxPitchRate*/ 0.1, /*maxSpeed*/ 100.0 });

  GameLogic::Tick(world);

  // Resolves to pitch = 0.1, speed = 100 - identical to the direct-control pitch
  // golden: nose -> normalize(0,-0.1,0.99), *100 truncates to (0,-10,99).
  EXPECT_TRUE((world.Get<GameLogic::WorldTransform>(ship).position == Math::Vector3i64{ 0, -10, 99 }));
}
