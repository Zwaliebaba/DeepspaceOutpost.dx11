#include "TestFramework.h"

#include "GameLogic.h"

using namespace Neuron;

TEST(FlightInput_ResolveMapsAxesThroughCaps)
{
  GameLogic::Flight f;
  GameLogic::FlightCaps caps;   // defaults: maxRoll/Pitch = 31/256, maxSpeed = 100

  GameLogic::ResolveIntent(f, GameLogic::FlightIntent{ /*roll*/ 1.0, /*pitch*/ 0.0, /*throttle*/ 1.0 }, caps);
  CHECK(f.roll == 31.0 / 256.0);
  CHECK(f.pitch == 0.0);
  CHECK(f.speed == 100.0);

  GameLogic::ResolveIntent(f, GameLogic::FlightIntent{ -1.0, 0.5, 0.5 }, caps);
  CHECK(f.roll == -(31.0 / 256.0));
  CHECK(f.pitch == 0.5 * (31.0 / 256.0));
  CHECK(f.speed == 50.0);
}

TEST(FlightInput_OutOfRangeRequestsAreClampedToTheEnvelope)
{
  GameLogic::Flight f;
  GameLogic::FlightCaps caps;

  // A hostile/overdriven client asking for 5x throttle and 3x roll gets bounded.
  GameLogic::ResolveIntent(f, GameLogic::FlightIntent{ /*roll*/ 3.0, /*pitch*/ -9.0, /*throttle*/ 5.0 }, caps);
  CHECK(f.roll == 31.0 / 256.0);          // clamped to +max
  CHECK(f.pitch == -(31.0 / 256.0));      // clamped to -max
  CHECK(f.speed == 100.0);                // clamped to maxSpeed

  // Negative throttle floors at zero (no reverse via throttle).
  GameLogic::ResolveIntent(f, GameLogic::FlightIntent{ 0.0, 0.0, -2.0 }, caps);
  CHECK(f.speed == 0.0);
}

TEST(FlightInput_ThrottleDrivesForwardMotionThroughTick)
{
  ECS::Registry world;
  ECS::EntityId ship = world.Create();
  world.Add<GameLogic::WorldTransform>(ship, GameLogic::WorldTransform{ { 0, 0, 0 } });
  world.Add<GameLogic::Flight>(ship, GameLogic::Flight{});                 // nose = +z
  world.Add<GameLogic::FlightIntent>(ship, GameLogic::FlightIntent{ 0.0, 0.0, 1.0 });
  // no FlightCaps -> default envelope (maxSpeed 100)

  for (int i = 0; i < 5; ++i)
    GameLogic::Tick(world);

  // StepFlightInput sets speed = 100 from the throttle each tick; 5 ticks * 100.
  CHECK((world.Get<GameLogic::WorldTransform>(ship).position == Math::Vector3i64{ 0, 0, 500 }));
}

TEST(FlightInput_PerShipCapsBoundTopSpeed)
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
  CHECK((world.Get<GameLogic::WorldTransform>(ship).position == Math::Vector3i64{ 0, 0, 50 }));
}

TEST(FlightInput_PitchIntentCurvesPathLikeDirectControl)
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
  CHECK((world.Get<GameLogic::WorldTransform>(ship).position == Math::Vector3i64{ 0, -10, 99 }));
}
