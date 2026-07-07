#include <gtest/gtest.h>

#include "GameLogic.h"
#include "StationProtocol.h"

using namespace Neuron;

namespace
{
  // A market with one stocked commodity at a known price (others empty).
  void SetItem(GameLogic::MarketEntry (&_m)[GameLogic::COMMODITY_COUNT], int _i, int _price, int _qty)
  {
    _m[_i].price = _price;
    _m[_i].quantity = _qty;
  }

  ECS::EntityId SpawnTrader(ECS::Registry& _w, int64_t _x, int _credits)
  {
    ECS::EntityId e = _w.Create();
    _w.Add<GameLogic::WorldTransform>(e, GameLogic::WorldTransform{ { _x, 0, 0 } });
    _w.Add<GameLogic::Wallet>(e, GameLogic::Wallet{ _credits });
    _w.Add<GameLogic::CargoHold>(e, GameLogic::CargoHold{});
    _w.Add<GameLogic::DockState>(e, GameLogic::DockState{});
    return e;
  }

  // A station entity with a market (food at index 0).
  ECS::EntityId SpawnStation(ECS::Registry& _w, int64_t _x, int _foodPrice, int _foodQty)
  {
    ECS::EntityId e = _w.Create();
    _w.Add<GameLogic::WorldTransform>(e, GameLogic::WorldTransform{ { _x, 0, 0 } });
    GameLogic::ServerStation st;
    st.market[0].price = _foodPrice;
    st.market[0].quantity = _foodQty;
    _w.Add<GameLogic::ServerStation>(e, st);
    return e;
  }

  // A station belonging to a galaxy system at an arbitrary position.
  ECS::EntityId SpawnSystemStation(ECS::Registry& _w, int _systemId, int64_t _x, int64_t _y, int64_t _z)
  {
    ECS::EntityId e = _w.Create();
    _w.Add<GameLogic::WorldTransform>(e, GameLogic::WorldTransform{ { _x, _y, _z } });
    GameLogic::ServerStation st;
    st.systemId = _systemId;
    _w.Add<GameLogic::ServerStation>(e, st);
    return e;
  }
}

TEST(Station, BuyMovesCreditsCargoAndStock)
{
  GameLogic::Wallet wallet{ 1000 };
  GameLogic::CargoHold hold;
  GameLogic::MarketEntry market[GameLogic::COMMODITY_COUNT] = {};
  SetItem(market, /*food*/ 0, /*price*/ 10, /*qty*/ 50);

  GameLogic::TradeResult r = GameLogic::BuyCommodity(wallet, hold, market, /*docked*/ true, 0, /*qty*/ 5);

  EXPECT_TRUE(r.status == Net::StationStatus::Ok);
  EXPECT_TRUE(wallet.credits == 950);          // 1000 - 5*10
  EXPECT_TRUE(hold.units[0] == 5);
  EXPECT_TRUE(market[0].quantity == 45);
  EXPECT_TRUE(r.credits == 950);
  EXPECT_TRUE(r.cargo == 5);
}

TEST(Station, BuyRequiresDocking)
{
  GameLogic::Wallet wallet{ 1000 };
  GameLogic::CargoHold hold;
  GameLogic::MarketEntry market[GameLogic::COMMODITY_COUNT] = {};
  SetItem(market, 0, 10, 50);

  GameLogic::TradeResult r = GameLogic::BuyCommodity(wallet, hold, market, /*docked*/ false, 0, 5);
  EXPECT_TRUE(r.status == Net::StationStatus::NotDocked);
  EXPECT_TRUE(wallet.credits == 1000);         // unchanged
}

TEST(Station, BuyRejectsInsufficientStockAndCredits)
{
  GameLogic::MarketEntry market[GameLogic::COMMODITY_COUNT] = {};
  SetItem(market, 0, 10, 3);

  {
    GameLogic::Wallet w{ 1000 };
    GameLogic::CargoHold h;
    GameLogic::TradeResult r = GameLogic::BuyCommodity(w, h, market, true, 0, /*qty*/ 5);  // only 3 in stock
    EXPECT_TRUE(r.status == Net::StationStatus::NoStock);
  }
  {
    GameLogic::Wallet w{ 20 };           // can only afford 2
    GameLogic::CargoHold h;
    GameLogic::TradeResult r = GameLogic::BuyCommodity(w, h, market, true, 0, 3);
    EXPECT_TRUE(r.status == Net::StationStatus::NotEnoughCredits);
    EXPECT_TRUE(w.credits == 20);
  }
}

