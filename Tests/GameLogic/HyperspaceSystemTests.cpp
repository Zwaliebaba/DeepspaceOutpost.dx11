#include <gtest/gtest.h>

#include "GameLogic.h"

using namespace Neuron;
using namespace Neuron::GameLogic;

namespace
{
  // A jump-capable player: transform, full tank, dock state, clean record.
  ECS::EntityId SpawnJumper(ECS::Registry& _w, Math::Vector3i64 _pos = { 0, 0, 0 })
  {
    const ECS::EntityId e = _w.Create();
    _w.Add<WorldTransform>(e, WorldTransform{ _pos });
    _w.Add<Fuel>(e, Fuel{});
    _w.Add<DockState>(e, DockState{ /*docked*/ true, 0 });
    _w.Add<Wanted>(e, Wanted{});
    return e;
  }

  // A destination system: a station carrying that system id at a world position.
  ECS::EntityId SpawnSystemStation(ECS::Registry& _w, int _systemId, Math::Vector3i64 _pos)
  {
    const ECS::EntityId e = _w.Create();
    _w.Add<WorldTransform>(e, WorldTransform{ _pos });
    ServerStation ss;
    ss.systemId = _systemId;
    _w.Add<ServerStation>(e, ss);
    return e;
  }

  // Mirror of Detail::AiRand255's first draw, to pick a seed that will (or won't)
  // trip the witchspace roll on the first call - keeps the witchspace tests
  // deterministic without reaching into the header internals.
  uint32_t SeedRollingOver(bool _wantWitchspace)
  {
    for (uint32_t s = 1; s < 1'000'000; ++s)
    {
      const uint32_t r = s * 1664525u + 1013904223u;
      const bool witch = ((r >> 8) & 0xFFu) > WITCHSPACE_ROLL_THRESHOLD;
      if (witch == _wantWitchspace)
        return s;
    }
    return 0;
  }

  int Thargoids(ECS::Registry& _w)
  {
    int n = 0;
    _w.Each<NetType>([&n](ECS::EntityId, NetType& _nt) { if (_nt.type == ShipType::Thargoid) ++n; });
    return n;
  }
}

// --- Cost -------------------------------------------------------------------

TEST(Hyperspace, JumpCostScalesWithDistanceAndFloorsAtOne)
{
  EXPECT_EQ(JumpCostTenths({ 0, 0, 0 }, { 0, 0, 0 }), 1);                    // adjacent still costs 0.1 LY
  EXPECT_EQ(JumpCostTenths({ 0, 0, 0 }, { UNITS_PER_TENTH_LY * 10, 0, 0 }), 10);   // 1.0 LY
  EXPECT_EQ(JumpCostTenths({ 0, 0, 0 }, { UNITS_PER_TENTH_LY * 35, 0, 0 }), 35);   // 3.5 LY
}

// --- Hyperspace jump --------------------------------------------------------

TEST(Hyperspace, ASuccessfulJumpSpendsFuelAndArrivesInFlight)
{
  ECS::Registry w;
  const ECS::EntityId p = SpawnJumper(w);
  w.Get<Wanted>(p).level = 4;
  const Math::Vector3i64 destPos{ UNITS_PER_TENTH_LY * 10, 0, 0 };   // 1.0 LY away
  SpawnSystemStation(w, /*systemId*/ 7, destPos);

  uint32_t rng = SeedRollingOver(/*witchspace*/ false);
  const HyperspaceOutcome out = Hyperspace(w, p, 7, rng);

  EXPECT_TRUE(out.status == Msg::TravelStatus::Arrived);
  EXPECT_TRUE(out.jumped);
  EXPECT_FALSE(out.witchspace);
  EXPECT_EQ(w.Get<Fuel>(p).tenths, MAX_FUEL_TENTHS - 10);   // 1.0 LY burned
  EXPECT_FALSE(w.Get<DockState>(p).docked);                 // arrived in flight
  EXPECT_EQ(w.Get<Wanted>(p).level, 2);                     // record halved
  EXPECT_TRUE(out.wantedChanged);
  // Dropped in near the destination station (a launch offset out).
  EXPECT_EQ(w.Get<WorldTransform>(p).position.x, destPos.x);
  EXPECT_EQ(w.Get<WorldTransform>(p).position.z, destPos.z + LAUNCH_OFFSET);
}

