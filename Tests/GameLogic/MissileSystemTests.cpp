#include <gtest/gtest.h>
#include "ECS.h"

#include "GameLogic.h"

// G8 gave StepMissiles a deterministic RNG stream and an out-list of auto-ECM
// pulses; these tests exercise the pre-ECM behaviour (no fitted targets), so a
// thin adapter keeps the call sites readable.
namespace
{
  std::vector<Neuron::GameLogic::Kill> StepMissilesPlain(Neuron::ECS::Registry& _w)
  {
    uint32_t rng = 1u;
    std::vector<uint32_t> pulses;
    return Neuron::GameLogic::StepMissiles(_w, rng, pulses);
  }
}

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
  std::ignore = StepMissilesPlain(w);
  EXPECT_TRUE(w.Get<GameLogic::WorldTransform>(missile).position.z > zStart);
  EXPECT_TRUE(w.IsValid(missile));

  // Run it to detonation. On the detonation tick StepMissiles reports BOTH the
  // pirate (it died) and the missile itself (it exploded); the caller (server kill
  // loop) destroys the victims, which we mimic here.
  bool killedPirate = false, killedMissile = false;
  for (int i = 0; i < GameLogic::MISSILE_LIFE && !killedPirate; ++i)
    for (const GameLogic::Kill& k : StepMissilesPlain(w))
    {
      if (k.victim == pirate)  { killedPirate = true; EXPECT_TRUE(k.killer == shooter.index); }
      if (k.victim == missile) { killedMissile = true; }
      w.Destroy(k.victim);   // the server's kill loop destroys reported victims
    }

  EXPECT_TRUE(killedPirate);
  EXPECT_TRUE(killedMissile);         // the missile reports its own explosion
  EXPECT_TRUE(!w.IsValid(missile));   // ...and is destroyed by the kill loop
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
      std::ignore = StepMissilesPlain(w);
  EXPECT_TRUE(!w.IsValid(missile));
}

TEST(MissileSys, DetonatesOnTheStationWithoutDestroyingIt)
{
  ECS::Registry w;
  ECS::EntityId shooter = SpawnShooter(w);
  ECS::EntityId station = SpawnTarget(w, 3000, GameLogic::Team::Station, /*energy*/ 1000000);

  ECS::EntityId missile = GameLogic::SpawnMissile(w, shooter, station.index);
  EXPECT_TRUE(w.Get<GameLogic::Missile>(missile).target == station);

  // Run to detonation. The station is never reported as a victim (it survives),
  // but the missile reports its own explosion; mimic the server destroying it.
  bool stationKilled = false, missileExploded = false;
  for (int i = 0; i < GameLogic::MISSILE_LIFE && w.IsValid(missile); ++i)
    for (const GameLogic::Kill& k : StepMissilesPlain(w))
    {
      if (k.victim == station) stationKilled = true;
      if (k.victim == missile) missileExploded = true;
      w.Destroy(k.victim);
    }

  EXPECT_TRUE(!stationKilled);           // the station shrugs off the hit
  EXPECT_TRUE(missileExploded);          // ...but the missile still detonated
  EXPECT_TRUE(!w.IsValid(missile));      // ...and is gone
  EXPECT_TRUE(w.Get<GameLogic::Combatant>(station).energy == 1000000 - GameLogic::MISSILE_HIT_DAMAGE);
}

// --- G2: the server-side lock gate (closes D6) ----------------------------------

TEST(MissileValidation, AcceptsALiveCombatantInRangeAndAhead)
{
  ECS::Registry w;
  const ECS::EntityId shooter = SpawnShooter(w);
  const ECS::EntityId pirate = SpawnTarget(w, 3000, GameLogic::Team::Pirate, 100);   // ahead, in range
  EXPECT_TRUE(GameLogic::MissileTargetValid(w, shooter, pirate.index));
}

TEST(MissileValidation, RejectsASpoofedOrDeadIndex)
{
  ECS::Registry w;
  const ECS::EntityId shooter = SpawnShooter(w);
  EXPECT_FALSE(GameLogic::MissileTargetValid(w, shooter, 0xFFFFFFFFu));   // sentinel
  EXPECT_FALSE(GameLogic::MissileTargetValid(w, shooter, 9999u));         // never existed
  const ECS::EntityId pirate = SpawnTarget(w, 3000, GameLogic::Team::Pirate, 100);
  w.Destroy(pirate);
  EXPECT_FALSE(GameLogic::MissileTargetValid(w, shooter, pirate.index));  // dead
}

TEST(MissileValidation, RejectsOutOfLockRange)
{
  ECS::Registry w;
  const ECS::EntityId shooter = SpawnShooter(w);
  const ECS::EntityId far = SpawnTarget(w, GameLogic::MISSILE_LOCK_RANGE + 1000, GameLogic::Team::Pirate, 100);
  EXPECT_FALSE(GameLogic::MissileTargetValid(w, shooter, far.index));
}

TEST(MissileValidation, RejectsATargetBehindTheShooter)
{
  ECS::Registry w;
  const ECS::EntityId shooter = SpawnShooter(w);   // faces +z
  const ECS::EntityId behind = SpawnTarget(w, -3000, GameLogic::Team::Pirate, 100);   // at -z
  EXPECT_FALSE(GameLogic::MissileTargetValid(w, shooter, behind.index));
}

TEST(MissileValidation, RejectsTargetingYourself)
{
  ECS::Registry w;
  const ECS::EntityId shooter = SpawnShooter(w);
  EXPECT_FALSE(GameLogic::MissileTargetValid(w, shooter, shooter.index));
}

TEST(MissileValidation, SpendMissileDecrementsTheRackAndEmptiesOut)
{
  ECS::Registry w;
  const ECS::EntityId shooter = SpawnShooter(w);
  w.Add<GameLogic::Equipment>(shooter, GameLogic::Equipment{});   // default rack = 3
  EXPECT_TRUE(GameLogic::SpendMissile(w, shooter));
  EXPECT_EQ(w.Get<GameLogic::Equipment>(shooter).missiles, 2);
  EXPECT_TRUE(GameLogic::SpendMissile(w, shooter));
  EXPECT_TRUE(GameLogic::SpendMissile(w, shooter));
  EXPECT_EQ(w.Get<GameLogic::Equipment>(shooter).missiles, 0);
  EXPECT_FALSE(GameLogic::SpendMissile(w, shooter));   // empty: refused
}

TEST(MissileValidation, SpendMissileRefusesWithNoRack)
{
  ECS::Registry w;
  const ECS::EntityId shooter = SpawnShooter(w);   // no Equipment component
  EXPECT_FALSE(GameLogic::SpendMissile(w, shooter));
}
