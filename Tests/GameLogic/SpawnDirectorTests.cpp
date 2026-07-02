#include <gtest/gtest.h>

#include <vector>

#include "GameLogic.h"

using namespace Neuron;

namespace
{
  ECS::EntityId AddPlayer(ECS::Registry& _w, int64_t _x)
  {
    ECS::EntityId e = _w.Create();
    _w.Add<GameLogic::WorldTransform>(e, GameLogic::WorldTransform{ { _x, 0, 0 } });
    _w.Add<GameLogic::PlayerTag>(e, GameLogic::PlayerTag{});
    return e;
  }
}

TEST(Spawn, OnlyFiresAtTheInterval)
{
  ECS::Registry w;
  AddPlayer(w, 0);
  GameLogic::SpawnDirector d(/*seed*/ 123, /*interval*/ 10, /*cap*/ 5);

  EXPECT_TRUE(!w.IsValid(d.Step(w, 5)));    // not a multiple of the interval
  EXPECT_TRUE(w.IsValid(d.Step(w, 10)));    // spawns
}

TEST(Spawn, NeedsAPlayerToSpawnNear)
{
  ECS::Registry w;   // no players
  GameLogic::SpawnDirector d(123, 10, 5);
  EXPECT_TRUE(!w.IsValid(d.Step(w, 10)));
}

TEST(Spawn, RespectsTheNpcCap)
{
  ECS::Registry w;
  AddPlayer(w, 0);
  GameLogic::SpawnDirector d(123, /*interval*/ 1, /*cap*/ 3);

  int spawned = 0;
  for (uint32_t t = 1; t <= 20; ++t)
    if (w.IsValid(d.Step(w, t)))
      ++spawned;

  EXPECT_TRUE(spawned == 3);
  EXPECT_TRUE(d.CountNpcs(w) == 3);
}

TEST(Spawn, SpawnsAutoEngagingPirates)
{
  ECS::Registry w;
  AddPlayer(w, 0);
  GameLogic::SpawnDirector d(123, 10, 5);

  ECS::EntityId e = d.Step(w, 10);
  EXPECT_TRUE(w.IsValid(e));
  EXPECT_TRUE(w.Get<GameLogic::Combatant>(e).team == GameLogic::Team::Pirate);
  EXPECT_TRUE(w.Get<GameLogic::Combatant>(e).autoEngage);
}

TEST(Spawn, PiratesRenderAsAShipAndCarryABounty)
{
  ECS::Registry w;
  AddPlayer(w, 0);
  GameLogic::SpawnDirector d(123, 10, 5);

  ECS::EntityId e = d.Step(w, 10);
  ASSERT_TRUE(w.IsValid(e));
  ASSERT_TRUE(w.Has<GameLogic::NetType>(e));                                  // not the default type-0 model
  EXPECT_TRUE(w.Get<GameLogic::NetType>(e).type == GameLogic::ShipType::Viper);
  ASSERT_TRUE(w.Has<GameLogic::Bounty>(e));
  EXPECT_TRUE(w.Get<GameLogic::Bounty>(e).value == GameLogic::PIRATE_BOUNTY);
}

TEST(Spawn, PoliceOnDemandHuntOnTheirOwnTeam)
{
  ECS::Registry w;
  GameLogic::SpawnDirector d(123, 10, 5);

  std::vector<ECS::EntityId> police = d.SpawnPolice(w, Math::Vector3i64{ 0, 0, 0 }, 2);
  EXPECT_TRUE(police.size() == 2);
  EXPECT_TRUE(w.Get<GameLogic::Combatant>(police[0]).team == GameLogic::Team::Police);
  EXPECT_TRUE(w.Get<GameLogic::Combatant>(police[0]).autoEngage);
}

TEST(Spawn, PoliceRenderAsAShipButCarryNoBounty)
{
  ECS::Registry w;
  GameLogic::SpawnDirector d(123, 10, 5);

  std::vector<ECS::EntityId> police = d.SpawnPolice(w, Math::Vector3i64{ 0, 0, 0 }, 1);
  ASSERT_TRUE(w.Has<GameLogic::NetType>(police[0]));
  EXPECT_TRUE(w.Get<GameLogic::NetType>(police[0]).type == GameLogic::ShipType::Viper);
  EXPECT_FALSE(w.Has<GameLogic::Bounty>(police[0]));   // killing the police is a crime, not a payday
}
