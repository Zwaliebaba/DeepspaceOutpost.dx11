#include "TestFramework.h"

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

TEST(Station_BuyMovesCreditsCargoAndStock)
{
  GameLogic::Wallet wallet{ 1000 };
  GameLogic::CargoHold hold;
  GameLogic::MarketEntry market[GameLogic::COMMODITY_COUNT] = {};
  SetItem(market, /*food*/ 0, /*price*/ 10, /*qty*/ 50);

  GameLogic::TradeResult r = GameLogic::BuyCommodity(wallet, hold, market, /*docked*/ true, 0, /*qty*/ 5);

  CHECK(r.status == Net::StationStatus::Ok);
  CHECK(wallet.credits == 950);          // 1000 - 5*10
  CHECK(hold.units[0] == 5);
  CHECK(market[0].quantity == 45);
  CHECK(r.credits == 950);
  CHECK(r.cargo == 5);
}

TEST(Station_BuyRequiresDocking)
{
  GameLogic::Wallet wallet{ 1000 };
  GameLogic::CargoHold hold;
  GameLogic::MarketEntry market[GameLogic::COMMODITY_COUNT] = {};
  SetItem(market, 0, 10, 50);

  GameLogic::TradeResult r = GameLogic::BuyCommodity(wallet, hold, market, /*docked*/ false, 0, 5);
  CHECK(r.status == Net::StationStatus::NotDocked);
  CHECK(wallet.credits == 1000);         // unchanged
}

TEST(Station_BuyRejectsInsufficientStockAndCredits)
{
  GameLogic::MarketEntry market[GameLogic::COMMODITY_COUNT] = {};
  SetItem(market, 0, 10, 3);

  {
    GameLogic::Wallet w{ 1000 };
    GameLogic::CargoHold h;
    GameLogic::TradeResult r = GameLogic::BuyCommodity(w, h, market, true, 0, /*qty*/ 5);  // only 3 in stock
    CHECK(r.status == Net::StationStatus::NoStock);
  }
  {
    GameLogic::Wallet w{ 20 };           // can only afford 2
    GameLogic::CargoHold h;
    GameLogic::TradeResult r = GameLogic::BuyCommodity(w, h, market, true, 0, 3);
    CHECK(r.status == Net::StationStatus::NotEnoughCredits);
    CHECK(w.credits == 20);
  }
}

TEST(Station_BuyRespectsHoldCapacityForTonnageGoods)
{
  GameLogic::Wallet wallet{ 100000 };
  GameLogic::CargoHold hold;
  hold.capacity = 10;
  GameLogic::MarketEntry market[GameLogic::COMMODITY_COUNT] = {};
  SetItem(market, /*food, tonnage*/ 0, 1, 100);

  GameLogic::TradeResult r = GameLogic::BuyCommodity(wallet, hold, market, true, 0, /*qty*/ 11);
  CHECK(r.status == Net::StationStatus::HoldFull);
  CHECK(hold.units[0] == 0);
}

TEST(Station_NonTonnageGoodsIgnoreHoldCapacity)
{
  GameLogic::Wallet wallet{ 100000 };
  GameLogic::CargoHold hold;
  hold.capacity = 1;
  GameLogic::MarketEntry market[GameLogic::COMMODITY_COUNT] = {};
  SetItem(market, /*Gold = 13, not tonnage*/ 13, 1, 100);

  GameLogic::TradeResult r = GameLogic::BuyCommodity(wallet, hold, market, true, 13, /*qty*/ 50);
  CHECK(r.status == Net::StationStatus::Ok);    // gold doesn't fill the tonnage hold
  CHECK(hold.units[13] == 50);
  CHECK(GameLogic::TotalTonnage(hold) == 0);
}

