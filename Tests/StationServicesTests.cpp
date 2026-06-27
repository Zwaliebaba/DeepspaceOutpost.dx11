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
  GameLogic::MarketEntry market[GameLogic::COMMODITY_COUNT] = {};

  ECS::Registry w1;
  ECS::EntityId near = SpawnTrader(w1, /*x*/ 100, 1000);
  Net::StationRequest dock;
  dock.kind = Net::StationRequestKind::Dock;
  dock.stationId = 9;
  Net::StationResponse r1 = GameLogic::ProcessStationRequest(w1, near, market, Math::Vector3i64{ 0, 0, 0 }, 5000, dock);
  CHECK(r1.status == Net::StationStatus::Ok);
  CHECK(w1.Get<GameLogic::DockState>(near).docked);
  CHECK(w1.Get<GameLogic::DockState>(near).stationId == 9);

  ECS::Registry w2;
  ECS::EntityId farAway = SpawnTrader(w2, /*x*/ 100000, 1000);
  Net::StationResponse r2 = GameLogic::ProcessStationRequest(w2, farAway, market, Math::Vector3i64{ 0, 0, 0 }, 5000, dock);
  CHECK(r2.status == Net::StationStatus::CantDock);
  CHECK(!w2.Get<GameLogic::DockState>(farAway).docked);
}

TEST(Station_ProcessBuyNeedsDockThenSucceeds)
{
  ECS::Registry w;
  ECS::EntityId p = SpawnTrader(w, 0, /*credits*/ 1000);
  GameLogic::MarketEntry market[GameLogic::COMMODITY_COUNT] = {};
  SetItem(market, 0, /*price*/ 10, /*qty*/ 50);

  Net::StationRequest buy;
  buy.kind = Net::StationRequestKind::Buy;
  buy.commodity = 0;
  buy.quantity = 4;

  // Not docked yet.
  Net::StationResponse fail = GameLogic::ProcessStationRequest(w, p, market, Math::Vector3i64{ 0, 0, 0 }, 5000, buy);
  CHECK(fail.status == Net::StationStatus::NotDocked);

  // Dock, then buy.
  Net::StationRequest dock;
  dock.kind = Net::StationRequestKind::Dock;
  (void)GameLogic::ProcessStationRequest(w, p, market, Math::Vector3i64{ 0, 0, 0 }, 5000, dock);

  Net::StationResponse ok = GameLogic::ProcessStationRequest(w, p, market, Math::Vector3i64{ 0, 0, 0 }, 5000, buy);
  CHECK(ok.status == Net::StationStatus::Ok);
  CHECK(ok.credits == 960);
  CHECK(ok.cargo == 4);
  CHECK(w.Get<GameLogic::Wallet>(p).credits == 960);
}

TEST(Station_ProcessUndock)
{
  ECS::Registry w;
  ECS::EntityId p = SpawnTrader(w, 0, 1000);
  GameLogic::MarketEntry market[GameLogic::COMMODITY_COUNT] = {};

  Net::StationRequest dock;
  dock.kind = Net::StationRequestKind::Dock;
  (void)GameLogic::ProcessStationRequest(w, p, market, Math::Vector3i64{ 0, 0, 0 }, 5000, dock);
  CHECK(w.Get<GameLogic::DockState>(p).docked);

  Net::StationRequest undock;
  undock.kind = Net::StationRequestKind::Undock;
  Net::StationResponse r = GameLogic::ProcessStationRequest(w, p, market, Math::Vector3i64{ 0, 0, 0 }, 5000, undock);
  CHECK(r.status == Net::StationStatus::Ok);
  CHECK(!w.Get<GameLogic::DockState>(p).docked);
}
