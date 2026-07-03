#pragma once

// Combat - authoritative damage resolution (GameLogic, A4).
//
// A faithful, decoupled port of the legacy combat math from space.cpp
// (damage_ship / decrease_energy) and swat.cpp (check_target / explode_object /
// bounty award). In the MMO the server is the sole arbiter of who took damage,
// who died, and who got paid, so these are pure functions over plain numbers:
// no global cmdr, no ship_list lookup, no rendering. The caller maps its own
// ship/laser tables onto the small enums below.

namespace Neuron::GameLogic
{
  // --- Player (or any shielded ship) taking a hit ---------------------------

  // The mutable defensive state of a shielded ship: two directional shields and
  // an energy bank that absorbs whatever punches through the shield.
  struct ShieldState
  {
    int frontShield = 0;
    int aftShield = 0;
    int energy = 0;
  };

  // Apply `_damage` to `_state`, hitting the front shield when `_hitFront` is
  // true (legacy damage_ship). Overflow past a depleted shield drains the energy
  // bank (legacy decrease_energy with the negative remainder); the shield floors
  // at zero. `destroyed` mirrors the legacy do_game_over() trigger: energy
  // reached zero or below.
  struct ShieldHitResult
  {
    ShieldState state;
    bool destroyed = false;
  };

  [[nodiscard]] inline ShieldHitResult ApplyDamageToShields(ShieldState _state, int _damage, bool _hitFront)
  {
    ShieldHitResult result{ _state, false };

    if (_damage <= 0)   // legacy sanity check: no effect
      return result;

    int shield = _hitFront ? _state.frontShield : _state.aftShield;
    shield -= _damage;
    if (shield < 0)
    {
      result.state.energy += shield;   // shield is negative -> drains energy
      shield = 0;
    }

    if (_hitFront)
      result.state.frontShield = shield;
    else
      result.state.aftShield = shield;

    result.destroyed = result.state.energy <= 0;
    return result;
  }

  // --- Kill reward ----------------------------------------------------------

  // Award for destroying a ship (legacy bounty payout + score bump on the kill).
  // Bounty is withheld in witchspace (the legacy `!witchspace` guard). Credits
  // are in legacy tenths-of-a-credit units; score increments by one per kill.
  struct KillReward
  {
    int credits = 0;
    int score = 0;
  };

  [[nodiscard]] inline KillReward ApplyKill(int _credits, int _score, int _bounty, bool _inWitchspace)
  {
    KillReward reward{ _credits, _score + 1 };
    if (_bounty != 0 && !_inWitchspace)
      reward.credits = _credits + _bounty;
    return reward;
  }
}