TEST(Station, BuyRespectsHoldCapacityForTonnageGoods)
{
  GameLogic::Wallet wallet{ 100000 };
  GameLogic::CargoHold hold;
  hold.capacity = 10;
  GameLogic::MarketEntry market[GameLogic::COMMODITY_COUNT] = {};
  SetItem(market, /*food, tonnage*/ 0, 1, 100);

  GameLogic::TradeResult r = GameLogic::BuyCommodity(wallet, hold, market, true, 0, /*qty*/ 11);
  EXPECT_TRUE(r.status == Net::StationStatus::HoldFull);
  EXPECT_TRUE(hold.units[0] == 0);
}

TEST(Station, NonTonnageGoodsIgnoreHoldCapacity)
{
  GameLogic::Wallet wallet{ 100000 };
  GameLogic::CargoHold hold;
  hold.capacity = 1;
  GameLogic::MarketEntry market[GameLogic::COMMODITY_COUNT] = {};
  SetItem(market, /*Gold = 13, not tonnage*/ 13, 1, 100);

  GameLogic::TradeResult r = GameLogic::BuyCommodity(wallet, hold, market, true, 13, /*qty*/ 50);
  EXPECT_TRUE(r.status == Net::StationStatus::Ok);    // gold doesn't fill the tonnage hold
  EXPECT_TRUE(hold.units[13] == 50);
  EXPECT_TRUE(GameLogic::TotalTonnage(hold) == 0);
}

TEST(Station, SellMovesCreditsCargoAndStock)
{
  GameLogic::Wallet wallet{ 0 };
  GameLogic::CargoHold hold;
  hold.units[0] = 8;
  GameLogic::MarketEntry market[GameLogic::COMMODITY_COUNT] = {};
  SetItem(market, 0, /*price*/ 7, /*qty*/ 0);

  GameLogic::TradeResult r = GameLogic::SellCommodity(wallet, hold, market, true, 0, /*qty*/ 3);
  EXPECT_TRUE(r.status == Net::StationStatus::Ok);
  EXPECT_TRUE(wallet.credits == 21);           // 3 * 7
  EXPECT_TRUE(hold.units[0] == 5);
  EXPECT_TRUE(market[0].quantity == 3);
}

TEST(Station, SellRejectsMoreThanHeld)
{
  GameLogic::Wallet wallet{ 0 };
  GameLogic::CargoHold hold;
  hold.units[0] = 2;
  GameLogic::MarketEntry market[GameLogic::COMMODITY_COUNT] = {};
  SetItem(market, 0, 7, 0);

  GameLogic::TradeResult r = GameLogic::SellCommodity(wallet, hold, market, true, 0, /*qty*/ 5);
  EXPECT_TRUE(r.status == Net::StationStatus::NoCargo);
  EXPECT_TRUE(hold.units[0] == 2);
  EXPECT_TRUE(wallet.credits == 0);
}

TEST(Station, CanDockOnlyWithinRange)
{
  EXPECT_TRUE(GameLogic::CanDock(Math::Vector3i64{ 0, 0, 0 }, Math::Vector3i64{ 100, 0, 0 }, /*range*/ 200));
  EXPECT_TRUE(!GameLogic::CanDock(Math::Vector3i64{ 0, 0, 0 }, Math::Vector3i64{ 500, 0, 0 }, 200));
}

