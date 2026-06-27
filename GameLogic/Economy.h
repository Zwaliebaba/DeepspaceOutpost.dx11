#pragma once

// Economy - the authoritative stock market (GameLogic, A4).
//
// A faithful port of the legacy generate_stock_market(): commodity prices and
// quantities are a deterministic function of a planet's economy and a per-system
// market seed. In the MMO the server owns this (shared economy), so it is pure,
// seedable, and free of any global state - GenerateMarket() fills a caller-owned
// array instead of mutating a global table, so each planet/system has its own
// market.

namespace Neuron::GameLogic
{
  inline constexpr int COMMODITY_COUNT = 17;
  inline constexpr int ALIEN_ITEMS_INDEX = 16;   // never available to buy

  // Static per-commodity market data (base_price, eco_adjust, base_quantity,
  // mask), transcribed from the legacy stock_market[] table.
  struct Commodity
  {
    int basePrice;
    int ecoAdjust;
    int baseQuantity;
    int mask;
  };

  inline constexpr Commodity COMMODITIES[COMMODITY_COUNT] =
  {
    {  19, -2,   6, 0x01 },   // Food
    {  20, -1,  10, 0x03 },   // Textiles
    {  65, -3,   2, 0x07 },   // Radioactives
    {  40, -5, 226, 0x1F },   // Slaves
    {  83, -5, 251, 0x0F },   // Liquor/Wines
    { 196,  8,  54, 0x03 },   // Luxuries
    { 235, 29,   8, 0x78 },   // Narcotics
    { 154, 14,  56, 0x03 },   // Computers
    { 117,  6,  40, 0x07 },   // Machinery
    {  78,  1,  17, 0x1F },   // Alloys
    { 124, 13,  29, 0x07 },   // Firearms
    { 176, -9, 220, 0x3F },   // Furs
    {  32, -1,  53, 0x03 },   // Minerals
    {  97, -1,  66, 0x07 },   // Gold
    { 171, -2,  55, 0x1F },   // Platinum
    {  45, -1, 250, 0x0F },   // Gem-Stones
    {  53, 15, 192, 0x07 },   // Alien Items
  };

  struct MarketEntry
  {
    int price = 0;        // displayed price (legacy stores base*4)
    int quantity = 0;     // 0..63
  };

  // Fill `_out` with the market for a planet of the given economy (0..7) and
  // market seed. Faithful to generate_stock_market(): 8-bit wrap, the >127 ->
  // 0 quantity clamp, the 0..63 quantity range, and Alien Items never stocked.
  inline void GenerateMarket(int _economy, int _marketSeed, MarketEntry (&_out)[COMMODITY_COUNT])
  {
    for (int i = 0; i < COMMODITY_COUNT; ++i)
    {
      int price = COMMODITIES[i].basePrice;
      price += _marketSeed & COMMODITIES[i].mask;
      price += _economy * COMMODITIES[i].ecoAdjust;
      price &= 255;

      int quant = COMMODITIES[i].baseQuantity;
      quant += _marketSeed & COMMODITIES[i].mask;
      quant -= _economy * COMMODITIES[i].ecoAdjust;
      quant &= 255;

      if (quant > 127)    // would be negative in 8-bit; floor at zero
        quant = 0;
      quant &= 63;        // quantities range 0..63

      _out[i].price = price * 4;
      _out[i].quantity = quant;
    }

    _out[ALIEN_ITEMS_INDEX].quantity = 0;
  }
}