TEST(Hyperspace, ATooExpensiveJumpIsRejectedWithoutSpendingFuel)
{
  ECS::Registry w;
  const ECS::EntityId p = SpawnJumper(w);
  w.Get<Fuel>(p).tenths = 5;   // only 0.5 LY in the tank
  SpawnSystemStation(w, 7, { UNITS_PER_TENTH_LY * 10, 0, 0 });   // needs 1.0 LY

  uint32_t rng = 1u;
  const HyperspaceOutcome out = Hyperspace(w, p, 7, rng);

  EXPECT_TRUE(out.status == Msg::TravelStatus::NotEnoughFuel);
  EXPECT_FALSE(out.jumped);
  EXPECT_EQ(w.Get<Fuel>(p).tenths, 5);                 // untouched
  EXPECT_TRUE(w.Get<DockState>(p).docked);             // still on the pad
}

TEST(Hyperspace, ADestinationBeyondAFullTankIsOutOfRange)
{
  ECS::Registry w;
  const ECS::EntityId p = SpawnJumper(w);
  SpawnSystemStation(w, 7, { UNITS_PER_TENTH_LY * 100, 0, 0 });   // 10 LY, tank holds 7

  uint32_t rng = 1u;
  const HyperspaceOutcome out = Hyperspace(w, p, 7, rng);

  EXPECT_TRUE(out.status == Msg::TravelStatus::OutOfRange);
  EXPECT_FALSE(out.jumped);
  EXPECT_EQ(w.Get<Fuel>(p).tenths, MAX_FUEL_TENTHS);   // full tank untouched
}

TEST(Hyperspace, AnUnknownDestinationCantDock)
{
  ECS::Registry w;
  const ECS::EntityId p = SpawnJumper(w);
  SpawnSystemStation(w, 7, { UNITS_PER_TENTH_LY * 10, 0, 0 });

  uint32_t rng = 1u;
  const HyperspaceOutcome out = Hyperspace(w, p, /*no such system*/ 99, rng);

  EXPECT_TRUE(out.status == Msg::TravelStatus::UnknownSystem);
  EXPECT_FALSE(out.jumped);
  EXPECT_EQ(w.Get<Fuel>(p).tenths, MAX_FUEL_TENTHS);
}

TEST(Hyperspace, AMisjumpStrandsYouInWitchspaceWithThargoids)
{
  ECS::Registry w;
  const ECS::EntityId p = SpawnJumper(w);
  const Math::Vector3i64 destPos{ UNITS_PER_TENTH_LY * 10, 0, 0 };
  SpawnSystemStation(w, 7, destPos);

  uint32_t rng = SeedRollingOver(/*witchspace*/ true);
  const HyperspaceOutcome out = Hyperspace(w, p, 7, rng);

  EXPECT_TRUE(out.status == Msg::TravelStatus::Witchspace);
  EXPECT_TRUE(out.jumped);
  EXPECT_TRUE(out.witchspace);
  EXPECT_TRUE(w.Has<Witchspace>(p));                       // marked stranded
  EXPECT_EQ(w.Get<Fuel>(p).tenths, MAX_FUEL_TENTHS - 10);  // fuel still spent
  EXPECT_FALSE(w.Get<DockState>(p).docked);
  EXPECT_GE(Thargoids(w), 1);                              // ambushed
  EXPECT_LE(Thargoids(w), 4);                              // legacy (rand & 3) + 1
  // Flung far from the destination (deep space).
  EXPECT_GT(w.Get<WorldTransform>(p).position.x, destPos.x + WITCHSPACE_DISPLACEMENT - 1);
}

