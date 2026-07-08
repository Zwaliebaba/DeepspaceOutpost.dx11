#include <gtest/gtest.h>

#include "GameLogic.h"

using namespace Neuron;

// StepLaunchCruise drives a freshly-undocked hull straight out of the station bay,
// overriding the player's (normally zero) FlightIntent, until it has cleared
// LaunchCruise::distance from the launch origin - then it removes the component and
// drops the throttle so the player regains control.
//
// The happy-path fly-out through the full GameLogic::Tick is covered by
// FlightInput.LaunchCruiseFliesOutThenReleases. These cases exercise the parts of
// StepLaunchCruise a single +z fly-out does not: the per-axis Chebyshev clearance
// test, the intent override while cruising, the drop when the hull loses its flight
// state, and multiple hulls launching in the same tick. They call StepLaunchCruise
// directly (it only reads WorldTransform and writes FlightIntent) and set the
// position by hand, so the distance branch is tested without running the integrator.

namespace
{
  // Spawn a launching hull at `_pos` with `_origin`/`_distance` and a player intent
  // that is deliberately non-zero, so the override is observable.
  ECS::EntityId SpawnLaunching(ECS::Registry& _world, Math::Vector3i64 _pos,
                               Math::Vector3i64 _origin, int64_t _distance,
                               double _throttle = 0.35)
  {
    const ECS::EntityId id = _world.Create();
    _world.Add<GameLogic::WorldTransform>(id, GameLogic::WorldTransform{ _pos });
    _world.Add<GameLogic::FlightIntent>(id, GameLogic::FlightIntent{ /*roll*/ 0.9, /*pitch*/ 0.9, /*throttle*/ 0.0 });
    _world.Add<GameLogic::LaunchCruise>(id, GameLogic::LaunchCruise{ _origin, _distance, _throttle });
    return id;
  }
}

TEST(LaunchSystem, CruisingHullOverridesPlayerIntent)
{
  ECS::Registry world;
  // Far from clearing the bay (distance huge), so it stays in the cruise branch.
  const ECS::EntityId ship = SpawnLaunching(world, { 0, 0, 10 }, { 0, 0, 0 }, /*distance*/ 1'000'000, /*throttle*/ 0.35);

  GameLogic::StepLaunchCruise(world);

  ASSERT_TRUE(world.Has<GameLogic::LaunchCruise>(ship));   // still launching
  const GameLogic::FlightIntent& intent = world.Get<GameLogic::FlightIntent>(ship);
  EXPECT_EQ(intent.rollAxis, 0.0);        // player roll wiped: straight, level cruise
  EXPECT_EQ(intent.pitchAxis, 0.0);       // player pitch wiped
  EXPECT_EQ(intent.throttle, 0.35);       // forced to the launch throttle
}

TEST(LaunchSystem, ReleasesWhenClearedAlongZ)
{
  ECS::Registry world;
  // Already sitting exactly `distance` out along +z: it should release immediately.
  const ECS::EntityId ship = SpawnLaunching(world, { 0, 0, 1000 }, { 0, 0, 0 }, /*distance*/ 1000);

  GameLogic::StepLaunchCruise(world);

  EXPECT_FALSE(world.Has<GameLogic::LaunchCruise>(ship));         // launch complete
  EXPECT_EQ(world.Get<GameLogic::FlightIntent>(ship).throttle, 0.0);  // coast to rest
}

