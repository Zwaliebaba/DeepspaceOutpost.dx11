#include "TestFramework.h"

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

TEST(Spawn_OnlyFiresAtTheInterval)
{
  ECS::Registry w;
  AddPlayer(w, 0);
  GameLogic::SpawnDirector d(/*seed*/ 123, /*interval*/ 10, /*cap*/ 5);

  CHECK(!w.IsValid(d.Step(w, 5)));    // not a multiple of the interval
  CHECK(w.IsValid(d.Step(w, 10)));    // spawns
}

TEST(Spawn_NeedsAPlayerToSpawnNear)
{
  ECS::Registry w;   // no players
  GameLogic::SpawnDirector d(123, 10, 5);
  CHECK(!w.IsValid(d.Step(w, 10)));
}

TEST(Spawn_RespectsTheNpcCap)
{
  ECS::Registry w;
  AddPlayer(w, 0);
  GameLogic::SpawnDirector d(123, /*interval*/ 1, /*cap*/ 3);

  int spawned = 0;
  for (uint32_t t = 1; t <= 20; ++t)
    if (w.IsValid(d.Step(w, t)))
      ++spawned;

  CHECK(spawned == 3);
  CHECK(d.CountNpcs(w) == 3);
}

TEST(Spawn_SpawnsAutoEngagingPirates)
{
  ECS::Registry w;
  AddPlayer(w, 0);
  GameLogic::SpawnDirector d(123, 10, 5);

  ECS::EntityId e = d.Step(w, 10);
  CHECK(w.IsValid(e));
  CHECK(w.Get<GameLogic::Combatant>(e).team == GameLogic::Team::Pirate);
  CHECK(w.Get<GameLogic::Combatant>(e).autoEngage);
}

TEST(Spawn_PoliceOnDemandHuntOnTheirOwnTeam)
{
  ECS::Registry w;
  GameLogic::SpawnDirector d(123, 10, 5);

  std::vector<ECS::EntityId> police = d.SpawnPolice(w, Math::Vector3i64{ 0, 0, 0 }, 2);
  CHECK(police.size() == 2);
  CHECK(w.Get<GameLogic::Combatant>(police[0]).team == GameLogic::Team::Police);
  CHECK(w.Get<GameLogic::Combatant>(police[0]).autoEngage);
}
