#include "TestFramework.h"

#include "GameLogic.h"

using namespace Neuron;

TEST(GameLogic_MotionIntegratesDeterministically)
{
  ECS::Registry world;

  ECS::EntityId mover = world.Create();
  world.Add<GameLogic::WorldTransform>(mover, GameLogic::WorldTransform{ { 1000, 0, 0 } });
  world.Add<GameLogic::Velocity>(mover, GameLogic::Velocity{ { 10, -5, 2 } });

  for (int i = 0; i < 100; ++i)
    GameLogic::Tick(world);

  // 100 ticks * (10,-5,2) added to (1000,0,0).
  CHECK((world.Get<GameLogic::WorldTransform>(mover).position
         == Math::Vector3i64{ 2000, -500, 200 }));
}

TEST(GameLogic_EntitiesWithoutVelocityStayPut)
{
  ECS::Registry world;

  ECS::EntityId still = world.Create();
  world.Add<GameLogic::WorldTransform>(still, GameLogic::WorldTransform{ { 7, 8, 9 } });
  // no Velocity component

  for (int i = 0; i < 50; ++i)
    GameLogic::Tick(world);

  CHECK((world.Get<GameLogic::WorldTransform>(still).position
         == Math::Vector3i64{ 7, 8, 9 }));
}

TEST(GameLogic_ManyEntitiesAdvanceIndependently)
{
  ECS::Registry world;

  ECS::EntityId a = world.Create();
  ECS::EntityId b = world.Create();
  world.Add<GameLogic::WorldTransform>(a, GameLogic::WorldTransform{ { 0, 0, 0 } });
  world.Add<GameLogic::Velocity>(a, GameLogic::Velocity{ { 1, 0, 0 } });
  world.Add<GameLogic::WorldTransform>(b, GameLogic::WorldTransform{ { 0, 0, 0 } });
  world.Add<GameLogic::Velocity>(b, GameLogic::Velocity{ { 0, 2, 0 } });

  for (int i = 0; i < 10; ++i)
    GameLogic::Tick(world);

  CHECK((world.Get<GameLogic::WorldTransform>(a).position == Math::Vector3i64{ 10, 0, 0 }));
  CHECK((world.Get<GameLogic::WorldTransform>(b).position == Math::Vector3i64{ 0, 20, 0 }));
}