TEST(Station, ProcessDockSucceedsInRangeFailsOutOfRange)
{
  ECS::Registry w1;
  ECS::EntityId stn = SpawnStation(w1, /*x*/ 0, 10, 50);
  ECS::EntityId near = SpawnTrader(w1, /*x*/ 100, 1000);
  Net::StationRequest dock;
  dock.kind = Net::StationRequestKind::Dock;
  Net::StationResponse r1 = GameLogic::ProcessStationRequest(w1, near, 5000, dock);
  EXPECT_TRUE(r1.status == Net::StationStatus::Ok);
  EXPECT_TRUE(w1.Get<GameLogic::DockState>(near).docked);
  EXPECT_TRUE(w1.Get<GameLogic::DockState>(near).stationId == stn.index);   // attached to the nearest station

  ECS::Registry w2;
  SpawnStation(w2, 0, 10, 50);
  ECS::EntityId farAway = SpawnTrader(w2, /*x*/ 100000, 1000);
  Net::StationResponse r2 = GameLogic::ProcessStationRequest(w2, farAway, 5000, dock);
  EXPECT_TRUE(r2.status == Net::StationStatus::CantDock);
  EXPECT_TRUE(!w2.Get<GameLogic::DockState>(farAway).docked);
}

TEST(Station, DockAttachesToTheNearestStation)
{
  ECS::Registry w;
  ECS::EntityId nearStn = SpawnStation(w, /*x*/ 100, 5, 5);
  SpawnStation(w, /*x*/ 4000, 5, 5);            // farther, also in range
  ECS::EntityId p = SpawnTrader(w, 0, 1000);

  Net::StationRequest dock;
  dock.kind = Net::StationRequestKind::Dock;
  Net::StationResponse r = GameLogic::ProcessStationRequest(w, p, 5000, dock);

  EXPECT_TRUE(r.status == Net::StationStatus::Ok);
  EXPECT_TRUE(w.Get<GameLogic::DockState>(p).stationId == nearStn.index);   // the closer one
}

TEST(Station, StationRefusesToDockAFugitive)
{
  ECS::Registry w;
  SpawnStation(w, /*x*/ 100, 5, 5);
  ECS::EntityId p = SpawnTrader(w, 0, 1000);
  w.Add<GameLogic::Wanted>(p, GameLogic::Wanted{ GameLogic::FUGITIVE_THRESHOLD });   // wanted enough to be turned away

  Net::StationRequest dock;
  dock.kind = Net::StationRequestKind::Dock;
  Net::StationResponse r = GameLogic::ProcessStationRequest(w, p, 5000, dock);

  EXPECT_TRUE(r.status == Net::StationStatus::DockingRefused);
  EXPECT_FALSE(w.Get<GameLogic::DockState>(p).docked);

  // Once the record cools below the fugitive threshold, the same station lets them in.
  w.Get<GameLogic::Wanted>(p).level = GameLogic::FUGITIVE_THRESHOLD - 1;
  Net::StationResponse r2 = GameLogic::ProcessStationRequest(w, p, 5000, dock);
  EXPECT_TRUE(r2.status == Net::StationStatus::Ok);
  EXPECT_TRUE(w.Get<GameLogic::DockState>(p).docked);
}

TEST(Station, RespawnAtNearestStationDocksAndDropsCargo)
{
  ECS::Registry w;
  ECS::EntityId nearStn = SpawnStation(w, /*x*/ 1000, 5, 5);
  SpawnStation(w, /*x*/ 500000, 5, 5);               // a farther station
  ECS::EntityId p = SpawnTrader(w, /*x*/ 900, 1000);  // closest to nearStn
  w.Get<GameLogic::CargoHold>(p).units[0] = 7;
  w.Get<GameLogic::CargoHold>(p).capacity = 35;       // e.g. large cargo bay

  EXPECT_TRUE(GameLogic::RespawnAtNearestStation(w, p));

  EXPECT_TRUE(w.Get<GameLogic::DockState>(p).docked);
  EXPECT_TRUE(w.Get<GameLogic::DockState>(p).stationId == nearStn.index);

  const auto& pp = w.Get<GameLogic::WorldTransform>(p).position;
  const auto& sp = w.Get<GameLogic::WorldTransform>(nearStn).position;
  EXPECT_TRUE(pp.x == sp.x && pp.y == sp.y && pp.z == sp.z);   // relocated onto the station

  EXPECT_TRUE(w.Get<GameLogic::CargoHold>(p).units[0] == 0);   // cargo dropped
  EXPECT_TRUE(w.Get<GameLogic::CargoHold>(p).capacity == 35);  // hold size kept
}

