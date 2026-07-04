// D2 regression coverage: the whole point of FrameScratch is that its vectors/
// maps/grids are reused across calls instead of freed - so every system that
// takes one must fully overwrite its own fields each call and never let a
// PREVIOUS call's data leak into the next. These tests share ONE FrameScratch
// across two DIFFERENT worlds (deliberately built to catch stale-index/stale-
// grid-entry bugs) and assert each call sees only its own world.

#include <gtest/gtest.h>

#include "GameLogic.h"

using namespace Neuron;

TEST(FrameScratch, StepCollisionsDoesNotLeakBetweenCalls)
{
  GameLogic::FrameScratch scratch;

  // World A: two ships ramming in contact - one call should report exactly one
  // mutual-ram pair (both take damage; neither's energy reaches 0 with the
  // default stock energy, so no kill, but the candidate count is non-zero).
  ECS::Registry a;
  ECS::EntityId a1 = a.Create();
  a.Add<GameLogic::WorldTransform>(a1, GameLogic::WorldTransform{ { 0, 0, 0 } });
  a.Add<GameLogic::Combatant>(a1, GameLogic::Combatant{ GameLogic::Team::Pirate, 200, 10, 5000, false });
  ECS::EntityId a2 = a.Create();
  a.Add<GameLogic::WorldTransform>(a2, GameLogic::WorldTransform{ { 100, 0, 0 } });   // in ram range
  a.Add<GameLogic::Combatant>(a2, GameLogic::Combatant{ GameLogic::Team::Police, 200, 10, 5000, false });

  uint64_t pairsA = 0;
  const std::vector<GameLogic::Kill> killsA = GameLogic::StepCollisions(a, &pairsA, scratch);
  EXPECT_TRUE(killsA.empty());
  EXPECT_GT(pairsA, 0u);
  EXPECT_EQ(a.Get<GameLogic::Combatant>(a1).energy, 100);   // 200 - SHIP_RAM_DAMAGE(100)
  EXPECT_EQ(a.Get<GameLogic::Combatant>(a2).energy, 100);

  // World B: a single, isolated ship far from anything - reusing the SAME
  // scratch must not resurrect world A's units/grid/dead-set.
  ECS::Registry b;
  ECS::EntityId lone = b.Create();
  b.Add<GameLogic::WorldTransform>(lone, GameLogic::WorldTransform{ { 900000, 900000, 900000 } });
  b.Add<GameLogic::Combatant>(lone, GameLogic::Combatant{ GameLogic::Team::Pirate, 200, 10, 5000, false });

  uint64_t pairsB = 0;
  const std::vector<GameLogic::Kill> killsB = GameLogic::StepCollisions(b, &pairsB, scratch);
  EXPECT_TRUE(killsB.empty());
  EXPECT_EQ(pairsB, 0u);                                    // no partner -> zero candidates
  EXPECT_EQ(b.Get<GameLogic::Combatant>(lone).energy, 200); // untouched
  EXPECT_TRUE(b.IsValid(lone));                             // still just the one entity in world B
}

TEST(FrameScratch, StepCombatDoesNotLeakBetweenCalls)
{
  GameLogic::FrameScratch scratch;

  // World A: an auto-engaging pirate in range of a police target - fires once.
  ECS::Registry a;
  ECS::EntityId shooter = a.Create();
  a.Add<GameLogic::WorldTransform>(shooter, GameLogic::WorldTransform{ { 0, 0, 0 } });
  a.Add<GameLogic::Combatant>(shooter, GameLogic::Combatant{ GameLogic::Team::Pirate, 200, 50, 6000, true });
  ECS::EntityId target = a.Create();
  a.Add<GameLogic::WorldTransform>(target, GameLogic::WorldTransform{ { 500, 0, 0 } });
  a.Add<GameLogic::Combatant>(target, GameLogic::Combatant{ GameLogic::Team::Police, 200, 10, 6000, false });

  const std::vector<GameLogic::Kill> killsA = GameLogic::StepCombat(a, nullptr, scratch);
  EXPECT_TRUE(killsA.empty());
  EXPECT_EQ(a.Get<GameLogic::Combatant>(target).energy, 150);   // 200 - laserStrength(50)

  // World B: a lone non-combatant - reusing scratch must not damage anything
  // (no stale attacker/damage map entries from world A carrying over).
  ECS::Registry b;
  ECS::EntityId civ = b.Create();
  b.Add<GameLogic::WorldTransform>(civ, GameLogic::WorldTransform{ { 0, 0, 0 } });
  b.Add<GameLogic::Combatant>(civ, GameLogic::Combatant{ GameLogic::Team::Trader, 200, 10, 6000, false });

  const std::vector<GameLogic::Kill> killsB = GameLogic::StepCombat(b, nullptr, scratch);
  EXPECT_TRUE(killsB.empty());
  EXPECT_EQ(b.Get<GameLogic::Combatant>(civ).energy, 200);   // untouched
}

