#include <gtest/gtest.h>

#include "GameLogic.h"

using namespace Neuron;
using namespace Neuron::GameLogic;

namespace
{
  // A player-shaped ship: gear, equipment, hold, dock state, record, shields.
  ECS::EntityId SpawnPilot(ECS::Registry& _w, Math::Vector3i64 _pos = { 0, 0, 0 })
  {
    const ECS::EntityId e = _w.Create();
    _w.Add<WorldTransform>(e, WorldTransform{ _pos });
    _w.Add<Flight>(e, Flight{});
    _w.Add<Combatant>(e, Combatant{ Team::Player, MAX_ENERGY, 10, 6000, false });
    _w.Add<PlayerTag>(e, PlayerTag{});
    _w.Add<Shields>(e, Shields{});
    _w.Add<ShipGear>(e, ShipGear{});
    _w.Add<Equipment>(e, Equipment{});
    _w.Add<CargoHold>(e, CargoHold{});
    _w.Add<DockState>(e, DockState{});
    _w.Add<Wanted>(e, Wanted{});
    _w.Add<Fuel>(e, Fuel{});
    _w.Add<Wallet>(e, Wallet{});
    return e;
  }

  ECS::EntityId SpawnNpcShip(ECS::Registry& _w, Math::Vector3i64 _pos, int _team = Team::Pirate)
  {
    const ECS::EntityId e = _w.Create();
    _w.Add<WorldTransform>(e, WorldTransform{ _pos });
    _w.Add<Combatant>(e, Combatant{ _team, 80, 3, 5000, true });
    return e;
  }

  // A live missile parked at a position (no target needed).
  ECS::EntityId SpawnLooseMissile(ECS::Registry& _w, Math::Vector3i64 _pos)
  {
    const ECS::EntityId e = _w.Create();
    _w.Add<WorldTransform>(e, WorldTransform{ _pos });
    _w.Add<Flight>(e, Flight{});
    _w.Add<Missile>(e, Missile{});
    return e;
  }

  ECS::EntityId SpawnStationAt(ECS::Registry& _w, Math::Vector3i64 _pos)
  {
    const ECS::EntityId e = _w.Create();
    _w.Add<WorldTransform>(e, WorldTransform{ _pos });
    _w.Add<Combatant>(e, Combatant{ Team::Station, 1000000, 0, 1, false });
    ServerStation ss;
    ss.systemId = 1;
    _w.Add<ServerStation>(e, ss);
    return e;
  }
}

// --- ECM ---------------------------------------------------------------------

TEST(EquipmentSys, EcmDownsEveryMissileInRangeIncludingYourOwn)
{
  ECS::Registry w;
  const ECS::EntityId ship = SpawnPilot(w);
  w.Get<Equipment>(ship).ecm = true;
  const ECS::EntityId near1 = SpawnLooseMissile(w, { 3000, 0, 0 });
  const ECS::EntityId near2 = SpawnLooseMissile(w, { 0, -8000, 0 });
  const ECS::EntityId far = SpawnLooseMissile(w, { ECM_RANGE + 500, 0, 0 });

  const EcmOutcome out = ActivateEcm(w, ship);

  ASSERT_TRUE(out.fired);
  ASSERT_EQ(out.kills.size(), 2u);   // both in-range missiles, the distant one spared
  for (const Kill& k : out.kills)
  {
    EXPECT_TRUE(k.victim == near1 || k.victim == near2);
    EXPECT_EQ(k.killer, ship.index);
  }
  EXPECT_TRUE(w.IsValid(far));
  EXPECT_EQ(w.Get<Combatant>(ship).energy, MAX_ENERGY - ECM_ENERGY_COST);   // the burst costs
  EXPECT_EQ(w.Get<ShipGear>(ship).ecmCooldown, ECM_COOLDOWN_TICKS);          // and recharges
}

