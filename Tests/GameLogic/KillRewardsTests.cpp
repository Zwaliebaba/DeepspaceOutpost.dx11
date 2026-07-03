#include <gtest/gtest.h>

#include "GameLogic.h"

using namespace Neuron;
using namespace Neuron::GameLogic;

namespace
{
  // A player killer: has a Wallet + PlayerRecord (so it can be paid), plus the
  // combat/identity components a real player carries.
  ECS::EntityId SpawnKillerPlayer(ECS::Registry& _w, int _credits = 1000)
  {
    const ECS::EntityId e = _w.Create();
    _w.Add<WorldTransform>(e, WorldTransform{ { 0, 0, 0 } });
    _w.Add<Wallet>(e, Wallet{ _credits });
    _w.Add<PlayerRecord>(e, PlayerRecord{});
    _w.Add<PlayerTag>(e, PlayerTag{});
    _w.Add<Combatant>(e, Combatant{ Team::Player, 255, 10, 6000, false });
    _w.Add<Wanted>(e, Wanted{});
    return e;
  }

  ECS::EntityId SpawnPirate(ECS::Registry& _w, int _bounty = PIRATE_BOUNTY)
  {
    const ECS::EntityId e = _w.Create();
    _w.Add<WorldTransform>(e, WorldTransform{ { 0, 0, 1000 } });
    _w.Add<Combatant>(e, Combatant{ Team::Pirate, 1, 3, 5000, true });
    _w.Add<Bounty>(e, Bounty{ _bounty });
    return e;
  }
}

TEST(KillRewards, PlayerKillerEarnsThePirateBountyAndAScore)
{
  ECS::Registry w;
  const ECS::EntityId killer = SpawnKillerPlayer(w, /*credits*/ 1000);
  const ECS::EntityId pirate = SpawnPirate(w, /*bounty*/ PIRATE_BOUNTY);

  const int paid = CreditKill(w, killer.index, pirate);

  EXPECT_EQ(paid, PIRATE_BOUNTY);
  EXPECT_EQ(w.Get<Wallet>(killer).credits, 1000 + PIRATE_BOUNTY);
  EXPECT_EQ(w.Get<PlayerRecord>(killer).score, 1);
}

TEST(KillRewards, NpcKillerEarnsNothing)
{
  ECS::Registry w;
  // An NPC "killer": a Combatant with no Wallet/PlayerRecord.
  const ECS::EntityId npc = w.Create();
  w.Add<Combatant>(npc, Combatant{ Team::Pirate, 80, 3, 5000, true });
  const ECS::EntityId victim = SpawnPirate(w);

  EXPECT_EQ(CreditKill(w, npc.index, victim), 0);   // no wallet to pay into
}

TEST(KillRewards, KillingAFugitivePlayerPaysAWantedDerivedBounty)
{
  ECS::Registry w;
  const ECS::EntityId killer = SpawnKillerPlayer(w, /*credits*/ 500);
  const ECS::EntityId fugitive = SpawnKillerPlayer(w, /*credits*/ 0);
  w.Get<Wanted>(fugitive).level = 3;

  const int paid = CreditKill(w, killer.index, fugitive);

  EXPECT_EQ(paid, 3 * WANTED_BOUNTY_PER_LEVEL);
  EXPECT_EQ(w.Get<Wallet>(killer).credits, 500 + 3 * WANTED_BOUNTY_PER_LEVEL);
  EXPECT_EQ(w.Get<PlayerRecord>(killer).score, 1);
}

TEST(KillRewards, KillingACleanPlayerPaysNoBountyButStillScores)
{
  ECS::Registry w;
  const ECS::EntityId killer = SpawnKillerPlayer(w, /*credits*/ 500);
  const ECS::EntityId innocent = SpawnKillerPlayer(w, /*credits*/ 0);   // wanted 0

  const int paid = CreditKill(w, killer.index, innocent);

  EXPECT_EQ(paid, 0);
  EXPECT_EQ(w.Get<Wallet>(killer).credits, 500);        // no bounty
  EXPECT_EQ(w.Get<PlayerRecord>(killer).score, 1);      // but the kill still counts
}

TEST(KillRewards, BountyForIgnoresEntitiesWithoutABountyOrWantedRecord)
{
  ECS::Registry w;
  // A missile-like entity: NetType but no Combatant/Bounty/PlayerTag.
  const ECS::EntityId missile = w.Create();
  w.Add<NetType>(missile, NetType{ ShipType::Missile });

  EXPECT_EQ(BountyFor(w, missile), 0);
}

TEST(KillRewards, CreditKillIsANoOpForAnUnknownKiller)
{
  ECS::Registry w;
  const ECS::EntityId pirate = SpawnPirate(w);
  EXPECT_EQ(CreditKill(w, /*killerIndex*/ 9999u, pirate), 0);   // no such entity
}
