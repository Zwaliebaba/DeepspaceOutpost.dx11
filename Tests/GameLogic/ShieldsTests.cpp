#include <gtest/gtest.h>

#include "GameLogic.h"

using namespace Neuron;
using namespace Neuron::GameLogic;

namespace
{
  // A shielded player at the origin, nose facing +z, with full shields and a given
  // energy bank.
  ECS::EntityId SpawnShieldedPlayer(ECS::Registry& _w, int _energy = MAX_ENERGY)
  {
    const ECS::EntityId e = _w.Create();
    _w.Add<WorldTransform>(e, WorldTransform{ { 0, 0, 0 } });
    _w.Add<Flight>(e, Flight{});   // default nose = +z
    _w.Add<Combatant>(e, Combatant{ Team::Player, _energy, 10, 6000, false });
    _w.Add<Shields>(e, Shields{});
    _w.Add<PlayerTag>(e, PlayerTag{});
    return e;
  }

  const Math::Vector3i64 AHEAD{ 0, 0, 1000 };    // in front of the +z nose
  const Math::Vector3i64 BEHIND{ 0, 0, -1000 };  // behind
}

TEST(Shields, FrontHitDepletesFrontShieldNotEnergy)
{
  ECS::Registry w;
  const ECS::EntityId p = SpawnShieldedPlayer(w);

  EXPECT_FALSE(ApplyDamage(w, p, 40, AHEAD));
  EXPECT_EQ(w.Get<Shields>(p).front, MAX_SHIELD - 40);
  EXPECT_EQ(w.Get<Shields>(p).aft, MAX_SHIELD);
  EXPECT_EQ(w.Get<Combatant>(p).energy, MAX_ENERGY);   // bank untouched
}

TEST(Shields, RearHitDepletesAftShield)
{
  ECS::Registry w;
  const ECS::EntityId p = SpawnShieldedPlayer(w);

  EXPECT_FALSE(ApplyDamage(w, p, 30, BEHIND));
  EXPECT_EQ(w.Get<Shields>(p).aft, MAX_SHIELD - 30);
  EXPECT_EQ(w.Get<Shields>(p).front, MAX_SHIELD);
}

TEST(Shields, OverflowPastAShieldDrainsTheEnergyBank)
{
  ECS::Registry w;
  const ECS::EntityId p = SpawnShieldedPlayer(w);
  w.Get<Shields>(p).front = 10;

  EXPECT_FALSE(ApplyDamage(w, p, 40, AHEAD));   // 10 absorbed, 30 into the bank
  EXPECT_EQ(w.Get<Shields>(p).front, 0);
  EXPECT_EQ(w.Get<Combatant>(p).energy, MAX_ENERGY - 30);
}

TEST(Shields, EnergyDepletionKills)
{
  ECS::Registry w;
  const ECS::EntityId p = SpawnShieldedPlayer(w, /*energy*/ 20);
  w.Get<Shields>(p).front = 0;

  EXPECT_TRUE(ApplyDamage(w, p, 25, AHEAD));     // 25 into a 20 bank -> dead
  EXPECT_LE(w.Get<Combatant>(p).energy, 0);
}

TEST(Shields, UnshieldedNpcTakesFlatEnergyDamage)
{
  ECS::Registry w;
  const ECS::EntityId npc = w.Create();
  w.Add<WorldTransform>(npc, WorldTransform{ { 0, 0, 0 } });
  w.Add<Combatant>(npc, Combatant{ Team::Pirate, 50, 3, 5000, true });

  EXPECT_FALSE(ApplyDamage(w, npc, 20, AHEAD));
  EXPECT_EQ(w.Get<Combatant>(npc).energy, 30);   // straight off the pool
}

TEST(Shields, RegenRefillsShieldsFromASurplusEnergyBank)
{
  ECS::Registry w;
  const ECS::EntityId p = SpawnShieldedPlayer(w, /*energy*/ MAX_ENERGY);
  w.Get<Shields>(p).front = 100;
  w.Get<Shields>(p).aft = 100;

  StepShieldRegen(w);
  // Bank over half: each shield gains one (bank pays 2), then the bank recovers 1.
  EXPECT_EQ(w.Get<Shields>(p).front, 101);
  EXPECT_EQ(w.Get<Shields>(p).aft, 101);
  EXPECT_EQ(w.Get<Combatant>(p).energy, MAX_ENERGY - 1);
}

TEST(Shields, RegenOnlyRechargesEnergyWhenTheBankIsLow)
{
  ECS::Registry w;
  const ECS::EntityId p = SpawnShieldedPlayer(w, /*energy*/ 50);   // below half
  w.Get<Shields>(p).front = 100;

  StepShieldRegen(w);
  EXPECT_EQ(w.Get<Shields>(p).front, 100);       // shield untouched
  EXPECT_EQ(w.Get<Combatant>(p).energy, 51);     // only the bank recovers
}

TEST(Shields, RegenCapsAtMax)
{
  ECS::Registry w;
  const ECS::EntityId p = SpawnShieldedPlayer(w, /*energy*/ MAX_ENERGY);   // already full

  StepShieldRegen(w);
  EXPECT_EQ(w.Get<Combatant>(p).energy, MAX_ENERGY);
  EXPECT_EQ(w.Get<Shields>(p).front, MAX_SHIELD);
}

TEST(Shields, NpcsHaveNoShieldsToRegen)
{
  ECS::Registry w;
  const ECS::EntityId npc = w.Create();
  w.Add<Combatant>(npc, Combatant{ Team::Pirate, 40, 3, 5000, true });

  StepShieldRegen(w);   // iterates the Shields pool only -> NPC untouched
  EXPECT_EQ(w.Get<Combatant>(npc).energy, 40);
  EXPECT_FALSE(w.Has<Shields>(npc));
}