TEST(EquipmentSys, EcmNeedsTheFittingChargeAndEnergy)
{
  ECS::Registry w;
  const ECS::EntityId ship = SpawnPilot(w);
  SpawnLooseMissile(w, { 1000, 0, 0 });

  EXPECT_FALSE(ActivateEcm(w, ship).fired);          // no ECM purchased

  w.Get<Equipment>(ship).ecm = true;
  w.Get<ShipGear>(ship).ecmCooldown = 5;
  EXPECT_FALSE(ActivateEcm(w, ship).fired);          // still recharging

  w.Get<ShipGear>(ship).ecmCooldown = 0;
  w.Get<Combatant>(ship).energy = ECM_ENERGY_COST;   // not strictly above the cost
  EXPECT_FALSE(ActivateEcm(w, ship).fired);          // too drained to fire

  w.Get<Combatant>(ship).energy = MAX_ENERGY;
  EXPECT_TRUE(ActivateEcm(w, ship).fired);
}

TEST(EquipmentSys, StepEquipmentCoolsTheLaserAndRechargesTheEcm)
{
  ECS::Registry w;
  const ECS::EntityId ship = SpawnPilot(w);
  w.Get<ShipGear>(ship).laserHeat = 3;
  w.Get<ShipGear>(ship).ecmCooldown = 2;

  StepEquipment(w);
  EXPECT_EQ(w.Get<ShipGear>(ship).laserHeat, 2);
  EXPECT_EQ(w.Get<ShipGear>(ship).ecmCooldown, 1);

  StepEquipment(w);
  StepEquipment(w);
  EXPECT_EQ(w.Get<ShipGear>(ship).laserHeat, 0);     // floors at zero
  EXPECT_EQ(w.Get<ShipGear>(ship).ecmCooldown, 0);
}

TEST(EquipmentSys, AHomingMissileCanBeJammedByAFittedTarget)
{
  ECS::Registry w;
  // The defender carries an NPC ECM fitting; the attacker fires a locked missile.
  const ECS::EntityId defender = SpawnNpcShip(w, { 0, 0, 20000 }, Team::Pirate);
  w.Add<EcmFitted>(defender, EcmFitted{});
  const ECS::EntityId shooter = SpawnPilot(w);
  const ECS::EntityId missile = SpawnMissile(w, shooter, defender.index);
  ASSERT_TRUE(w.IsValid(missile));

  // With the legacy 16/256 per-tick jam chance, a long homing run is jammed with
  // overwhelming probability before 200 ticks (the run itself takes ~110).
  uint32_t rng = 0xEC3u;
  std::vector<uint32_t> pulses;
  bool jammed = false;
  for (int i = 0; i < 200 && !jammed; ++i)
  {
    for (const Kill& k : StepMissiles(w, rng, pulses))
      if (k.victim == missile && k.killer == defender.index)
        jammed = true;
    if (jammed)
      break;
    if (!w.IsValid(missile))
      break;   // detonated instead (would fail the assertions below)
  }

  EXPECT_TRUE(jammed);
  ASSERT_FALSE(pulses.empty());
  EXPECT_EQ(pulses[0], defender.index);
  EXPECT_TRUE(w.IsValid(defender));   // the defender never took the hit
}

TEST(EquipmentSys, AnUnfittedTargetStillEatsTheMissile)
{
  ECS::Registry w;
  const ECS::EntityId defender = SpawnNpcShip(w, { 0, 0, 20000 }, Team::Pirate);   // no ECM
  const ECS::EntityId shooter = SpawnPilot(w);
  SpawnMissile(w, shooter, defender.index);

  uint32_t rng = 0xEC3u;
  std::vector<uint32_t> pulses;
  bool defenderDied = false;
  for (int i = 0; i < 300 && !defenderDied; ++i)
    for (const Kill& k : StepMissiles(w, rng, pulses))
      if (k.victim == defender)
        defenderDied = true;

  EXPECT_TRUE(defenderDied);
  EXPECT_TRUE(pulses.empty());   // no ECM, no pulses
}

// --- Energy bomb ---------------------------------------------------------------