TEST(Hyperspace, AnOnwardJumpClearsWitchspace)
{
  ECS::Registry w;
  const ECS::EntityId p = SpawnJumper(w);
  w.Add<Witchspace>(p, Witchspace{});                     // stranded from a prior misjump
  SpawnSystemStation(w, 7, { UNITS_PER_TENTH_LY * 10, 0, 0 });

  uint32_t rng = SeedRollingOver(/*witchspace*/ false);
  const HyperspaceOutcome out = Hyperspace(w, p, 7, rng);

  EXPECT_TRUE(out.status == Msg::TravelStatus::Arrived);
  EXPECT_FALSE(w.Has<Witchspace>(p));                     // escaped
}

TEST(Hyperspace, KillsInWitchspacePayNoBounty)
{
  ECS::Registry w;
  // A killer player with a wallet, stranded in witchspace.
  const ECS::EntityId killer = w.Create();
  w.Add<WorldTransform>(killer, WorldTransform{ { 0, 0, 0 } });
  w.Add<Wallet>(killer, Wallet{ 1000 });
  w.Add<PlayerTag>(killer, PlayerTag{});
  w.Add<Witchspace>(killer, Witchspace{});
  // A bountied Thargoid victim.
  const ECS::EntityId thargoid = w.Create();
  w.Add<Combatant>(thargoid, Combatant{ Team::Pirate, 1, 4, 5000, true });
  w.Add<Bounty>(thargoid, Bounty{ THARGOID_BOUNTY });

  const KillCredit credit = CreditKill(w, killer.index, thargoid);

  EXPECT_EQ(credit.bounty, 0);                          // bounty withheld in witchspace
  EXPECT_EQ(w.Get<Wallet>(killer).credits, 1000);       // wallet untouched
  EXPECT_EQ(credit.score, 1);                           // but the kill still scores
}

// --- In-system jump ---------------------------------------------------------

TEST(Hyperspace, InSystemJumpHopsTowardThePlanet)
{
  ECS::Registry w;
  const ECS::EntityId p = w.Create();
  w.Add<WorldTransform>(p, WorldTransform{ { 0, 0, 0 } });
  const ECS::EntityId planet = w.Create();
  w.Add<WorldTransform>(planet, WorldTransform{ { 0, 0, 300000 } });
  w.Add<NetType>(planet, NetType{ ShipType::Planet });

  const JumpDriveOutcome out = InSystemJump(w, p);

  EXPECT_TRUE(out.status == Msg::TravelStatus::Jumped);
  EXPECT_TRUE(out.jumped);
  EXPECT_EQ(w.Get<WorldTransform>(p).position.z, IN_SYSTEM_JUMP_MAX);   // shoved down-system
}

TEST(Hyperspace, InSystemJumpIsMassLockedByANearbyShip)
{
  ECS::Registry w;
  const ECS::EntityId p = w.Create();
  w.Add<WorldTransform>(p, WorldTransform{ { 0, 0, 0 } });
  const ECS::EntityId planet = w.Create();
  w.Add<WorldTransform>(planet, WorldTransform{ { 0, 0, 300000 } });
  w.Add<NetType>(planet, NetType{ ShipType::Planet });
  // A hostile within the mass-lock radius.
  const ECS::EntityId foe = w.Create();
  w.Add<WorldTransform>(foe, WorldTransform{ { 0, 0, 50000 } });
  w.Add<Combatant>(foe, Combatant{ Team::Pirate, 80, 3, 5000, true });

  const JumpDriveOutcome out = InSystemJump(w, p);

  EXPECT_TRUE(out.status == Msg::TravelStatus::MassLocked);
  EXPECT_FALSE(out.jumped);
  EXPECT_EQ(w.Get<WorldTransform>(p).position.z, 0);   // didn't move
}

TEST(Hyperspace, InSystemJumpIsMassLockedByAClosePlanet)
{
  ECS::Registry w;
  const ECS::EntityId p = w.Create();
  w.Add<WorldTransform>(p, WorldTransform{ { 0, 0, 0 } });
  const ECS::EntityId planet = w.Create();
  w.Add<WorldTransform>(planet, WorldTransform{ { 0, 0, 50000 } });   // inside mass-lock range
  w.Add<NetType>(planet, NetType{ ShipType::Planet });

  const JumpDriveOutcome out = InSystemJump(w, p);
  EXPECT_TRUE(out.status == Msg::TravelStatus::MassLocked);
  EXPECT_FALSE(out.jumped);
}