TEST(Station_SellMovesCreditsCargoAndStock)
{
  GameLogic::Wallet wallet{ 0 };
  GameLogic::CargoHold hold;
  hold.units[0] = 8;
  GameLogic::MarketEntry market[GameLogic::COMMODITY_COUNT] = {};
  SetItem(market, 0, /*price*/ 7, /*qty*/ 0);

  GameLogic::TradeResult r = GameLogic::SellCommodity(wallet, hold, market, true, 0, /*qty*/ 3);
  CHECK(r.status == Net::StationStatus::Ok);
  CHECK(wallet.credits == 21);           // 3 * 7
  CHECK(hold.units[0] == 5);
  CHECK(market[0].quantity == 3);
}

TEST(Station_SellRejectsMoreThanHeld)
{
  GameLogic::Wallet wallet{ 0 };
  GameLogic::CargoHold hold;
  hold.units[0] = 2;
  GameLogic::MarketEntry market[GameLogic::COMMODITY_COUNT] = {};
  SetItem(market, 0, 7, 0);

  GameLogic::TradeResult r = GameLogic::SellCommodity(wallet, hold, market, true, 0, /*qty*/ 5);
  CHECK(r.status == Net::StationStatus::NoCargo);
  CHECK(hold.units[0] == 2);
  CHECK(wallet.credits == 0);
}

TEST(Station_CanDockOnlyWithinRange)
{
  CHECK(GameLogic::CanDock(Math::Vector3i64{ 0, 0, 0 }, Math::Vector3i64{ 100, 0, 0 }, /*range*/ 200));
  CHECK(!GameLogic::CanDock(Math::Vector3i64{ 0, 0, 0 }, Math::Vector3i64{ 500, 0, 0 }, 200));
}

TEST(Station_ProcessDockSucceedsInRangeFailsOutOfRange)
{
  ECS::Registry w1;
  ECS::EntityId stn = SpawnStation(w1, /*x*/ 0, 10, 50);
  ECS::EntityId near = SpawnTrader(w1, /*x*/ 100, 1000);
  Net::StationRequest dock;
  dock.kind = Net::StationRequestKind::Dock;
  Net::StationResponse r1 = GameLogic::ProcessStationRequest(w1, near, 5000, dock);
  CHECK(r1.status == Net::StationStatus::Ok);
  CHECK(w1.Get<GameLogic::DockState>(near).docked);
  CHECK(w1.Get<GameLogic::DockState>(near).stationId == stn.index);   // attached to the nearest station

  ECS::Registry w2;
  SpawnStation(w2, 0, 10, 50);
  ECS::EntityId farAway = SpawnTrader(w2, /*x*/ 100000, 1000);
  Net::StationResponse r2 = GameLogic::ProcessStationRequest(w2, farAway, 5000, dock);
  CHECK(r2.status == Net::StationStatus::CantDock);
  CHECK(!w2.Get<GameLogic::DockState>(farAway).docked);
}

TEST(Station_DockAttachesToTheNearestStation)
{
  ECS::Registry w;
  ECS::EntityId nearStn = SpawnStation(w, /*x*/ 100, 5, 5);
  SpawnStation(w, /*x*/ 4000, 5, 5);            // farther, also in range
  ECS::EntityId p = SpawnTrader(w, 0, 1000);

  Net::StationRequest dock;
  dock.kind = Net::StationRequestKind::Dock;
  Net::StationResponse r = GameLogic::ProcessStationRequest(w, p, 5000, dock);

  CHECK(r.status == Net::StationStatus::Ok);
  CHECK(w.Get<GameLogic::DockState>(p).stationId == nearStn.index);   // the closer one
}

TEST(Station_ProcessBuyNeedsDockThenSucceeds)
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
  CHECK(fail.status == Net::StationStatus::NotDocked);

  // Dock, then buy against the station's own market.
  Net::StationRequest dock;
  dock.kind = Net::StationRequestKind::Dock;
  (void)GameLogic::ProcessStationRequest(w, p, 5000, dock);

  Net::StationResponse ok = GameLogic::ProcessStationRequest(w, p, 5000, buy);
  CHECK(ok.status == Net::StationStatus::Ok);
  CHECK(ok.credits == 960);
  CHECK(ok.cargo == 4);
  CHECK(w.Get<GameLogic::Wallet>(p).credits == 960);
}