TEST(LaunchSystem, ClearanceUsesChebyshevAcrossEveryAxis)
{
  // The outward distance is a Chebyshev (max-abs) distance, so clearing along ANY
  // single axis - and in either direction - counts, not just +z.
  const Math::Vector3i64 origin{ 100, 100, 100 };
  const int64_t distance = 500;

  struct Case { Math::Vector3i64 pos; const char* axis; };
  const Case cleared[] = {
    { { 100 + 500, 100, 100 }, "+x" },
    { { 100 - 500, 100, 100 }, "-x" },
    { { 100, 100 + 600, 100 }, "+y" },
    { { 100, 100 - 700, 100 }, "-y" },
    { { 100, 100, 100 + 500 }, "+z" },
    { { 100, 100, 100 - 900 }, "-z" },
  };
  for (const Case& c : cleared)
  {
    ECS::Registry world;
    const ECS::EntityId ship = SpawnLaunching(world, c.pos, origin, distance);
    GameLogic::StepLaunchCruise(world);
    EXPECT_FALSE(world.Has<GameLogic::LaunchCruise>(ship)) << "should release on axis " << c.axis;
  }

  // One unit short on the furthest axis is NOT cleared yet.
  ECS::Registry world;
  const ECS::EntityId ship = SpawnLaunching(world, { 100, 100 - 499, 100 }, origin, distance);
  GameLogic::StepLaunchCruise(world);
  EXPECT_TRUE(world.Has<GameLogic::LaunchCruise>(ship));   // dy = 499 < 500
}

TEST(LaunchSystem, DropsLaunchWhenHullLosesFlightIntent)
{
  ECS::Registry world;
  // A hull that carries LaunchCruise + WorldTransform but no FlightIntent has lost
  // its flight state; the launch must be dropped rather than dereferencing null.
  const ECS::EntityId ship = world.Create();
  world.Add<GameLogic::WorldTransform>(ship, GameLogic::WorldTransform{ { 0, 0, 0 } });
  world.Add<GameLogic::LaunchCruise>(ship, GameLogic::LaunchCruise{ { 0, 0, 0 }, 1000, 0.35 });

  GameLogic::StepLaunchCruise(world);

  EXPECT_FALSE(world.Has<GameLogic::LaunchCruise>(ship));   // dropped, no crash
}

TEST(LaunchSystem, DropsLaunchWhenHullLosesTransform)
{
  ECS::Registry world;
  const ECS::EntityId ship = world.Create();
  world.Add<GameLogic::FlightIntent>(ship, GameLogic::FlightIntent{});
  world.Add<GameLogic::LaunchCruise>(ship, GameLogic::LaunchCruise{ { 0, 0, 0 }, 1000, 0.35 });

  GameLogic::StepLaunchCruise(world);

  EXPECT_FALSE(world.Has<GameLogic::LaunchCruise>(ship));   // dropped, no crash
}

TEST(LaunchSystem, HandlesMultipleHullsLaunchingSameTick)
{
  ECS::Registry world;
  // One hull has already cleared, one is still cruising: removing the finished one
  // mid-iteration must not disturb the other.
  const ECS::EntityId arrived = SpawnLaunching(world, { 0, 0, 2000 }, { 0, 0, 0 }, /*distance*/ 2000);
  const ECS::EntityId cruising = SpawnLaunching(world, { 0, 0, 50 },  { 0, 0, 0 }, /*distance*/ 2000, /*throttle*/ 0.5);

  GameLogic::StepLaunchCruise(world);

  EXPECT_FALSE(world.Has<GameLogic::LaunchCruise>(arrived));       // released
  EXPECT_EQ(world.Get<GameLogic::FlightIntent>(arrived).throttle, 0.0);

  ASSERT_TRUE(world.Has<GameLogic::LaunchCruise>(cruising));       // untouched, still launching
  EXPECT_EQ(world.Get<GameLogic::FlightIntent>(cruising).throttle, 0.5);
}

TEST(LaunchSystem, StationUndockOffsetIsClearedByTheFlyOut)
{
  // The station Undock handler seeds LaunchCruise with LAUNCH_OFFSET; a hull that
  // has travelled exactly that far is done.
  ECS::Registry world;
  const ECS::EntityId ship = SpawnLaunching(world, { 0, 0, GameLogic::LAUNCH_OFFSET }, { 0, 0, 0 },
                                            GameLogic::LAUNCH_OFFSET);
  GameLogic::StepLaunchCruise(world);
  EXPECT_FALSE(world.Has<GameLogic::LaunchCruise>(ship));
}
