#include <gtest/gtest.h>

#include "Combat.h"

using namespace Neuron::GameLogic;

TEST(Combat, ShieldAbsorbsDamageWithoutTouchingEnergy)
{
  // 20 damage to a 64-front-shield ship: shield -> 44, energy untouched.
  ShieldHitResult r = ApplyDamageToShields(ShieldState{ 64, 64, 200 }, 20, /*front*/ true);
  EXPECT_TRUE(r.state.frontShield == 44);
  EXPECT_TRUE(r.state.aftShield == 64);
  EXPECT_TRUE(r.state.energy == 200);
  EXPECT_TRUE(!r.destroyed);
}

TEST(Combat, ShieldOverflowDrainsEnergy)
{
  // 30 damage to a 10-aft-shield ship: shield floors at 0, the 20 overflow
  // drains energy 200 -> 180. Not destroyed.
  ShieldHitResult r = ApplyDamageToShields(ShieldState{ 50, 10, 200 }, 30, /*front*/ false);
  EXPECT_TRUE(r.state.aftShield == 0);
  EXPECT_TRUE(r.state.frontShield == 50);
  EXPECT_TRUE(r.state.energy == 180);
  EXPECT_TRUE(!r.destroyed);
}

TEST(Combat, EnergyToZeroIsDestroyed)
{
  // Shield 5, energy 25, take 30: overflow 25 drains energy to 0 -> destroyed.
  ShieldHitResult r = ApplyDamageToShields(ShieldState{ 5, 5, 25 }, 30, /*front*/ true);
  EXPECT_TRUE(r.state.frontShield == 0);
  EXPECT_TRUE(r.state.energy == 0);
  EXPECT_TRUE(r.destroyed);
}

TEST(Combat, NonPositiveDamageIsNoOp)
{
  ShieldHitResult r = ApplyDamageToShields(ShieldState{ 40, 40, 100 }, 0, true);
  EXPECT_TRUE(r.state.frontShield == 40);
  EXPECT_TRUE(r.state.energy == 100);
  EXPECT_TRUE(!r.destroyed);
}

TEST(Combat, StationIsImmuneToLaser)
{
  EXPECT_TRUE(LaserDamageTo(TargetClass::Station, 100) == 0);
  LaserHitResult r = ResolveLaserHit(50, TargetClass::Station, 100);
  EXPECT_TRUE(r.targetEnergy == 50);
  EXPECT_TRUE(!r.destroyed);
}

TEST(Combat, ArmouredTakesOnlyMilitaryLaserAtQuarter)
{
  // Pulse laser (strength 15) does nothing to an armoured target.
  EXPECT_TRUE(LaserDamageTo(TargetClass::Armoured, 15) == 0);
  // Military laser (strength 23) does 23/4 = 5.
  EXPECT_TRUE(LaserDamageTo(TargetClass::Armoured, MILITARY_LASER_STRENGTH) == 5);
}

TEST(Combat, NormalTargetTakesFullStrengthAndDies)
{
  EXPECT_TRUE(LaserDamageTo(TargetClass::Normal, 23) == 23);
  LaserHitResult r = ResolveLaserHit(20, TargetClass::Normal, 23);
  EXPECT_TRUE(r.targetEnergy == -3);
  EXPECT_TRUE(r.destroyed);
}

TEST(Combat, BountyPaidOutsideWitchspaceScoreAlwaysBumps)
{
  KillReward paid = ApplyKill(/*credits*/ 1000, /*score*/ 4, /*bounty*/ 50, /*witch*/ false);
  EXPECT_TRUE(paid.credits == 1050);
  EXPECT_TRUE(paid.score == 5);

  // In witchspace the bounty is withheld but the kill still scores.
  KillReward withheld = ApplyKill(1000, 4, 50, /*witch*/ true);
  EXPECT_TRUE(withheld.credits == 1000);
  EXPECT_TRUE(withheld.score == 5);

  // A zero-bounty kill still scores.
  KillReward free = ApplyKill(1000, 4, 0, false);
  EXPECT_TRUE(free.credits == 1000);
  EXPECT_TRUE(free.score == 5);
}
