#pragma once

// GalaxyRows - the one place that maps between the procedural galaxy generator
// (GameLogic) and the durable persistence rows (NeuronServer). Shared by the
// world bootstrap (WorldBuilder, which loads rows to lay out the universe) and
// the seeding tool (tools/dbseed, which writes them). Keeping the conversion in
// exactly one header guarantees the rows the seeder writes are the rows the
// server would otherwise generate - the "seed" becomes a one-time way to fill the
// table, not a second, drifting source of truth.
//
// Header-only; depends on GameLogic (GalaxyGen/Galaxy/Economy) + the plain
// Persist row structs + the wire manifest entry. No sockets, no loop state.

#include <algorithm>
#include <cstdint>
#include <vector>

#include "GalaxyGen.h"                    // GameLogic::GenerateGalaxy / GalaxySystem / GalaxyConfig
#include "Galaxy.h"                       // GameLogic::GeneratePlanet / NamePlanet / BASE_GALAXY_SEED
#include "Economy.h"                      // GameLogic::GenerateMarket / COMMODITY_COUNT / MarketEntry
#include "Messages/Defs/GalaxyChunks.h"   // Net::GalaxySystemInfo / GALAXY_NAME_MAX

#include "PersistenceStore.h"             // Neuron::Persist::SystemRow / MarketRow

namespace DSOServer
{
  // The hand-placed home system's fixed layout (id -1). Mirrors WorldBuilder's
  // original constants so a seeded galaxy and an unseeded (generated) one agree.
  inline constexpr int HOME_SYSTEM_ID = -1;
  inline constexpr int64_t HOME_PLANET_Z = 65536;
  inline constexpr int64_t HOME_STATION_Z = -3000;

  // Convert one generated system to its durable row.
  [[nodiscard]] inline Neuron::Persist::SystemRow SystemRowFrom(const Neuron::GameLogic::GalaxySystem& _s)
  {
    Neuron::Persist::SystemRow r;
    r.systemId     = static_cast<int32_t>(_s.id);
    r.name         = _s.name;
    r.planetX      = _s.planetPos.x;
    r.planetY      = _s.planetPos.y;
    r.planetZ      = _s.planetPos.z;
    r.stationX     = _s.stationPos.x;
    r.stationY     = _s.stationPos.y;
    r.stationZ     = _s.stationPos.z;
    r.economy      = _s.planet.economy;
    r.government   = _s.planet.government;
    r.techLevel    = _s.planet.techLevel;
    r.population   = _s.planet.population;
    r.productivity = _s.planet.productivity;
    r.radius       = _s.planet.radius;
    r.marketSeed   = _s.marketSeed;
    return r;
  }

  // The home system's durable row (id -1). Positions are hand-placed; attributes
  // come from the canonical base seed, exactly as WorldBuilder used to derive them.
  [[nodiscard]] inline Neuron::Persist::SystemRow HomeSystemRow()
  {
    const Neuron::GameLogic::PlanetData home = Neuron::GameLogic::GeneratePlanet(Neuron::GameLogic::BASE_GALAXY_SEED);
    Neuron::Persist::SystemRow r;
    r.systemId     = HOME_SYSTEM_ID;
    r.name         = "HOME";
    r.planetX      = 0;  r.planetY = 0;  r.planetZ = HOME_PLANET_Z;
    r.stationX     = 0;  r.stationY = 0; r.stationZ = HOME_STATION_Z;
    r.economy      = home.economy;
    r.government   = home.government;
    r.techLevel    = home.techLevel;
    r.population   = home.population;
    r.productivity = home.productivity;
    r.radius       = home.radius;
    r.marketSeed   = Neuron::GameLogic::BASE_GALAXY_SEED.f;
    return r;
  }

  // Every system row for a whole galaxy: the procedural systems then the home
  // system last (the order preserves the manifest's historical index layout).
  [[nodiscard]] inline std::vector<Neuron::Persist::SystemRow> BuildSystemRows(const Neuron::GameLogic::GalaxyConfig& _cfg)
  {
    std::vector<Neuron::Persist::SystemRow> rows;
    const std::vector<Neuron::GameLogic::GalaxySystem> systems = Neuron::GameLogic::GenerateGalaxy(_cfg);
    rows.reserve(systems.size() + 1);
    for (const Neuron::GameLogic::GalaxySystem& s : systems)
      rows.push_back(SystemRowFrom(s));
    rows.push_back(HomeSystemRow());
    return rows;
  }

  // The baseline market for a system, as durable rows (stock/price from
  // GenerateMarket). Alien Items (index 16) is never stocked but still gets a row
  // so the market is complete. `_tick` stamps updated_tick.
  [[nodiscard]] inline std::vector<Neuron::Persist::MarketRow> BaselineMarketRows(const Neuron::Persist::SystemRow& _sys, uint64_t _tick)
  {
    Neuron::GameLogic::MarketEntry market[Neuron::GameLogic::COMMODITY_COUNT] = {};
    Neuron::GameLogic::GenerateMarket(_sys.economy, _sys.marketSeed, market);

    std::vector<Neuron::Persist::MarketRow> rows;
    rows.reserve(Neuron::GameLogic::COMMODITY_COUNT);
    for (int c = 0; c < Neuron::GameLogic::COMMODITY_COUNT; ++c)
    {
      Neuron::Persist::MarketRow r;
      r.systemId    = _sys.systemId;
      r.commodity   = c;
      r.stock       = market[c].quantity;
      r.price       = market[c].price;
      r.updatedTick = _tick;
      rows.push_back(r);
    }
    return rows;
  }

  // The chart manifest entry for a system row (what the client plots).
  [[nodiscard]] inline Neuron::Net::GalaxySystemInfo ManifestEntryFrom(const Neuron::Persist::SystemRow& _sys)
  {
    Neuron::Net::GalaxySystemInfo e;
    e.id = static_cast<uint32_t>(_sys.systemId);
    e.x = _sys.planetX;
    e.y = _sys.planetY;
    e.z = _sys.planetZ;
    const std::size_t n = std::min(_sys.name.size(), Neuron::Net::GALAXY_NAME_MAX - 1);
    for (std::size_t i = 0; i < n; ++i)
      e.name[i] = _sys.name[i];
    e.government   = static_cast<uint8_t>(_sys.government);
    e.economy      = static_cast<uint8_t>(_sys.economy);
    e.techLevel    = static_cast<uint8_t>(_sys.techLevel);
    e.population   = static_cast<uint16_t>(_sys.population);
    e.productivity = static_cast<uint16_t>(_sys.productivity);
    return e;
  }
}