TEST(Station_EquipGrantsItemAndChargesCredits)
{
  GameLogic::Wallet wallet{ 20000 };
  GameLogic::Equipment eq;
  GameLogic::CargoHold hold;   // capacity 20

  GameLogic::EquipResult r = GameLogic::EquipPlayer(wallet, eq, hold, Net::EquipItem::LargeCargoBay);
  CHECK(r.status == Net::StationStatus::Ok);
  CHECK(wallet.credits == 16000);     // 20000 - 4000
  CHECK(eq.largeCargoBay);
  CHECK(hold.capacity == 35);          // 20 + 15
}

TEST(Station_EquipRejectsDuplicateAndBroke)
{
  GameLogic::CargoHold hold;
  {
    GameLogic::Wallet w{ 20000 };
    GameLogic::Equipment eq;
    eq.ecm = true;                     // already owned
    GameLogic::EquipResult r = GameLogic::EquipPlayer(w, eq, hold, Net::EquipItem::Ecm);
    CHECK(r.status == Net::StationStatus::AlreadyOwned);
    CHECK(w.credits == 20000);
  }
  {
    GameLogic::Wallet w{ 100 };        // can't afford a 6000 ECM
    GameLogic::Equipment eq;
    GameLogic::EquipResult r = GameLogic::EquipPlayer(w, eq, hold, Net::EquipItem::Ecm);
    CHECK(r.status == Net::StationStatus::NotEnoughCredits);
    CHECK(!eq.ecm);
  }
}

TEST(Station_MissilesIncrementAndCapAtFour)
{
  GameLogic::Wallet wallet{ 100000 };
  GameLogic::Equipment eq;             // starts with 3
  GameLogic::CargoHold hold;

  GameLogic::EquipResult r1 = GameLogic::EquipPlayer(wallet, eq, hold, Net::EquipItem::Missile);
  CHECK(r1.status == Net::StationStatus::Ok);
  CHECK(eq.missiles == 4);

  GameLogic::EquipResult r2 = GameLogic::EquipPlayer(wallet, eq, hold, Net::EquipItem::Missile);
  CHECK(r2.status == Net::StationStatus::AlreadyOwned);   // capped at 4
  CHECK(eq.missiles == 4);
}

TEST(Station_ProcessEquipNeedsDocking)
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
  CHECK(fail.status == Net::StationStatus::NotDocked);

  // Dock, then equip.
  Net::StationRequest dock;
  dock.kind = Net::StationRequestKind::Dock;
  (void)GameLogic::ProcessStationRequest(w, p, 5000, dock);

  Net::StationResponse ok = GameLogic::ProcessStationRequest(w, p, 5000, equip);
  CHECK(ok.status == Net::StationStatus::Ok);
  CHECK(ok.credits == 14750);          // 20000 - 5250
  CHECK(w.Get<GameLogic::Equipment>(p).fuelScoop);
}

TEST(Station_TeleportJumpsToTheDestinationStation)
{
  ECS::Registry w;
  ECS::EntityId s0 = SpawnSystemStation(w, /*system*/ 0, 0, 0, 0);
  ECS::EntityId s1 = SpawnSystemStation(w, /*system*/ 1, 5'000'000, 0, 0);
  ECS::EntityId p = SpawnTrader(w, 0, 1000);

  Net::StationRequest dock;
  dock.kind = Net::StationRequestKind::Dock;
  (void)GameLogic::ProcessStationRequest(w, p, 5000, dock);
  CHECK(w.Get<GameLogic::DockState>(p).docked);
  CHECK(w.Get<GameLogic::DockState>(p).stationId == s0.index);

  Net::StationRequest tp;
  tp.kind = Net::StationRequestKind::Teleport;
  tp.stationId = 1;   // target system id
  Net::StationResponse r = GameLogic::ProcessStationRequest(w, p, 5000, tp);

  CHECK(r.status == Net::StationStatus::Ok);
  CHECK((w.Get<GameLogic::WorldTransform>(p).position == Math::Vector3i64{ 5'000'000, 0, 0 }));
  CHECK(w.Get<GameLogic::DockState>(p).stationId == s1.index);   // now docked at the destination
}