TEST(Station, RespawnFailsGracefullyWhenNoStationExists)
{
  ECS::Registry w;
  ECS::EntityId p = SpawnTrader(w, 0, 1000);
  EXPECT_FALSE(GameLogic::RespawnAtNearestStation(w, p));
  EXPECT_FALSE(w.Get<GameLogic::DockState>(p).docked);
}

TEST(Station, ProcessBuyNeedsDockThenSucceeds)
{
  ECS::Registry w;
  SpawnStation(w, /*x*/ 0, /*price*/ 10, /*qty*/ 50);
  ECS::EntityId p = SpawnTrader(w, 0, /*credits*/ 1000);

  Net::StationRequest buy;
  buy.kind = Net::StationRequestKind::Buy;
  buy.commodity = 0;
  buy.quantity = 4;

  // Not docked yet.
  Net::StationResponse fail = GameLogic::ProcessStationRequest(w, p, 5000, buy);
  EXPECT_TRUE(fail.status == Net::StationStatus::NotDocked);

  // Dock, then buy against the station's own market.
  Net::StationRequest dock;
  dock.kind = Net::StationRequestKind::Dock;
  (void)GameLogic::ProcessStationRequest(w, p, 5000, dock);

  Net::StationResponse ok = GameLogic::ProcessStationRequest(w, p, 5000, buy);
  EXPECT_TRUE(ok.status == Net::StationStatus::Ok);
  EXPECT_TRUE(ok.credits == 960);
  EXPECT_TRUE(ok.cargo == 4);
  EXPECT_TRUE(w.Get<GameLogic::Wallet>(p).credits == 960);
}

TEST(Station, EquipGrantsItemAndChargesCredits)
{
  GameLogic::Wallet wallet{ 20000 };
  GameLogic::Equipment eq;
  GameLogic::CargoHold hold;   // capacity 20

  GameLogic::EquipResult r = GameLogic::EquipPlayer(wallet, eq, hold, Net::EquipItem::LargeCargoBay);
  EXPECT_TRUE(r.status == Net::StationStatus::Ok);
  EXPECT_TRUE(wallet.credits == 16000);     // 20000 - 4000
  EXPECT_TRUE(eq.largeCargoBay);
  EXPECT_TRUE(hold.capacity == 35);          // 20 + 15
}

TEST(Station, EquipRejectsDuplicateAndBroke)
{
  GameLogic::CargoHold hold;
  {
    GameLogic::Wallet w{ 20000 };
    GameLogic::Equipment eq;
    eq.ecm = true;                     // already owned
    GameLogic::EquipResult r = GameLogic::EquipPlayer(w, eq, hold, Net::EquipItem::Ecm);
    EXPECT_TRUE(r.status == Net::StationStatus::AlreadyOwned);
    EXPECT_TRUE(w.credits == 20000);
  }
  {
    GameLogic::Wallet w{ 100 };        // can't afford a 6000 ECM
    GameLogic::Equipment eq;
    GameLogic::EquipResult r = GameLogic::EquipPlayer(w, eq, hold, Net::EquipItem::Ecm);
    EXPECT_TRUE(r.status == Net::StationStatus::NotEnoughCredits);
    EXPECT_TRUE(!eq.ecm);
  }
}

TEST(Station, MissilesIncrementAndCapAtFour)
{
  GameLogic::Wallet wallet{ 100000 };
  GameLogic::Equipment eq;             // starts with 3
  GameLogic::CargoHold hold;

  GameLogic::EquipResult r1 = GameLogic::EquipPlayer(wallet, eq, hold, Net::EquipItem::Missile);
  EXPECT_TRUE(r1.status == Net::StationStatus::Ok);
  EXPECT_TRUE(eq.missiles == 4);

  GameLogic::EquipResult r2 = GameLogic::EquipPlayer(wallet, eq, hold, Net::EquipItem::Missile);
  EXPECT_TRUE(r2.status == Net::StationStatus::AlreadyOwned);   // capped at 4
  EXPECT_TRUE(eq.missiles == 4);
}

