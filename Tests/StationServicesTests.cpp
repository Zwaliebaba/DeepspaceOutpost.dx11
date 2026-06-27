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
