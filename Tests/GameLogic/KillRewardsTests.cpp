#include <gtest/gtest.h>

#include "GameLogic.h"

using namespace Neuron;
using namespace Neuron::GameLogic;

namespace
{
  // A player killer: has a Wallet + PlayerTag (so it can be paid), plus the
  // combat components a real player carries. (Score is a per-player SESSION
  // record since C2 - CreditKill reports the delta, it writes no component.)
  ECS::EntityId SpawnKillerPlayer(ECS::Registry& _w, int _credits = 1000)
  {
    const ECS::EntityId e = _w.Create();
    _w.Add<WorldTransform>(e, WorldTransform{ { 0, 0, 0 } });
    _w.Add<Wallet>(e, Wallet{ _credits });
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

  const KillCredit credit = CreditKill(w, killer.index, pirate);

  EXPECT_EQ(credit.bounty, PIRATE_BOUNTY);
  EXPECT_EQ(credit.score, 1);
  EXPECT_EQ(w.Get<Wallet>(killer).credits, 1000 + PIRATE_BOUNTY);
}

TEST(KillRewards, NpcKillerEarnsNothing)
{
  ECS::Registry w;
  // An NPC "killer": a Combatant with no Wallet/PlayerTag.
  const ECS::EntityId npc = w.Create();
  w.Add<Combatant>(npc, Combatant{ Team::Pirate, 80, 3, 5000, true });
  const ECS::EntityId victim = SpawnPirate(w);

  const KillCredit credit = CreditKill(w, npc.index, victim);
  EXPECT_EQ(credit.bounty, 0);   // no wallet to pay into
  EXPECT_EQ(credit.score, 0);
}

TEST(KillRewards, KillingAFugitivePlayerPaysAWantedDerivedBounty)
{
  ECS::Registry w;
  const ECS::EntityId killer = SpawnKillerPlayer(w, /*credits*/ 500);
  const ECS::EntityId fugitive = SpawnKillerPlayer(w, /*credits*/ 0);
  w.Get<Wanted>(fugitive).level = 3;

  const KillCredit credit = CreditKill(w, killer.index, fugitive);

  EXPECT_EQ(credit.bounty, 3 * WANTED_BOUNTY_PER_LEVEL);
  EXPECT_EQ(credit.score, 1);
  EXPECT_EQ(w.Get<Wallet>(killer).credits, 500 + 3 * WANTED_BOUNTY_PER_LEVEL);
}

TEST(KillRewards, KillingACleanPlayerPaysNoBountyButStillScores)
{
  ECS::Registry w;
  const ECS::EntityId killer = SpawnKillerPlayer(w, /*credits*/ 500);
  const ECS::EntityId innocent = SpawnKillerPlayer(w, /*credits*/ 0);   // wanted 0

  const KillCredit credit = CreditKill(w, killer.index, innocent);

  EXPECT_EQ(credit.bounty, 0);
  EXPECT_EQ(credit.score, 1);                           // the kill still counts
  EXPECT_EQ(w.Get<Wallet>(killer).credits, 500);        // no bounty
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
  const KillCredit credit = CreditKill(w, /*killerIndex*/ 9999u, pirate);   // no such entity
  EXPECT_EQ(credit.bounty, 0);
  EXPECT_EQ(credit.score, 0);
}
