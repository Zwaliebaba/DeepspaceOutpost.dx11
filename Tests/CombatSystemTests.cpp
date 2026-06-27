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

  // A shooter has a Flight (nose +z) and fires only on command (autoEngage false).
  ECS::EntityId SpawnShooter(ECS::Registry& _w, int _team, int _laser)
  {
    ECS::EntityId e = _w.Create();
    _w.Add<GameLogic::WorldTransform>(e, GameLogic::WorldTransform{ { 0, 0, 0 } });
    _w.Add<GameLogic::Flight>(e, GameLogic::Flight{});
    _w.Add<GameLogic::Combatant>(e, GameLogic::Combatant{ _team, 255, _laser, 6000, false });
    return e;
  }

  ECS::EntityId SpawnTargetAt(ECS::Registry& _w, int64_t _x, int64_t _y, int64_t _z, int _team, int _energy)
  {
    ECS::EntityId e = _w.Create();
    _w.Add<GameLogic::WorldTransform>(e, GameLogic::WorldTransform{ { _x, _y, _z } });
    _w.Add<GameLogic::Combatant>(e, GameLogic::Combatant{ _team, _energy, 0, 1, false });
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

TEST(CombatSys_PlayersDoNotAutoEngage)
{
  ECS::Registry world;
  // Player (autoEngage false) and an enemy pirate (autoEngage true) in range.
  ECS::EntityId player = world.Create();
  world.Add<GameLogic::WorldTransform>(player, GameLogic::WorldTransform{ { 0, 0, 0 } });
  world.Add<GameLogic::Combatant>(player, GameLogic::Combatant{ GameLogic::Team::Player, 255, 10, 5000, false });
  ECS::EntityId pirate = Spawn(world, 1000, GameLogic::Team::Pirate, 50, 10);

  GameLogic::StepCombat(world);

  // The pirate fired on the player; the player did NOT fire back automatically.
  CHECK(world.Get<GameLogic::Combatant>(player).energy == 245);
  CHECK(world.Get<GameLogic::Combatant>(pirate).energy == 50);
}

TEST(Fire_HitsTheEnemyAhead)
{
  ECS::Registry w;
  ECS::EntityId shooter = SpawnShooter(w, GameLogic::Team::Player, /*laser*/ 20);
  ECS::EntityId target = SpawnTargetAt(w, 0, 0, 1000, GameLogic::Team::Pirate, 100);  // straight ahead (+z)

  GameLogic::FireOutcome o = GameLogic::ResolvePlayerFire(w, shooter, 6000, 0.9);
  CHECK(o.hit);
  CHECK(o.target == target);
  CHECK(o.targetTeam == GameLogic::Team::Pirate);
  CHECK(!o.destroyed);
  CHECK(w.Get<GameLogic::Combatant>(target).energy == 80);   // 100 - 20
}

TEST(Fire_MissesTargetsOutsideTheCone)
{
  ECS::Registry w;
  ECS::EntityId shooter = SpawnShooter(w, GameLogic::Team::Player, 20);
  SpawnTargetAt(w, 0, 0, -1000, GameLogic::Team::Pirate, 100);   // directly behind

  GameLogic::FireOutcome o = GameLogic::ResolvePlayerFire(w, shooter, 6000, 0.9);
  CHECK(!o.hit);
}

TEST(Fire_IgnoresAllies)
{
  ECS::Registry w;
  ECS::EntityId shooter = SpawnShooter(w, GameLogic::Team::Player, 20);
  SpawnTargetAt(w, 0, 0, 1000, GameLogic::Team::Player, 100);    // same team ahead

  GameLogic::FireOutcome o = GameLogic::ResolvePlayerFire(w, shooter, 6000, 0.9);
  CHECK(!o.hit);
}

TEST(Fire_ReportsDestroyedTargets)
{
  ECS::Registry w;
  ECS::EntityId shooter = SpawnShooter(w, GameLogic::Team::Player, 20);
  ECS::EntityId target = SpawnTargetAt(w, 0, 0, 500, GameLogic::Team::Pirate, /*energy*/ 10);

  GameLogic::FireOutcome o = GameLogic::ResolvePlayerFire(w, shooter, 6000, 0.9);
  CHECK(o.hit);
  CHECK(o.destroyed);   // 10 - 20 <= 0
  CHECK(o.target == target);
}