TEST(FrameScratch, ScoopSystemDoesNotLeakBetweenCalls)
{
  GameLogic::FrameScratch scratch;

  // World A: a scoop-equipped player right on top of a canister - scoops it.
  ECS::Registry a;
  ECS::EntityId player = a.Create();
  a.Add<GameLogic::WorldTransform>(player, GameLogic::WorldTransform{ { 0, 0, 0 } });
  a.Add<GameLogic::PlayerTag>(player, GameLogic::PlayerTag{});
  a.Add<GameLogic::CargoHold>(player, GameLogic::CargoHold{});
  a.Add<GameLogic::Equipment>(player, GameLogic::Equipment{ 3, false, false, /*fuelScoop*/ true, false, false });
  ECS::EntityId can = a.Create();
  a.Add<GameLogic::WorldTransform>(can, GameLogic::WorldTransform{ { 10, 0, 0 } });
  a.Add<GameLogic::LootItem>(can, GameLogic::LootItem{ 0, 4, 100 });

  const std::vector<uint32_t> changedA = GameLogic::ScoopSystem(a, scratch);
  ASSERT_EQ(changedA.size(), 1u);
  EXPECT_EQ(changedA[0], player.index);
  EXPECT_EQ(a.Get<GameLogic::CargoHold>(player).units[0], 4);
  EXPECT_FALSE(a.IsValid(can));   // consumed

  // World B: no canisters at all - reusing scratch must not resurrect world A's
  // (now-destroyed) canister or report a phantom scoop.
  ECS::Registry b;
  ECS::EntityId lonePlayer = b.Create();
  b.Add<GameLogic::WorldTransform>(lonePlayer, GameLogic::WorldTransform{ { 0, 0, 0 } });
  b.Add<GameLogic::PlayerTag>(lonePlayer, GameLogic::PlayerTag{});
  b.Add<GameLogic::CargoHold>(lonePlayer, GameLogic::CargoHold{});
  b.Add<GameLogic::Equipment>(lonePlayer, GameLogic::Equipment{ 3, false, false, true, false, false });

  const std::vector<uint32_t> changedB = GameLogic::ScoopSystem(b, scratch);
  EXPECT_TRUE(changedB.empty());
  EXPECT_EQ(b.Get<GameLogic::CargoHold>(lonePlayer).units[0], 0);
}

TEST(FrameScratch, StepAiAndStepMissilesDoNotLeakBetweenCalls)
{
  GameLogic::FrameScratch scratch;
  uint32_t rng = 12345u;

  // First call against a world with no AiPilots/Missiles at all: must be a no-op
  // (in particular, must not crash dereferencing a stale pointer from a
  // previous call on a different registry - there is no previous call yet, but
  // this seeds the scratch's vectors with non-empty capacity for the next one).
  ECS::Registry empty;
  std::vector<uint32_t> pulses;
  EXPECT_EQ(GameLogic::StepAi(empty, /*tick*/ 0, rng, scratch), 0);
  EXPECT_TRUE(GameLogic::StepMissiles(empty, rng, pulses, scratch).empty());
  EXPECT_TRUE(pulses.empty());

  // Second call on a DIFFERENT world with an actual missile: the scratch's
  // internal id-snapshot vector must reflect only THIS world's missiles.
  ECS::Registry b;
  ECS::EntityId missile = b.Create();
  b.Add<GameLogic::WorldTransform>(missile, GameLogic::WorldTransform{ { 0, 0, 0 } });
  b.Add<GameLogic::Flight>(missile, GameLogic::Flight{});
  GameLogic::Missile m;
  m.life = 5;   // no target set (default EntityId): homes on nothing, just ages
  b.Add<GameLogic::Missile>(missile, m);

  pulses.clear();
  const std::vector<GameLogic::Kill> kills = GameLogic::StepMissiles(b, rng, pulses, scratch);
  EXPECT_TRUE(kills.empty());
  EXPECT_TRUE(b.IsValid(missile));   // life ticked down but not yet expired
  EXPECT_EQ(b.Get<GameLogic::Missile>(missile).life, 4);
}
