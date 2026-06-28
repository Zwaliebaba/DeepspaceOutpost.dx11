#include <gtest/gtest.h>

#include <vector>

// Include the combat-message header directly (not the GameLogic umbrella) so this
// suite stays headless and winsock-free: it exercises the bus-driven combat chain
// (FireWeapon -> Crime / EntityKilled facts) without pulling in the session/net code.
#include "CombatMessages.h"

using namespace Neuron;
using namespace Neuron::GameLogic;

namespace
{
  constexpr int64_t FIRE_RANGE = 6000;   // mirror the server's constants
  constexpr double  AIM_CONE   = 0.9;

  // A player: Flight (nose +z), fires on command (autoEngage false), has the
  // PlayerTag + a clean Wanted record.
  ECS::EntityId SpawnPlayer(ECS::Registry& _w, int64_t _x = 0)
  {
    const ECS::EntityId e = _w.Create();
    _w.Add<WorldTransform>(e, WorldTransform{ { _x, 0, 0 } });
    _w.Add<Flight>(e, Flight{});
    _w.Add<Combatant>(e, Combatant{ Team::Player, 255, 50, 6000, false });
    _w.Add<PlayerTag>(e, PlayerTag{});
    _w.Add<Wanted>(e, Wanted{});
    return e;
  }

  ECS::EntityId SpawnTarget(ECS::Registry& _w, int64_t _z, int _team, int _energy)
  {
    const ECS::EntityId e = _w.Create();
    _w.Add<WorldTransform>(e, WorldTransform{ { 0, 0, _z } });
    _w.Add<Combatant>(e, Combatant{ _team, _energy, 0, 1, false });
    return e;
  }

  // Wire the FireWeapon -> ResolveFireWeapon subscriber the server uses.
  void InstallResolver(ECS::Registry& _w, Msg::MessageBus& _bus)
  {
    _bus.Subscribe<FireWeapon>([&](const FireWeapon& _fw)
    {
      ResolveFireWeapon(_w, _bus, _fw, FIRE_RANGE, AIM_CONE);
    });
  }
}

TEST(CombatMessages, LaserKillPublishesEntityKilledWithKiller)
{
  ECS::Registry w;
  Msg::MessageBus bus;
  InstallResolver(w, bus);

  std::vector<EntityKilled> killed;
  bus.Subscribe<EntityKilled>([&](const EntityKilled& _k) { killed.push_back(_k); });

  const ECS::EntityId player = SpawnPlayer(w);
  const ECS::EntityId enemy = SpawnTarget(w, 3000, Team::Pirate, /*energy*/ 5);

  bus.Publish(FireWeapon{ player, Weapon::Laser });
  bus.Dispatch();

  ASSERT_EQ(killed.size(), 1u);
  EXPECT_TRUE(killed[0].victim == enemy);
  EXPECT_EQ(killed[0].killer, player.index);
}

TEST(CombatMessages, LaserMissPublishesNothing)
{
  ECS::Registry w;
  Msg::MessageBus bus;
  InstallResolver(w, bus);

  int facts = 0;
  bus.Subscribe<EntityKilled>([&](const EntityKilled&) { ++facts; });
  bus.Subscribe<Crime>([&](const Crime&) { ++facts; });

  const ECS::EntityId player = SpawnPlayer(w);
  SpawnTarget(w, -3000, Team::Pirate, 5);   // behind the nose -> outside the aim cone

  bus.Publish(FireWeapon{ player, Weapon::Laser });
  bus.Dispatch();

  EXPECT_EQ(facts, 0);
}

TEST(CombatMessages, FiringOnStationIsAFirstOffenceCrimeAndAdvancesRecord)
{
  ECS::Registry w;
  Msg::MessageBus bus;
  InstallResolver(w, bus);

  std::vector<Crime> crimes;
  std::vector<EntityKilled> killed;
  bus.Subscribe<Crime>([&](const Crime& _c) { crimes.push_back(_c); });
  bus.Subscribe<EntityKilled>([&](const EntityKilled& _k) { killed.push_back(_k); });

  const ECS::EntityId player = SpawnPlayer(w);
  SpawnTarget(w, 3000, Team::Station, /*energy*/ 1000000);   // survives the shot

  bus.Publish(FireWeapon{ player, Weapon::Laser });
  bus.Dispatch();

  EXPECT_TRUE(killed.empty());
  ASSERT_EQ(crimes.size(), 1u);
  EXPECT_TRUE(crimes[0].firstOffence);
  EXPECT_EQ(crimes[0].victimTeam, Team::Station);
  EXPECT_EQ(w.Get<Wanted>(player).level, 1);

  // A second offence is no longer "first", and the record keeps climbing.
  bus.Publish(FireWeapon{ player, Weapon::Laser });
  bus.Dispatch();
  ASSERT_EQ(crimes.size(), 2u);
  EXPECT_FALSE(crimes[1].firstOffence);
  EXPECT_EQ(w.Get<Wanted>(player).level, 2);
}

TEST(CombatMessages, MissileLaunchSpawnsProjectileAndFlagsCrimeAtLaunch)
{
  ECS::Registry w;
  Msg::MessageBus bus;
  InstallResolver(w, bus);

  std::vector<Crime> crimes;
  bus.Subscribe<Crime>([&](const Crime& _c) { crimes.push_back(_c); });

  const ECS::EntityId player = SpawnPlayer(w);
  const ECS::EntityId police = SpawnTarget(w, 2000, Team::Police, /*energy*/ 120);
  const std::size_t before = w.AliveCount();

  bus.Publish(FireWeapon{ player, Weapon::Missile, police.index });
  bus.Dispatch();

  EXPECT_EQ(w.AliveCount(), before + 1);          // a missile entity was spawned
  ASSERT_EQ(crimes.size(), 1u);
  EXPECT_EQ(crimes[0].victimTeam, Team::Police);
  EXPECT_TRUE(crimes[0].firstOffence);
}

TEST(CombatMessages, RepublishedFactsResolveInOneDispatch)
{
  // The resolver (FireWeapon handler) publishes EntityKilled; that fact must be
  // delivered in the SAME Dispatch (next generation), not dropped or deferred.
  ECS::Registry w;
  Msg::MessageBus bus;
  InstallResolver(w, bus);

  bool sawKill = false;
  bus.Subscribe<EntityKilled>([&](const EntityKilled&) { sawKill = true; });

  const ECS::EntityId player = SpawnPlayer(w);
  SpawnTarget(w, 3000, Team::Pirate, 5);

  bus.Publish(FireWeapon{ player, Weapon::Laser });
  bus.Dispatch();

  EXPECT_TRUE(sawKill);
  EXPECT_GE(bus.Stats().generations, 2u);
}