TEST(Station, ProcessEquipNeedsDocking)
{
  ECS::Registry w;
  SpawnStation(w, 0, 0, 0);
  ECS::EntityId p = SpawnTrader(w, 0, /*credits*/ 20000);
  w.Add<GameLogic::Equipment>(p, GameLogic::Equipment{});

  Net::StationRequest equip;
  equip.kind = Net::StationRequestKind::Equip;
  equip.commodity = static_cast<uint16_t>(Net::EquipItem::FuelScoop);

  // Not docked -> rejected.
  Net::StationResponse fail = GameLogic::ProcessStationRequest(w, p, 5000, equip);
  EXPECT_TRUE(fail.status == Net::StationStatus::NotDocked);

  // Dock, then equip.
  Net::StationRequest dock;
  dock.kind = Net::StationRequestKind::Dock;
  (void)GameLogic::ProcessStationRequest(w, p, 5000, dock);

  Net::StationResponse ok = GameLogic::ProcessStationRequest(w, p, 5000, equip);
  EXPECT_TRUE(ok.status == Net::StationStatus::Ok);
  EXPECT_TRUE(ok.credits == 14750);          // 20000 - 5250
  EXPECT_TRUE(w.Get<GameLogic::Equipment>(p).fuelScoop);
}

TEST(Station, TravelKindsAreNotStationServices)
{
  // Teleport (the fuel-gated hyperspace jump) and JumpDrive are intercepted by
  // the server loop and routed through HyperspaceSystem; if one ever reaches
  // the station dispatcher it must be rejected, never acted on.
  ECS::Registry w;
  ECS::EntityId s0 = SpawnSystemStation(w, /*system*/ 0, 0, 0, 0);
  SpawnSystemStation(w, /*system*/ 1, 5'000'000, 0, 0);
  ECS::EntityId p = SpawnTrader(w, 0, 1000);

  Net::StationRequest dock;
  dock.kind = Net::StationRequestKind::Dock;
  (void)GameLogic::ProcessStationRequest(w, p, 5000, dock);
  EXPECT_TRUE(w.Get<GameLogic::DockState>(p).docked);

  for (const Net::StationRequestKind kind : { Net::StationRequestKind::Teleport, Net::StationRequestKind::JumpDrive })
  {
    Net::StationRequest tp;
    tp.kind = kind;
    tp.stationId = 1;
    Net::StationResponse r = GameLogic::ProcessStationRequest(w, p, 5000, tp);
    EXPECT_TRUE(r.status == Net::StationStatus::BadCommodity);
    EXPECT_TRUE((w.Get<GameLogic::WorldTransform>(p).position == Math::Vector3i64{ 0, 0, 0 }));   // did not move
    EXPECT_TRUE(w.Get<GameLogic::DockState>(p).stationId == s0.index);                            // still docked here
  }
}

TEST(Station, ProcessUndock)
{
  ECS::Registry w;
  ECS::EntityId stn = SpawnStation(w, 0, 0, 0);
  ECS::EntityId p = SpawnTrader(w, 0, 1000);

  Net::StationRequest dock;
  dock.kind = Net::StationRequestKind::Dock;
  (void)GameLogic::ProcessStationRequest(w, p, 5000, dock);
  EXPECT_TRUE(w.Get<GameLogic::DockState>(p).docked);

  Net::StationRequest undock;
  undock.kind = Net::StationRequestKind::Undock;
  Net::StationResponse r = GameLogic::ProcessStationRequest(w, p, 5000, undock);
  EXPECT_TRUE(r.status == Net::StationStatus::Ok);
  EXPECT_TRUE(!w.Get<GameLogic::DockState>(p).docked);

  // Placed at the station bay and handed a LaunchCruise that flies it out over the
  // launch distance (StepLaunchCruise), rather than teleporting a fixed jump ahead.
  const Math::Vector3i64 station = w.Get<GameLogic::WorldTransform>(stn).position;
  EXPECT_TRUE(w.Get<GameLogic::WorldTransform>(p).position == station);
  ASSERT_TRUE(w.Has<GameLogic::LaunchCruise>(p));
  EXPECT_EQ(w.Get<GameLogic::LaunchCruise>(p).distance, GameLogic::LAUNCH_OFFSET);
}

