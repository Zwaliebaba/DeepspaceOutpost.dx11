#include <gtest/gtest.h>

#include "GameLogic.h"

using namespace Neuron;
using namespace Neuron::GameLogic;

namespace
{
  ECS::EntityId SpawnShip(ECS::Registry& _w, Math::Vector3i64 _pos, int _team = Team::Pirate, int _energy = 300)
  {
    const ECS::EntityId e = _w.Create();
    _w.Add<WorldTransform>(e, WorldTransform{ _pos });
    _w.Add<Combatant>(e, Combatant{ _team, _energy, 0, 1, false });
    return e;
  }

  ECS::EntityId SpawnStation(ECS::Registry& _w, Math::Vector3i64 _pos)
  {
    const ECS::EntityId e = _w.Create();
    _w.Add<WorldTransform>(e, WorldTransform{ _pos });
    _w.Add<Combatant>(e, Combatant{ Team::Station, 1000000, 0, 1, false });
    return e;
  }

  ECS::EntityId SpawnPlanet(ECS::Registry& _w, Math::Vector3i64 _pos)
  {
    const ECS::EntityId e = _w.Create();
    _w.Add<WorldTransform>(e, WorldTransform{ _pos });
    _w.Add<NetType>(e, NetType{ ShipType::Planet });
    return e;
  }
}

TEST(Collisions, RammingGrindsBothHulls)
{
  ECS::Registry w;
  const ECS::EntityId a = SpawnShip(w, { 0, 0, 0 });
  const ECS::EntityId b = SpawnShip(w, { 0, 0, 400 }, Team::Player);

  const std::vector<Kill> kills = StepCollisions(w);

  EXPECT_TRUE(kills.empty());   // both survive the scrape
  EXPECT_EQ(w.Get<Combatant>(a).energy, 300 - SHIP_RAM_DAMAGE);
  EXPECT_EQ(w.Get<Combatant>(b).energy, 300 - SHIP_RAM_DAMAGE);
}

TEST(Collisions, APlayerAbsorbsTheRamOnTheFacingShield)
{
  ECS::Registry w;
  const ECS::EntityId player = SpawnShip(w, { 0, 0, 0 }, Team::Player, MAX_ENERGY);
  w.Add<Flight>(player, Flight{});          // nose +z
  w.Add<Shields>(player, Shields{});        // full front/aft
  SpawnShip(w, { 0, 0, 400 });              // contact dead AHEAD

  std::ignore = StepCollisions(w);

  EXPECT_EQ(w.Get<Shields>(player).front, MAX_SHIELD - SHIP_RAM_DAMAGE);   // front took it
  EXPECT_EQ(w.Get<Shields>(player).aft, MAX_SHIELD);                        // aft untouched
  EXPECT_EQ(w.Get<Combatant>(player).energy, MAX_ENERGY);                   // bank intact
}

TEST(Collisions, ARamKillCreditsTheOtherShip)
{
  ECS::Registry w;
  const ECS::EntityId weak = SpawnShip(w, { 0, 0, 0 }, Team::Pirate, /*energy*/ 50);
  const ECS::EntityId strong = SpawnShip(w, { 0, 0, 400 }, Team::Player, /*energy*/ 300);

  const std::vector<Kill> kills = StepCollisions(w);

  ASSERT_EQ(kills.size(), 1u);
  EXPECT_TRUE(kills[0].victim == weak);
  EXPECT_EQ(kills[0].killer, strong.index);
  EXPECT_EQ(w.Get<Combatant>(strong).energy, 200);   // the survivor still paid for it
}

TEST(Collisions, ContactBeyondTheRangeIsFree)
{
  ECS::Registry w;
  const ECS::EntityId a = SpawnShip(w, { 0, 0, 0 });
  const ECS::EntityId b = SpawnShip(w, { 0, 0, SHIP_CONTACT_RANGE + 1 }, Team::Player);

  EXPECT_TRUE(StepCollisions(w).empty());
  EXPECT_EQ(w.Get<Combatant>(a).energy, 300);
  EXPECT_EQ(w.Get<Combatant>(b).energy, 300);
}

