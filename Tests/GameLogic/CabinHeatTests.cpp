#include <gtest/gtest.h>

#include "GameLogic.h"

using namespace Neuron;
using namespace Neuron::GameLogic;

namespace
{
  ECS::EntityId MakeSun(ECS::Registry& _w, const Math::Vector3i64& _pos)
  {
    const ECS::EntityId e = _w.Create();
    _w.Add<WorldTransform>(e, WorldTransform{ _pos });
    _w.Add<Sun>(e, Sun{});
    return e;
  }

  // A heat-bearing ship (like a spawned player) at _pos.
  ECS::EntityId MakeShip(ECS::Registry& _w, const Math::Vector3i64& _pos, int _temp = 0, int _energy = 255)
  {
    const ECS::EntityId e = _w.Create();
    _w.Add<WorldTransform>(e, WorldTransform{ _pos });
    _w.Add<Combatant>(e, Combatant{ Team::Player, _energy, 10, 6000, false });
    _w.Add<CabinHeat>(e, CabinHeat{ _temp });
    return e;
  }
}

TEST(CabinHeat, RisesInsideTheBandAndFallsOutside)
{
  ECS::Registry w;
  MakeSun(w, { 0, 0, 0 });
  const ECS::EntityId hot = MakeShip(w, { 10000, 0, 0 });         // Chebyshev 10k < 60k: in band
  const ECS::EntityId cool = MakeShip(w, { 500000, 0, 0 }, 100);  // far out: cooling

  std::ignore = StepCabinHeat(w);

  EXPECT_EQ(w.Get<CabinHeat>(hot).temp, CABIN_HEAT_RISE);          // 0 -> +rise
  EXPECT_EQ(w.Get<CabinHeat>(cool).temp, 100 - CABIN_HEAT_FALL);   // 100 -> -fall
}

TEST(CabinHeat, TemperatureClampsAtZeroAndMax)
{
  ECS::Registry w;
  MakeSun(w, { 0, 0, 0 });
  const ECS::EntityId cold = MakeShip(w, { 500000, 0, 0 }, 1);         // will floor at 0
  const ECS::EntityId maxed = MakeShip(w, { 0, 0, 0 }, CABIN_HEAT_MAX); // in band, already max

  std::ignore = StepCabinHeat(w);

  EXPECT_EQ(w.Get<CabinHeat>(cold).temp, 0);
  EXPECT_EQ(w.Get<CabinHeat>(maxed).temp, CABIN_HEAT_MAX);
}

TEST(CabinHeat, CooksTheHullToDeathAtSustainedMaximum)
{
  ECS::Registry w;
  MakeSun(w, { 0, 0, 0 });
  const ECS::EntityId ship = MakeShip(w, { 0, 0, 0 }, CABIN_HEAT_MAX, /*energy*/ CABIN_HEAT_DAMAGE);

  const std::vector<Kill> kills = StepCabinHeat(w);

  EXPECT_LE(w.Get<Combatant>(ship).energy, 0);
  ASSERT_EQ(kills.size(), 1u);
  EXPECT_EQ(kills[0].victim, ship);
}

TEST(CabinHeat, SpawnGraceProtectsAFreshlySpawnedShip)
{
  ECS::Registry w;
  MakeSun(w, { 0, 0, 0 });
  const ECS::EntityId ship = MakeShip(w, { 0, 0, 0 }, CABIN_HEAT_MAX, /*energy*/ CABIN_HEAT_DAMAGE);
  w.Get<Combatant>(ship).invulnTicks = 10;   // just (re)spawned

  const std::vector<Kill> kills = StepCabinHeat(w);

  EXPECT_EQ(w.Get<Combatant>(ship).energy, CABIN_HEAT_DAMAGE);   // not cooked
  EXPECT_TRUE(kills.empty());
}

TEST(CabinHeat, FuelScoopTopsTheTankSkimmingTheBand)
{
  ECS::Registry w;
  MakeSun(w, { 0, 0, 0 });
  const ECS::EntityId ship = MakeShip(w, { 20000, 0, 0 });   // in band
  Equipment eq; eq.fuelScoop = true;
  w.Add<Equipment>(ship, eq);
  w.Add<Fuel>(ship, Fuel{ /*tenths*/ 10, /*max*/ 70 });

  std::ignore = StepCabinHeat(w);

  EXPECT_EQ(w.Get<Fuel>(ship).tenths, 10 + FUEL_SCOOP_GAIN);
}

TEST(CabinHeat, NoScoopOrOutOfBandGainsNoFuel)
{
  ECS::Registry w;
  MakeSun(w, { 0, 0, 0 });
  // In the band but no scoop.
  const ECS::EntityId noScoop = MakeShip(w, { 20000, 0, 0 });
  w.Add<Fuel>(noScoop, Fuel{ 10, 70 });
  // Scoop-fitted but far from the sun.
  const ECS::EntityId farOff = MakeShip(w, { 500000, 0, 0 });
  Equipment eq; eq.fuelScoop = true;
  w.Add<Equipment>(farOff, eq);
  w.Add<Fuel>(farOff, Fuel{ 10, 70 });

  std::ignore = StepCabinHeat(w);

  EXPECT_EQ(w.Get<Fuel>(noScoop).tenths, 10);
  EXPECT_EQ(w.Get<Fuel>(farOff).tenths, 10);
}

TEST(CabinHeat, FullTankDoesNotOverfill)
{
  ECS::Registry w;
  MakeSun(w, { 0, 0, 0 });
  const ECS::EntityId ship = MakeShip(w, { 20000, 0, 0 });
  Equipment eq; eq.fuelScoop = true;
  w.Add<Equipment>(ship, eq);
  w.Add<Fuel>(ship, Fuel{ 70, 70 });   // already full

  std::ignore = StepCabinHeat(w);

  EXPECT_EQ(w.Get<Fuel>(ship).tenths, 70);
}

TEST(CabinHeat, NoSunsMeansNoHeatChange)
{
  ECS::Registry w;
  const ECS::EntityId ship = MakeShip(w, { 0, 0, 0 }, 100);   // no sun in the world

  const std::vector<Kill> kills = StepCabinHeat(w);

  EXPECT_EQ(w.Get<CabinHeat>(ship).temp, 100);   // unchanged
  EXPECT_TRUE(kills.empty());
}