TEST(Station_TeleportRequiresBeingDocked)
{
  ECS::Registry w;
  SpawnSystemStation(w, 0, 0, 0, 0);
  SpawnSystemStation(w, 1, 5'000'000, 0, 0);
  ECS::EntityId p = SpawnTrader(w, 0, 1000);   // not docked

  Net::StationRequest tp;
  tp.kind = Net::StationRequestKind::Teleport;
  tp.stationId = 1;
  Net::StationResponse r = GameLogic::ProcessStationRequest(w, p, 5000, tp);
  CHECK(r.status == Net::StationStatus::NotDocked);
  CHECK((w.Get<GameLogic::WorldTransform>(p).position == Math::Vector3i64{ 0, 0, 0 }));   // did not move
}

TEST(Station_TeleportToUnknownSystemFails)
{
  ECS::Registry w;
  SpawnSystemStation(w, 0, 0, 0, 0);
  ECS::EntityId p = SpawnTrader(w, 0, 1000);

  Net::StationRequest dock;
  dock.kind = Net::StationRequestKind::Dock;
  (void)GameLogic::ProcessStationRequest(w, p, 5000, dock);

  Net::StationRequest tp;
  tp.kind = Net::StationRequestKind::Teleport;
  tp.stationId = 999;   // no such system
  Net::StationResponse r = GameLogic::ProcessStationRequest(w, p, 5000, tp);
  CHECK(r.status == Net::StationStatus::CantDock);
}

TEST(Station_ProcessUndock)
{
  ECS::Registry w;
  ECS::EntityId stn = SpawnStation(w, 0, 0, 0);
  ECS::EntityId p = SpawnTrader(w, 0, 1000);

  Net::StationRequest dock;
  dock.kind = Net::StationRequestKind::Dock;
  (void)GameLogic::ProcessStationRequest(w, p, 5000, dock);
  CHECK(w.Get<GameLogic::DockState>(p).docked);

  Net::StationRequest undock;
  undock.kind = Net::StationRequestKind::Undock;
  Net::StationResponse r = GameLogic::ProcessStationRequest(w, p, 5000, undock);
  CHECK(r.status == Net::StationStatus::Ok);
  CHECK(!w.Get<GameLogic::DockState>(p).docked);

  // Ejected a fixed distance in front of the station (so a forward burn leaves
  // rather than instantly re-docking).
  const Math::Vector3i64 station = w.Get<GameLogic::WorldTransform>(stn).position;
  CHECK((w.Get<GameLogic::WorldTransform>(p).position ==
         station + Math::Vector3i64{ 0, 0, GameLogic::LAUNCH_OFFSET }));
}

TEST(Station_UndockResetsFlightToFaceOutward)
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
  CHECK((f.nose.x == 0.0 && f.nose.y == 0.0 && f.nose.z == 1.0));   // pointing outward (+z)
  CHECK(f.speed == 0.0);
  CHECK(f.roll == 0.0);

  // Outward nose + ejected ahead of the station => station is behind the player,
  // so the forgiving "station ahead" dock check cannot re-trigger on launch.
  const Math::Vector3i64 player = w.Get<GameLogic::WorldTransform>(p).position;
  const Math::Vector3i64 station = w.Get<GameLogic::WorldTransform>(stn).position;
  CHECK(station.z < player.z);   // station is behind (smaller z) the outward-facing player
}