TEST(Hyperspace, InSystemJumpIsMassLockedByAStation)
{
  ECS::Registry w;
  const ECS::EntityId p = w.Create();
  w.Add<WorldTransform>(p, WorldTransform{ { 0, 0, 0 } });
  const ECS::EntityId planet = w.Create();
  w.Add<WorldTransform>(planet, WorldTransform{ { 0, 0, 300000 } });
  w.Add<NetType>(planet, NetType{ ShipType::Planet });
  const ECS::EntityId station = w.Create();
  w.Add<WorldTransform>(station, WorldTransform{ { 0, 0, 40000 } });
  w.Add<Combatant>(station, Combatant{ Team::Station, 1000000, 0, 1, false });

  const JumpDriveOutcome out = InSystemJump(w, p);
  EXPECT_TRUE(out.status == Msg::TravelStatus::MassLocked);
}

// --- Refuel -----------------------------------------------------------------

TEST(Hyperspace, RefuelFillsTheTankAndCharges)
{
  Wallet wal{ 1000 };
  Fuel fuel{ 50, 70 };

  const Net::StationStatus st = RefuelPlayer(wal, fuel, /*docked*/ true);

  EXPECT_TRUE(st == Net::StationStatus::Ok);
  EXPECT_EQ(fuel.tenths, 70);
  EXPECT_EQ(wal.credits, 1000 - 20 * FUEL_PRICE_PER_TENTH);   // 20 tenths bought
}

TEST(Hyperspace, RefuelBuysWhatYouCanAfford)
{
  Wallet wal{ 10 };            // affords 5 tenths at price 2
  Fuel fuel{ 0, 70 };

  const Net::StationStatus st = RefuelPlayer(wal, fuel, true);

  EXPECT_TRUE(st == Net::StationStatus::Ok);
  EXPECT_EQ(fuel.tenths, 5);
  EXPECT_EQ(wal.credits, 0);
}

TEST(Hyperspace, RefuelWhenBrokeIsRejected)
{
  Wallet wal{ 1 };            // can't afford even one tenth (price 2)
  Fuel fuel{ 0, 70 };
  EXPECT_TRUE(RefuelPlayer(wal, fuel, true) == Net::StationStatus::NotEnoughCredits);
  EXPECT_EQ(fuel.tenths, 0);
}

TEST(Hyperspace, RefuelAFullTankIsANoOp)
{
  Wallet wal{ 1000 };
  Fuel fuel{ 70, 70 };
  EXPECT_TRUE(RefuelPlayer(wal, fuel, true) == Net::StationStatus::Ok);
  EXPECT_EQ(wal.credits, 1000);
}

TEST(Hyperspace, RefuelRequiresDocking)
{
  Wallet wal{ 1000 };
  Fuel fuel{ 0, 70 };
  EXPECT_TRUE(RefuelPlayer(wal, fuel, /*docked*/ false) == Net::StationStatus::NotDocked);
  EXPECT_EQ(fuel.tenths, 0);
}

// --- Thargoid spawn ---------------------------------------------------------

TEST(Hyperspace, SpawnThargoidsAreHostileBountiedIntentFlyers)
{
  ECS::Registry w;
  uint32_t rng = 42u;
  const std::vector<ECS::EntityId> thg = SpawnThargoids(w, { 0, 0, 0 }, 3, rng);

  ASSERT_EQ(thg.size(), 3u);
  for (const ECS::EntityId e : thg)
  {
    EXPECT_EQ(w.Get<NetType>(e).type, ShipType::Thargoid);
    EXPECT_EQ(w.Get<Combatant>(e).team, Team::Pirate);
    EXPECT_TRUE(w.Get<Combatant>(e).autoEngage);
    EXPECT_EQ(w.Get<Bounty>(e).value, THARGOID_BOUNTY);
    EXPECT_TRUE(w.Has<AiPilot>(e));
    EXPECT_TRUE(w.Has<FlightIntent>(e));
  }
}