TEST(Station, UndockResetsFlightToFaceOutward)
{
  ECS::Registry w;
  ECS::EntityId stn = SpawnStation(w, 0, 0, 0);
  ECS::EntityId p = SpawnTrader(w, 0, 1000);
  // A player with a tumbling, mis-oriented flight state at the dock.
  GameLogic::Flight tumbling;
  tumbling.nose = Math::Vector3d{ 0.0, 0.0, -1.0 };   // facing back at the station
  tumbling.roll = 0.3;
  tumbling.speed = 50.0;
  w.Add<GameLogic::Flight>(p, tumbling);

  Net::StationRequest dock;
  dock.kind = Net::StationRequestKind::Dock;
  (void)GameLogic::ProcessStationRequest(w, p, 5000, dock);

  Net::StationRequest undock;
  undock.kind = Net::StationRequestKind::Undock;
  (void)GameLogic::ProcessStationRequest(w, p, 5000, undock);

  const GameLogic::Flight& f = w.Get<GameLogic::Flight>(p);
  EXPECT_TRUE((f.nose.x == 0.0 && f.nose.y == 0.0 && f.nose.z == 1.0));   // pointing outward (+z)
  EXPECT_TRUE(f.speed == 0.0);   // at rest pre-tick; StepLaunchCruise throttles it out
  EXPECT_TRUE(f.roll == 0.0);

  // Starts at the station bay facing outward; the LaunchCruise (+ launch immunity)
  // carries it clear along +z, so a forward launch never instantly re-docks.
  const Math::Vector3i64 player = w.Get<GameLogic::WorldTransform>(p).position;
  const Math::Vector3i64 station = w.Get<GameLogic::WorldTransform>(stn).position;
  EXPECT_TRUE(player == station);
  EXPECT_TRUE(w.Has<GameLogic::LaunchCruise>(p));
}

// --- name-chosen spawn (account creation) -------------------------------------

TEST(Station, DockAtNameChosenSystemPicksAnExistingSystemDeterministically)
{
  ECS::Registry w;
  SpawnSystemStation(w, /*systemId*/ 3, 1000, 0, 0);
  SpawnSystemStation(w, /*systemId*/ 7, 0, 2000, 0);
  SpawnSystemStation(w, /*systemId*/ 9, 0, 0, 3000);

  ECS::EntityId p = SpawnTrader(w, 500, 100);
  ASSERT_TRUE(GameLogic::DockAtNameChosenSystem(w, p, "Jameson"));
  ASSERT_TRUE(w.Get<GameLogic::DockState>(p).docked);

  const uint32_t stationIdx = w.Get<GameLogic::DockState>(p).stationId;
  const int sysId = w.Get<GameLogic::ServerStation>(w.LiveEntity(stationIdx)).systemId;
  EXPECT_TRUE(sysId == 3 || sysId == 7 || sysId == 9);   // an existing system, not a made-up id

  // Deterministic: the same name always lands at the same system (no wall-clock RNG).
  ECS::EntityId p2 = SpawnTrader(w, 600, 100);
  ASSERT_TRUE(GameLogic::DockAtNameChosenSystem(w, p2, "Jameson"));
  const uint32_t stationIdx2 = w.Get<GameLogic::DockState>(p2).stationId;
  EXPECT_EQ(w.Get<GameLogic::ServerStation>(w.LiveEntity(stationIdx2)).systemId, sysId);
}

TEST(Station, DockAtNameChosenSystemIsANoOpWhenNoStationsExist)
{
  ECS::Registry w;
  ECS::EntityId p = SpawnTrader(w, 0, 100);
  EXPECT_FALSE(GameLogic::DockAtNameChosenSystem(w, p, "Anyone"));
  EXPECT_FALSE(w.Get<GameLogic::DockState>(p).docked);
}
