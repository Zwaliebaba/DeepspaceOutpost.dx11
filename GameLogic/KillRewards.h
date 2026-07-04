#pragma once

// KillRewards - pay the killer for a destroyed ship (GameLogic, server-side).
//
// Wires the ported, unit-tested Combat.h ApplyKill primitive to the live world: a
// resolved kill credits the KILLER's wallet with the victim's bounty and reports
// the score earned. The bounty is either an explicit Bounty component (NPC
// pirates) or, for a fugitive PLAYER, a reward derived from their wanted level.
// Only a player killer (one with a Wallet + PlayerTag) earns anything - an NPC or
// a departed player is paid nothing.
//
// Since C2 the SCORE is not written here: score is a player-level record living
// on the session (off the hull), so CreditKill returns the earned delta and the
// server routes it to the killer's session (ServerSessions::AddScore). The wallet
// stays ship-borne and is still paid in place. Pure apart from that wallet, so it
// is unit-tested headlessly; the server loop calls it from the EntityKilled handler.

#include "ECS.h"

#include "Combat.h"            // ApplyKill / KillReward
#include "CombatSystem.h"      // Bounty, Wanted, PlayerTag, WANTED_BOUNTY_PER_LEVEL
#include "StationServices.h"   // Wallet

namespace Neuron::GameLogic
{
  // The bounty payable for destroying `_victim`: an explicit Bounty component (a
  // pirate), or - for a wanted PLAYER - a reward scaled by their wanted level. A
  // clean player, or a victim with neither, is worth nothing.
  [[nodiscard]] inline int BountyFor(ECS::Registry& _world, ECS::EntityId _victim)
  {
    if (const Bounty* b = _world.TryGet<Bounty>(_victim))
      return b->value;
    if (_world.TryGet<PlayerTag>(_victim) != nullptr)
      if (const Wanted* w = _world.TryGet<Wanted>(_victim))
        return (w->level > 0) ? w->level * WANTED_BOUNTY_PER_LEVEL : 0;
    return 0;
  }

  // What a resolved kill actually earned the killer: the bounty PAID into their
  // wallet (0 when withheld or worthless) and the score DELTA the caller must
  // route to the killer's per-player record (the session, since C2).
  struct KillCredit
  {
    int bounty = 0;
    int score = 0;
  };

  // Credit the killer for downing the victim: add the bounty to their wallet in
  // place and report the score earned. No-op ({0,0}) unless the killer is a
  // player with a Wallet + PlayerTag. A kill made in witchspace pays no bounty
  // (legacy rule) - the score still counts.
  [[nodiscard]] inline KillCredit CreditKill(ECS::Registry& _world, uint32_t _killerIndex, ECS::EntityId _victim)
  {
    const ECS::EntityId killer = _world.LiveEntity(_killerIndex);
    if (!_world.IsValid(killer))
      return {};

    Wallet* wallet = _world.TryGet<Wallet>(killer);
    if (wallet == nullptr || _world.TryGet<PlayerTag>(killer) == nullptr)
      return {};   // an NPC (or a departed player) earns nothing

    // The already-implemented ApplyKill inWitchspace flag has its caller (G7);
    // score is fed as 0 so the returned value IS the delta for this kill.
    const int bounty = BountyFor(_world, _victim);
    const bool inWitchspace = _world.Has<Witchspace>(killer);
    const KillReward reward = ApplyKill(wallet->credits, /*score*/ 0, bounty, inWitchspace);
    wallet->credits = reward.credits;
    return KillCredit{ inWitchspace ? 0 : bounty, reward.score };
  }
}