TEST(Collisions, DockedShipsAreInsideTheStationNotInSpace)
{
  ECS::Registry w;
  const ECS::EntityId docked = SpawnShip(w, { 0, 0, 0 }, Team::Player);
  w.Add<DockState>(docked, DockState{ /*docked*/ true, 0 });
  const ECS::EntityId passerby = SpawnShip(w, { 0, 0, 100 });   // overlapping

  EXPECT_TRUE(StepCollisions(w).empty());
  EXPECT_EQ(w.Get<Combatant>(docked).energy, 300);      // exempt both ways
  EXPECT_EQ(w.Get<Combatant>(passerby).energy, 300);
}

TEST(Collisions, RespawnGraceBlocksCollisionDamageWithoutConsumingIt)
{
  ECS::Registry w;
  const ECS::EntityId graced = SpawnShip(w, { 0, 0, 0 }, Team::Player);
  w.Get<Combatant>(graced).invulnTicks = 10;
  const ECS::EntityId other = SpawnShip(w, { 0, 0, 400 });

  std::ignore = StepCollisions(w);

  EXPECT_EQ(w.Get<Combatant>(graced).energy, 300);        // no damage taken...
  EXPECT_EQ(w.Get<Combatant>(graced).invulnTicks, 10);    // ...and StepCombat still owns the countdown
  EXPECT_EQ(w.Get<Combatant>(other).energy, 300 - SHIP_RAM_DAMAGE);   // the graced hull still hurts to hit
}

TEST(Collisions, StationScrapesHurtTheShipOnly)
{
  ECS::Registry w;
  const ECS::EntityId station = SpawnStation(w, { 0, 0, 0 });
  const ECS::EntityId ship = SpawnShip(w, { 0, 0, 800 }, Team::Player);        // inside the hull range
  const ECS::EntityId clear = SpawnShip(w, { 5000, 0, 800 });                  // well away from both

  EXPECT_TRUE(StepCollisions(w).empty());
  EXPECT_EQ(w.Get<Combatant>(ship).energy, 300 - STATION_CRASH_DAMAGE);
  EXPECT_EQ(w.Get<Combatant>(station).energy, 1000000);   // the fortress doesn't notice
  EXPECT_EQ(w.Get<Combatant>(clear).energy, 300);          // bystander untouched
}

TEST(Collisions, FlyingIntoThePlanetKills)
{
  ECS::Registry w;
  const ECS::EntityId planet = SpawnPlanet(w, { 0, 0, 65536 });
  const ECS::EntityId ship = SpawnShip(w, { 0, 0, 65536 - PLANET_KILL_RADIUS + 100 }, Team::Player);

  const std::vector<Kill> kills = StepCollisions(w);

  ASSERT_EQ(kills.size(), 1u);
  EXPECT_TRUE(kills[0].victim == ship);
  EXPECT_EQ(kills[0].killer, planet.index);
  EXPECT_EQ(w.Get<Combatant>(ship).energy, 0);
}

TEST(Collisions, ThePlanetSparesGraceAndOrbit)
{
  ECS::Registry w;
  SpawnPlanet(w, { 0, 0, 65536 });
  // In the kill zone but under respawn grace: alive.
  const ECS::EntityId graced = SpawnShip(w, { 0, 0, 65536 - 1000 }, Team::Player);
  w.Get<Combatant>(graced).invulnTicks = 5;
  // At the trader lane gate: safely outside.
  const ECS::EntityId atGate = SpawnShip(w, { 0, PLANET_LANE_GATE, 65536 });

  EXPECT_TRUE(StepCollisions(w).empty());
  EXPECT_EQ(w.Get<Combatant>(graced).energy, 300);
  EXPECT_EQ(w.Get<Combatant>(atGate).energy, 300);
}

TEST(Collisions, TheStationOrbitIsClearOfThePlanetKillZone)
{
  // Geometry guard: a station sits 8000 from its planet (GalaxyConfig
  // stationOrbit); the kill radius must leave real margin for docking traffic.
  EXPECT_LT(PLANET_KILL_RADIUS + STATION_CONTACT_RANGE, 8000);
  EXPECT_LT(SHIP_CONTACT_RANGE, LAUNCH_OFFSET);       // undock is born clear of hulls
  EXPECT_LT(STATION_CONTACT_RANGE, LAUNCH_OFFSET);    // and clear of the station hull
}
