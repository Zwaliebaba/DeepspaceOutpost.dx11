#include "TestFramework.h"

#include "Combat.h"

using namespace Neuron::GameLogic;

TEST(Combat_ShieldAbsorbsDamageWithoutTouchingEnergy)
{
  // 20 damage to a 64-front-shield ship: shield -> 44, energy untouched.
  ShieldHitResult r = ApplyDamageToShields(ShieldState{ 64, 64, 200 }, 20, /*front*/ true);
  CHECK(r.state.frontShield == 44);
  CHECK(r.state.aftShield == 64);
  CHECK(r.state.energy == 200);
  CHECK(!r.destroyed);
}

TEST(Combat_ShieldOverflowDrainsEnergy)
{
  // 30 damage to a 10-aft-shield ship: shield floors at 0, the 20 overflow
  // drains energy 200 -> 180. Not destroyed.
  ShieldHitResult r = ApplyDamageToShields(ShieldState{ 50, 10, 200 }, 30, /*front*/ false);
  CHECK(r.state.aftShield == 0);
  CHECK(r.state.frontShield == 50);
  CHECK(r.state.energy == 180);
  CHECK(!r.destroyed);
}

TEST(Combat_EnergyToZeroIsDestroyed)
{
  // Shield 5, energy 25, take 30: overflow 25 drains energy to 0 -> destroyed.
  ShieldHitResult r = ApplyDamageToShields(ShieldState{ 5, 5, 25 }, 30, /*front*/ true);
  CHECK(r.state.frontShield == 0);
  CHECK(r.state.energy == 0);
  CHECK(r.destroyed);
}

TEST(Combat_NonPositiveDamageIsNoOp)
{
  ShieldHitResult r = ApplyDamageToShields(ShieldState{ 40, 40, 100 }, 0, true);
  CHECK(r.state.frontShield == 40);
  CHECK(r.state.energy == 100);
  CHECK(!r.destroyed);
}

TEST(Combat_StationIsImmuneToLaser)
{
  CHECK(LaserDamageTo(TargetClass::Station, 100) == 0);
  LaserHitResult r = ResolveLaserHit(50, TargetClass::Station, 100);
  CHECK(r.targetEnergy == 50);
  CHECK(!r.destroyed);
}

TEST(Combat_ArmouredTakesOnlyMilitaryLaserAtQuarter)
{
  // Pulse laser (strength 15) does nothing to an armoured target.
  CHECK(LaserDamageTo(TargetClass::Armoured, 15) == 0);
  // Military laser (strength 23) does 23/4 = 5.
  CHECK(LaserDamageTo(TargetClass::Armoured, MILITARY_LASER_STRENGTH) == 5);
}

TEST(Combat_NormalTargetTakesFullStrengthAndDies)
{
  CHECK(LaserDamageTo(TargetClass::Normal, 23) == 23);
  LaserHitResult r = ResolveLaserHit(20, TargetClass::Normal, 23);
  CHECK(r.targetEnergy == -3);
  CHECK(r.destroyed);
}

TEST(Combat_BountyPaidOutsideWitchspaceScoreAlwaysBumps)
{
  KillReward paid = ApplyKill(/*credits*/ 1000, /*score*/ 4, /*bounty*/ 50, /*witch*/ false);
  CHECK(paid.credits == 1050);
  CHECK(paid.score == 5);

  // In witchspace the bounty is withheld but the kill still scores.
  KillReward withheld = ApplyKill(1000, 4, 50, /*witch*/ true);
  CHECK(withheld.credits == 1000);
  CHECK(withheld.score == 5);

  // A zero-bounty kill still scores.
  KillReward free = ApplyKill(1000, 4, 0, false);
  CHECK(free.credits == 1000);
  CHECK(free.score == 5);
}
