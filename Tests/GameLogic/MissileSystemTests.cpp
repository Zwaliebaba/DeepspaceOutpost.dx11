#include <gtest/gtest.h>

#include "GameLogic.h"

using namespace Neuron;

namespace
{
  // A shooter at the origin facing +z (identity flight), able to lock/launch.
  ECS::EntityId SpawnShooter(ECS::Registry& _w)
  {
    ECS::EntityId e = _w.Create();
    _w.Add<GameLogic::WorldTransform>(e, GameLogic::WorldTransform{ { 0, 0, 0 } });
    _w.Add<GameLogic::Flight>(e, GameLogic::Flight{});
    _w.Add<GameLogic::Combatant>(e, GameLogic::Combatant{ GameLogic::Team::Player, 255, 10, 6000, false });
    return e;
  }

  // An inert target dead ahead (no Flight, never fires) so the test world is static.
  ECS::EntityId SpawnTarget(ECS::Registry& _w, int64_t _z, int _team, int _energy)
  {
    ECS::EntityId e = _w.Create();
    _w.Add<GameLogic::WorldTransform>(e, GameLogic::WorldTransform{ { 0, 0, _z } });
    _w.Add<GameLogic::Combatant>(e, GameLogic::Combatant{ _team, _energy, 0, 1, false });
    return e;
  }
}

TEST(MissileSys, LaunchesAsAMissileEntityLockedOnThePlayersTarget)
{
  ECS::Registry w;
  ECS::EntityId shooter = SpawnShooter(w);
  ECS::EntityId pirate = SpawnTarget(w, 3000, GameLogic::Team::Pirate, 100);

  // The player locked the pirate (passed by index, as it comes off the wire).
  ECS::EntityId missile = GameLogic::SpawnMissile(w, shooter, pirate.index);

  EXPECT_TRUE(w.IsValid(missile));
  EXPECT_TRUE(w.Get<GameLogic::NetType>(missile).type == GameLogic::ShipType::Missile);
  EXPECT_TRUE(w.Get<GameLogic::Missile>(missile).target == pirate);
  // It is a real flying entity, distinct from the shooter and target.
  EXPECT_TRUE(missile != shooter);
  EXPECT_TRUE(missile != pirate);
}

TEST(MissileSys, HomesOverSeveralTicksAndDestroysTheTarget)
{
  ECS::Registry w;
  ECS::EntityId shooter = SpawnShooter(w);
  ECS::EntityId pirate = SpawnTarget(w, 3000, GameLogic::Team::Pirate, /*energy*/ 100);

  ECS::EntityId missile = GameLogic::SpawnMissile(w, shooter, pirate.index);
  const int64_t zStart = w.Get<GameLogic::WorldTransform>(missile).position.z;

  // One tick: the missile has flown forward but not yet reached the target.
  GameLogic::StepMissiles(w);
  EXPECT_TRUE(w.Get<GameLogic::WorldTransform>(missile).position.z > zStart);
  EXPECT_TRUE(w.IsValid(missile));

  // Run it to detonation; the pirate dies, credited to the shooter.
  bool killed = false;
  for (int i = 0; i < GameLogic::MISSILE_LIFE && !killed; ++i)
    for (const GameLogic::Kill& k : GameLogic::StepMissiles(w))
      if (k.victim == pirate)
      {
        killed = true;
        EXPECT_TRUE(k.killer == shooter.index);
      }

  EXPECT_TRUE(killed);
  EXPECT_TRUE(!w.IsValid(missile));   // the projectile is spent on detonation
}

TEST(MissileSys, DumbFiresAndSelfDestructsWithNoTarget)
{
  ECS::Registry w;
  ECS::EntityId shooter = SpawnShooter(w);

  // Fired with no lock (sentinel index): the missile carries no target.
  ECS::EntityId missile = GameLogic::SpawnMissile(w, shooter, 0xFFFFFFFFu);
  EXPECT_TRUE(w.IsValid(missile));
  EXPECT_TRUE(!w.IsValid(w.Get<GameLogic::Missile>(missile).target));   // no lock

  // It flies straight and self-destructs once its life runs out.
  for (int i = 0; i < GameLogic::MISSILE_LIFE; ++i)
    GameLogic::StepMissiles(w);
  EXPECT_TRUE(!w.IsValid(missile));
}

TEST(MissileSys, DetonatesOnTheStationWithoutDestroyingIt)
{
  ECS::Registry w;
  ECS::EntityId shooter = SpawnShooter(w);
  ECS::EntityId station = SpawnTarget(w, 3000, GameLogic::Team::Station, /*energy*/ 1000000);

  ECS::EntityId missile = GameLogic::SpawnMissile(w, shooter, station.index);
  EXPECT_TRUE(w.Get<GameLogic::Missile>(missile).target == station);

  bool anyKill = false;
  for (int i = 0; i < GameLogic::MISSILE_LIFE && w.IsValid(missile); ++i)
    if (!GameLogic::StepMissiles(w).empty())
      anyKill = true;

  EXPECT_TRUE(!anyKill);                 // the station shrugs off the hit (no kill)
  EXPECT_TRUE(!w.IsValid(missile));      // ...but the missile still detonated and is gone
  EXPECT_TRUE(w.Get<GameLogic::Combatant>(station).energy == 1000000 - GameLogic::MISSILE_HIT_DAMAGE);
}
