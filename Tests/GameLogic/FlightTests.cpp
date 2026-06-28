#include <gtest/gtest.h>

#include <cmath>

#include "GameLogic.h"

using namespace Neuron;

namespace
{
  GameLogic::Flight MakeFlight(double _roll, double _pitch, double _speed)
  {
    GameLogic::Flight f;   // identity basis: nose = +z
    f.roll = _roll;
    f.pitch = _pitch;
    f.speed = _speed;
    return f;
  }
}

TEST(Flight, StraightAheadAdvancesAlongNose)
{
  ECS::Registry world;
  ECS::EntityId ship = world.Create();
  world.Add<GameLogic::WorldTransform>(ship, GameLogic::WorldTransform{ { 0, 0, 0 } });
  world.Add<GameLogic::Flight>(ship, MakeFlight(/*roll*/ 0.0, /*pitch*/ 0.0, /*speed*/ 100.0));

  for (int i = 0; i < 5; ++i)
    GameLogic::Tick(world);

  // No rotation: nose stays (0,0,1), so 5 ticks * 100 along z.
  EXPECT_TRUE((world.Get<GameLogic::WorldTransform>(ship).position == Math::Vector3i64{ 0, 0, 500 }));
}

TEST(Flight, RollDoesNotMoveTheNoseOrTranslate)
{
  ECS::Registry world;
  ECS::EntityId ship = world.Create();
  world.Add<GameLogic::WorldTransform>(ship, GameLogic::WorldTransform{ { 0, 0, 0 } });
  world.Add<GameLogic::Flight>(ship, MakeFlight(/*roll*/ 0.1, /*pitch*/ 0.0, /*speed*/ 0.0));

  for (int i = 0; i < 20; ++i)
    GameLogic::Tick(world);

  const GameLogic::Flight& f = world.Get<GameLogic::Flight>(ship);
  // Rolling spins the basis about the nose: the nose itself is unchanged...
  EXPECT_TRUE(std::fabs(f.nose.x) < 1e-12);
  EXPECT_TRUE(std::fabs(f.nose.y) < 1e-12);
  EXPECT_TRUE(std::fabs(f.nose.z - 1.0) < 1e-12);
  // ...the side vector has rotated off the +x axis (roll is doing something)...
  EXPECT_TRUE(f.side.y < 0.0);
  // ...and with zero speed the ship has not moved.
  EXPECT_TRUE((world.Get<GameLogic::WorldTransform>(ship).position == Math::Vector3i64{ 0, 0, 0 }));
}

TEST(Flight, PitchTiltsNoseAndCurvesPath)
{
  ECS::Registry world;
  ECS::EntityId ship = world.Create();
  world.Add<GameLogic::WorldTransform>(ship, GameLogic::WorldTransform{ { 0, 0, 0 } });
  world.Add<GameLogic::Flight>(ship, MakeFlight(/*roll*/ 0.0, /*pitch*/ 0.1, /*speed*/ 100.0));

  GameLogic::Tick(world);

  // One pitch tick: nose = normalize(0,-0.1,0.99) ~ (0,-0.100504,0.995037).
  // *100 truncates toward zero to (0,-10,99).
  EXPECT_TRUE((world.Get<GameLogic::WorldTransform>(ship).position == Math::Vector3i64{ 0, -10, 99 }));
  EXPECT_TRUE(world.Get<GameLogic::Flight>(ship).nose.y < 0.0);

  // Keep pitching: the path bends further toward -y (y keeps decreasing).
  const int64_t yAfter1 = world.Get<GameLogic::WorldTransform>(ship).position.y;
  for (int i = 0; i < 49; ++i)
    GameLogic::Tick(world);
  EXPECT_TRUE(world.Get<GameLogic::WorldTransform>(ship).position.y < yAfter1);
}

TEST(Flight, SubUnitSpeedAccumulatesViaCarry)
{
  ECS::Registry world;
  ECS::EntityId ship = world.Create();
  world.Add<GameLogic::WorldTransform>(ship, GameLogic::WorldTransform{ { 0, 0, 0 } });
  // 0.5 is exact in binary: the carry flushes one whole unit every two ticks.
  world.Add<GameLogic::Flight>(ship, MakeFlight(0.0, 0.0, /*speed*/ 0.5));

  for (int i = 0; i < 10; ++i)
    GameLogic::Tick(world);

  // 10 ticks * 0.5 = 5 whole units, none lost to truncation.
  EXPECT_TRUE((world.Get<GameLogic::WorldTransform>(ship).position == Math::Vector3i64{ 0, 0, 5 }));
}

TEST(Flight, BasisStaysOrthonormalUnderCombinedRotation)
{
  ECS::Registry world;
  ECS::EntityId ship = world.Create();
  world.Add<GameLogic::WorldTransform>(ship, GameLogic::WorldTransform{ { 0, 0, 0 } });
  world.Add<GameLogic::Flight>(ship, MakeFlight(/*roll*/ 0.07, /*pitch*/ 0.05, /*speed*/ 40.0));

  for (int i = 0; i < 200; ++i)
    GameLogic::Tick(world);

  const GameLogic::Flight& f = world.Get<GameLogic::Flight>(ship);
  // Orthonormalize() must keep the basis unit-length and mutually perpendicular
  // despite 200 ticks of incremental (drift-prone) rotation.
  EXPECT_TRUE(std::fabs(Math::Length(f.nose) - 1.0) < 1e-9);
  EXPECT_TRUE(std::fabs(Math::Length(f.roof) - 1.0) < 1e-9);
  EXPECT_TRUE(std::fabs(Math::Length(f.side) - 1.0) < 1e-9);
  EXPECT_TRUE(std::fabs(Math::Dot(f.nose, f.roof)) < 1e-9);
  EXPECT_TRUE(std::fabs(Math::Dot(f.nose, f.side)) < 1e-9);
  EXPECT_TRUE(std::fabs(Math::Dot(f.roof, f.side)) < 1e-9);
}

TEST(Flight, IsDeterministic)
{
  auto run = [](Math::Vector3i64& _outPos, GameLogic::Flight& _outFlight)
  {
    ECS::Registry world;
    ECS::EntityId ship = world.Create();
    world.Add<GameLogic::WorldTransform>(ship, GameLogic::WorldTransform{ { 100, -50, 25 } });
    world.Add<GameLogic::Flight>(ship, MakeFlight(0.03, 0.06, 17.0));
    for (int i = 0; i < 250; ++i)
      GameLogic::Tick(world);
    _outPos = world.Get<GameLogic::WorldTransform>(ship).position;
    _outFlight = world.Get<GameLogic::Flight>(ship);
  };

  Math::Vector3i64 posA, posB;
  GameLogic::Flight fA, fB;
  run(posA, fA);
  run(posB, fB);

  EXPECT_TRUE((posA == posB));
  EXPECT_TRUE(fA.nose.x == fB.nose.x);
  EXPECT_TRUE(fA.nose.y == fB.nose.y);
  EXPECT_TRUE(fA.nose.z == fB.nose.z);
}
