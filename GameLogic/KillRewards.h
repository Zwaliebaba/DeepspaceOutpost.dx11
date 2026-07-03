#pragma once

// KillRewards - pay the killer for a destroyed ship (GameLogic, server-side).
//
// Wires the ported, unit-tested Combat.h ApplyKill primitive to the live world: a
// resolved kill credits the KILLER's wallet with the victim's bounty and bumps
// their score. The bounty is either an explicit Bounty component (NPC pirates) or,
// for a fugitive PLAYER, a reward derived from their wanted level. Only a player
// killer (one with a Wallet + PlayerRecord) earns anything - an NPC or a departed
// player is paid nothing. Pure apart from the killer's components it mutates, so it
// is unit-tested headlessly; the server loop calls it from the EntityKilled handler.

#include "ECS.h"

#include "Combat.h"            // ApplyKill / KillReward
#include "CombatSystem.h"      // Bounty, Wanted, PlayerTag, PlayerRecord, WANTED_BOUNTY_PER_LEVEL
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

  // Credit the killer for downing the victim: add the bounty to their wallet and
  // bump their score (ApplyKill). No-op unless the killer is a player with a
  // Wallet + PlayerRecord. Returns the bounty paid (0 if the killer earns nothing).
  // Witchspace bounty-withholding is not modelled yet, so `inWitchspace` is false.
  inline int CreditKill(ECS::Registry& _world, uint32_t _killerIndex, ECS::EntityId _victim)
  {
    const ECS::EntityId killer = _world.LiveEntity(_killerIndex);
    if (!_world.IsValid(killer))
      return 0;

    Wallet* wallet = _world.TryGet<Wallet>(killer);
    PlayerRecord* record = _world.TryGet<PlayerRecord>(killer);
    if (wallet == nullptr || record == nullptr)
      return 0;   // an NPC (or a departed player) earns nothing

    // A kill made in witchspace pays no bounty (legacy rule); the score still
    // counts. The already-implemented ApplyKill inWitchspace flag finally has its
    // caller (G7).
    const int bounty = BountyFor(_world, _victim);
    const bool inWitchspace = _world.Has<Witchspace>(killer);
    const KillReward reward = ApplyKill(wallet->credits, record->score, bounty, inWitchspace);
    wallet->credits = reward.credits;
    record->score = reward.score;
    return inWitchspace ? 0 : bounty;
  }
}
