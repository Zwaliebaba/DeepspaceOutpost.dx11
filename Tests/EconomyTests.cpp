#include "TestFramework.h"

#include "Economy.h"

using namespace Neuron::GameLogic;

namespace
{
  // Commodity indices (order of COMMODITIES / the legacy stock_market table).
  constexpr int FOOD = 0;
  constexpr int SLAVES = 3;
  constexpr int FURS = 11;
}

TEST(Economy_BaselineEconomyZeroSeedZero)
{
  MarketEntry m[COMMODITY_COUNT];
  GenerateMarket(/*economy*/ 0, /*seed*/ 0, m);

  // Food: price = (19 + 0 + 0) & 255 = 19; *4 = 76. quant = 6.
  CHECK(m[FOOD].price == 76);
  CHECK(m[FOOD].quantity == 6);

  // Furs: base_quantity 220 -> >127 -> clamped to 0. price = 176*4 = 704.
  CHECK(m[FURS].price == 704);
  CHECK(m[FURS].quantity == 0);

  // Alien Items are never stocked.
  CHECK(m[ALIEN_ITEMS_INDEX].quantity == 0);
}

TEST(Economy_RicherEconomyShiftsFoodPriceAndStock)
{
  MarketEntry m[COMMODITY_COUNT];
  GenerateMarket(/*economy*/ 7, /*seed*/ 0, m);

  // Food eco_adjust -2: price = (19 + 0 + 7*-2) & 255 = 5; *4 = 20.
  //                     quant = (6 - 7*-2) = 20.
  CHECK(m[FOOD].price == 20);
  CHECK(m[FOOD].quantity == 20);
}

TEST(Economy_SeedAddsMaskedRandomness)
{
  MarketEntry m[COMMODITY_COUNT];
  GenerateMarket(/*economy*/ 0, /*seed*/ 255, m);

  // Food mask 0x01: price = (19 + 1) * 4 = 80; quant = 6 + 1 = 7.
  CHECK(m[FOOD].price == 80);
  CHECK(m[FOOD].quantity == 7);

  // Slaves mask 0x1F=31: price = (40 + 31) * 4 = 284.
  //   quant = (226 + 31) & 255 = 1  (not >127) -> 1.
  CHECK(m[SLAVES].price == 284);
  CHECK(m[SLAVES].quantity == 1);
}

TEST(Economy_DeterministicForSameInputs)
{
  MarketEntry a[COMMODITY_COUNT];
  MarketEntry b[COMMODITY_COUNT];
  GenerateMarket(4, 12345, a);
  GenerateMarket(4, 12345, b);
  for (int i = 0; i < COMMODITY_COUNT; ++i)
  {
    CHECK(a[i].price == b[i].price);
    CHECK(a[i].quantity == b[i].quantity);
  }
}
