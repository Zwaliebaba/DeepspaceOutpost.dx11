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

  // --- Laser fire striking a target -----------------------------------------

  // How a target absorbs laser fire (legacy check_target special cases).
  enum class TargetClass
  {
    Normal,      // ordinary ship/asteroid: takes full laser strength
    Station,     // Coriolis/Dodec station: immune to laser fire
    Armoured,    // Constrictor/Cougar: only the military laser bites, at 1/4
  };

  // The legacy `laser` value (a ship's laser strength) for the military laser:
  // MILITARY_LASER (0x97) masked to its 7-bit strength.
  inline constexpr int MILITARY_LASER_STRENGTH = 0x97 & 127;   // = 23

  // Laser damage actually dealt to a target of the given class (legacy
  // check_target). `_laserStrength` is the firing laser's 7-bit strength.
  [[nodiscard]] inline int LaserDamageTo(TargetClass _target, int _laserStrength)
  {
    switch (_target)
    {
      case TargetClass::Station:
        return 0;
      case TargetClass::Armoured:
        return (_laserStrength == MILITARY_LASER_STRENGTH) ? _laserStrength / 4 : 0;
      case TargetClass::Normal:
      default:
        return _laserStrength;
    }
  }

  // Resolve a laser hit on a target with `_targetEnergy` hull/energy. Returns the
  // target's remaining energy and whether it was destroyed (energy <= 0, the
  // legacy explode_object trigger).
  struct LaserHitResult
  {
    int targetEnergy = 0;
    bool destroyed = false;
  };

  [[nodiscard]] inline LaserHitResult ResolveLaserHit(int _targetEnergy, TargetClass _target, int _laserStrength)
  {
    int energy = _targetEnergy - LaserDamageTo(_target, _laserStrength);
    return LaserHitResult{ energy, energy <= 0 };
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