TEST(EquipmentSys, TheBombKillsNpcsAndMissilesButSparesStationsAndPlayers)
{
  ECS::Registry w;
  const ECS::EntityId bomber = SpawnPilot(w);
  w.Get<Equipment>(bomber).energyBomb = true;
  const ECS::EntityId pirate = SpawnNpcShip(w, { 4000, 0, 0 }, Team::Pirate);
  const ECS::EntityId trader = SpawnNpcShip(w, { 0, 8000, 0 }, Team::Trader);
  const ECS::EntityId missile = SpawnLooseMissile(w, { 0, 0, 2000 });
  const ECS::EntityId station = SpawnStationAt(w, { 0, 0, 5000 });
  const ECS::EntityId otherPlayer = SpawnPilot(w, { 1000, 0, 0 });
  const ECS::EntityId distant = SpawnNpcShip(w, { ENERGY_BOMB_RADIUS + 1000, 0, 0 }, Team::Pirate);

  const BombOutcome out = DetonateEnergyBomb(w, bomber);

  ASSERT_TRUE(out.detonated);
  EXPECT_FALSE(w.Get<Equipment>(bomber).energyBomb);   // one shot, consumed

  auto killed = [&out](ECS::EntityId _e)
  {
    for (const Kill& k : out.kills)
      if (k.victim == _e)
        return true;
    return false;
  };
  EXPECT_TRUE(killed(pirate));
  EXPECT_TRUE(killed(trader));
  EXPECT_TRUE(killed(missile));
  EXPECT_FALSE(killed(station));       // the fortress shrugs it off
  EXPECT_FALSE(killed(otherPlayer));   // no area one-shots on people
  EXPECT_FALSE(killed(distant));       // outside the blast
}

TEST(EquipmentSys, TheBombNeedsOwnershipAndOpenSpace)
{
  ECS::Registry w;
  const ECS::EntityId ship = SpawnPilot(w);
  EXPECT_FALSE(DetonateEnergyBomb(w, ship).detonated);   // none purchased

  w.Get<Equipment>(ship).energyBomb = true;
  w.Get<DockState>(ship).docked = true;
  EXPECT_FALSE(DetonateEnergyBomb(w, ship).detonated);   // not inside a station
  EXPECT_TRUE(w.Get<Equipment>(ship).energyBomb);        // not consumed by the refusal

  w.Get<DockState>(ship).docked = false;
  EXPECT_TRUE(DetonateEnergyBomb(w, ship).detonated);
}

TEST(EquipmentSys, BombingTheLawIsACrimePerVictim)
{
  ECS::Registry w;
  const ECS::EntityId bomber = SpawnPilot(w);
  w.Get<Equipment>(bomber).energyBomb = true;
  SpawnNpcShip(w, { 2000, 0, 0 }, Team::Police);
  SpawnNpcShip(w, { 0, 2000, 0 }, Team::Trader);
  SpawnNpcShip(w, { 0, 0, 2000 }, Team::Pirate);   // fair game, no crime

  Msg::MessageBus bus;
  int crimes = 0;
  bus.Subscribe<Crime>([&crimes](const Crime&) { ++crimes; });

  ResolveFireWeapon(w, bus, FireWeapon{ bomber, Weapon::EnergyBomb, Net::NO_MISSILE_TARGET }, 6000, 0.9);
  bus.Dispatch();

  EXPECT_EQ(crimes, 2);                            // the cop and the civilian
  EXPECT_EQ(w.Get<Wanted>(bomber).level, 2);
}

// --- Escape pod ------------------------------------------------------------------

