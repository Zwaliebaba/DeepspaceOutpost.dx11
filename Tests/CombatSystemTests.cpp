#include "TestFramework.h"

#include "GameLogic.h"

using namespace Neuron;

namespace
{
  ECS::EntityId Spawn(ECS::Registry& _w, int64_t _x, int _team, int _energy, int _laser, int64_t _range = 5000)
  {
    ECS::EntityId e = _w.Create();
    _w.Add<GameLogic::WorldTransform>(e, GameLogic::WorldTransform{ { _x, 0, 0 } });
    _w.Add<GameLogic::Combatant>(e, GameLogic::Combatant{ _team, _energy, _laser, _range });
    return e;
  }
}

TEST(CombatSys_EnemiesInRangeDamageEachOther)
{
  ECS::Registry world;
  ECS::EntityId a = Spawn(world, 0, /*team*/ 0, /*energy*/ 100, /*laser*/ 10);
  ECS::EntityId b = Spawn(world, 1000, /*team*/ 1, /*energy*/ 5, /*laser*/ 10);

  std::vector<GameLogic::Kill> kills = GameLogic::StepCombat(world);

  // A survives at 90; B drops to -5 and dies, credited to A.
  CHECK(world.Get<GameLogic::Combatant>(a).energy == 90);
  CHECK(kills.size() == 1);
  CHECK(kills[0].victim == b);
  CHECK(kills[0].killer == a.index);
}

TEST(CombatSys_AlliesDoNotFire)
{
  ECS::Registry world;
  ECS::EntityId a = Spawn(world, 0, 0, 100, 10);
  ECS::EntityId b = Spawn(world, 1000, 0, 100, 10);   // same team

  std::vector<GameLogic::Kill> kills = GameLogic::StepCombat(world);

  CHECK(kills.empty());
  CHECK(world.Get<GameLogic::Combatant>(a).energy == 100);
  CHECK(world.Get<GameLogic::Combatant>(b).energy == 100);
}

TEST(CombatSys_OutOfRangeDoesNothing)
{
  ECS::Registry world;
  ECS::EntityId a = Spawn(world, 0, 0, 100, 10, /*range*/ 5000);
  ECS::EntityId b = Spawn(world, 10000, 1, 100, 10, /*range*/ 5000);   // 10k apart

  std::vector<GameLogic::Kill> kills = GameLogic::StepCombat(world);

  CHECK(kills.empty());
  CHECK(world.Get<GameLogic::Combatant>(a).energy == 100);
  CHECK(world.Get<GameLogic::Combatant>(b).energy == 100);
}

TEST(CombatSys_FiresAtTheNearestEnemy)
{
  ECS::Registry world;
  ECS::EntityId a = Spawn(world, 0, 0, 100, 10, /*range*/ 9000);
  ECS::EntityId near = Spawn(world, 1000, 1, 100, 0, /*range*/ 1);   // unarmed enemies
  ECS::EntityId far = Spawn(world, 5000, 1, 100, 0, /*range*/ 1);

  GameLogic::StepCombat(world);

  // A engages only the nearer enemy.
  CHECK(world.Get<GameLogic::Combatant>(near).energy == 90);
  CHECK(world.Get<GameLogic::Combatant>(far).energy == 100);
}

TEST(CombatSys_SimultaneousMutualKill)
{
  ECS::Registry world;
  ECS::EntityId a = Spawn(world, 0, 0, /*energy*/ 5, /*laser*/ 10);
  ECS::EntityId b = Spawn(world, 1000, 1, /*energy*/ 5, /*laser*/ 10);

  std::vector<GameLogic::Kill> kills = GameLogic::StepCombat(world);

  // Both die the same tick (damage is resolved simultaneously).
  CHECK(kills.size() == 2);
  bool aDead = false, bDead = false;
  for (const GameLogic::Kill& k : kills)
  {
    if (k.victim == a) { aDead = true; CHECK(k.killer == b.index); }
    if (k.victim == b) { bDead = true; CHECK(k.killer == a.index); }
  }
  CHECK(aDead);
  CHECK(bDead);
}

TEST(CombatSys_NoCombatantsIsEmpty)
{
  ECS::Registry world;
  CHECK(GameLogic::StepCombat(world).empty());
}