TEST(EquipmentSys, TheEscapePodLosesTheCargoClearsTheRecordAndDocksYou)
{
  ECS::Registry w;
  SpawnStationAt(w, { 0, 0, -3000 });
  const ECS::EntityId ship = SpawnPilot(w, { 50000, 0, 0 });
  w.Get<Equipment>(ship).escapePod = true;
  w.Get<CargoHold>(ship).units[0] = 5;
  w.Get<Wanted>(ship).level = 6;
  w.Get<Fuel>(ship).tenths = 3;
  w.Get<Combatant>(ship).energy = 40;
  w.Get<Shields>(ship).front = 10;

  ASSERT_TRUE(UseEscapePod(w, ship));

  EXPECT_FALSE(w.Get<Equipment>(ship).escapePod);      // consumed
  EXPECT_EQ(w.Get<CargoHold>(ship).units[0], 0);       // cargo went down with the ship
  EXPECT_EQ(w.Get<Wanted>(ship).level, 0);             // legacy: record cleared
  EXPECT_EQ(w.Get<Fuel>(ship).tenths, MAX_FUEL_TENTHS);// legacy: fresh tank
  EXPECT_EQ(w.Get<Combatant>(ship).energy, MAX_ENERGY);
  EXPECT_EQ(w.Get<Shields>(ship).front, MAX_SHIELD);
  EXPECT_GT(w.Get<Combatant>(ship).invulnTicks, 0);    // respawn grace
  EXPECT_TRUE(w.Get<DockState>(ship).docked);          // woke up docked
  EXPECT_EQ(w.Get<WorldTransform>(ship).position.z, -3000);   // ...at the station

  // No canisters: the hull vanished, nothing spilled.
  int canisters = 0;
  w.Each<LootItem>([&canisters](ECS::EntityId, LootItem&) { ++canisters; });
  EXPECT_EQ(canisters, 0);
}

TEST(EquipmentSys, ThePodIsBlockedDockedInWitchspaceOrUnowned)
{
  ECS::Registry w;
  SpawnStationAt(w, { 0, 0, -3000 });
  const ECS::EntityId ship = SpawnPilot(w);

  EXPECT_FALSE(UseEscapePod(w, ship));                 // none purchased

  w.Get<Equipment>(ship).escapePod = true;
  w.Get<DockState>(ship).docked = true;
  EXPECT_FALSE(UseEscapePod(w, ship));                 // already docked

  w.Get<DockState>(ship).docked = false;
  w.Add<Witchspace>(ship, Witchspace{});
  EXPECT_FALSE(UseEscapePod(w, ship));                 // no free ride out of a misjump
  EXPECT_TRUE(w.Get<Equipment>(ship).escapePod);       // never consumed by a refusal
}

// --- Laser heat ------------------------------------------------------------------

TEST(EquipmentSys, SustainedFireOverheatsAndLocksTheTrigger)
{
  ECS::Registry w;
  const ECS::EntityId ship = SpawnPilot(w);

  // Each pull heats +8; from cold the trigger locks after ceil(242/8) = 31 pulls.
  int shots = 0;
  while (SpendLaserShot(w, ship) && shots < 100)
    ++shots;

  EXPECT_EQ(shots, 31);
  EXPECT_GE(w.Get<ShipGear>(ship).laserHeat, LASER_HEAT_BLOCK);
  EXPECT_EQ(w.Get<Combatant>(ship).energy, MAX_ENERGY - 31);   // each shot sipped the bank

  // Cooling reopens it (1/tick).
  for (int i = 0; i < 20; ++i)
    StepEquipment(w);
  EXPECT_TRUE(SpendLaserShot(w, ship));
}

TEST(EquipmentSys, AnOverheatedLaserFiresNoShot)
{
  ECS::Registry w;
  const ECS::EntityId shooter = SpawnPilot(w);
  w.Get<ShipGear>(shooter).laserHeat = LASER_HEAT_BLOCK;
  const ECS::EntityId prey = SpawnNpcShip(w, { 0, 0, 1000 });   // dead ahead

  Msg::MessageBus bus;
  ResolveFireWeapon(w, bus, FireWeapon{ shooter, Weapon::Laser, Net::NO_MISSILE_TARGET }, 6000, 0.9);
  bus.Dispatch();

  EXPECT_EQ(w.Get<Combatant>(prey).energy, 80);   // untouched: the trigger was locked
}

TEST(EquipmentSys, NpcLasersAreHeatFree)
{
  ECS::Registry w;
  const ECS::EntityId npc = SpawnNpcShip(w, { 0, 0, 0 });   // no ShipGear
  for (int i = 0; i < 100; ++i)
    EXPECT_TRUE(SpendLaserShot(w, npc));   // never blocks, never heats
}
