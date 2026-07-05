// WorldBuilder - bootstrap the authoritative world (Server). See WorldBuilder.h.

#include "WorldBuilder.h"

#include <cstdio>
#include <map>
#include <utility>

#include "GameLogic.h"
#include "GalaxyRows.h"   // row <-> generator/manifest/market conversions

using namespace Neuron;

namespace DSOServer
{
  namespace
  {
    // Materialize one system row into the world: a planet entity, a station entity
    // carrying its market (baseline, then any persisted drift overlaid). Appends
    // both to `_setup.landmarks` and the system to the chart manifest.
    void MaterializeSystem(ECS::Registry& _world, WorldSetup& _setup,
                           const Persist::SystemRow& _sys,
                           const std::map<std::pair<int32_t, int32_t>, Persist::MarketRow>& _drift)
    {
      const ECS::EntityId planet = _world.Create();
      _world.Add<GameLogic::WorldTransform>(planet,
          GameLogic::WorldTransform{ { _sys.planetX, _sys.planetY, _sys.planetZ } });
      _world.Add<GameLogic::NetType>(planet, GameLogic::NetType{ GameLogic::ShipType::Planet });
      _setup.landmarks.push_back(planet);

      const ECS::EntityId station = _world.Create();
      _world.Add<GameLogic::WorldTransform>(station,
          GameLogic::WorldTransform{ { _sys.stationX, _sys.stationY, _sys.stationZ } });
      _world.Add<GameLogic::NetType>(station, GameLogic::NetType{ GameLogic::ShipType::Coriolis });
      // A near-indestructible combat target: firing on it is a detectable crime; it
      // never initiates fire (autoEngage = false).
      _world.Add<GameLogic::Combatant>(station, GameLogic::Combatant{ GameLogic::Team::Station, 1000000, 0, 1, false });
      _setup.landmarks.push_back(station);

      // Market: the generated baseline, then overlay any persisted drifted rows so
      // trade state survives a restart.
      GameLogic::ServerStation ss;
      ss.systemId = _sys.systemId;
      GameLogic::GenerateMarket(_sys.economy, _sys.marketSeed, ss.market);
      for (int c = 0; c < GameLogic::COMMODITY_COUNT; ++c)
      {
        const auto it = _drift.find(std::make_pair(_sys.systemId, c));
        if (it != _drift.end())
        {
          ss.market[c].quantity = it->second.stock;
          ss.market[c].price = it->second.price;
        }
      }
      _world.Add<GameLogic::ServerStation>(station, ss);

      _setup.manifest.push_back(ManifestEntryFrom(_sys));
    }
  }

  WorldSetup BuildWorld(ECS::Registry& _world,
                        const std::vector<Persist::SystemRow>& _systems,
                        const std::vector<Persist::MarketRow>& _marketDrift)
  {
    WorldSetup setup;

    // The system rows to lay out: the loaded (seeded) ones, or - when the DB is
    // unseeded / persistence is off - the default galaxy generated from the seed,
    // so the no-persistence world is unchanged.
    constexpr GameLogic::GalaxyConfig GALAXY_CFG{};
    const std::vector<Persist::SystemRow> generated =
        _systems.empty() ? BuildSystemRows(GALAXY_CFG) : std::vector<Persist::SystemRow>{};
    const std::vector<Persist::SystemRow>& systems = _systems.empty() ? generated : _systems;

    // Index the persisted market drift by (system, commodity) for O(1) overlay.
    std::map<std::pair<int32_t, int32_t>, Persist::MarketRow> drift;
    for (const Persist::MarketRow& r : _marketDrift)
      drift[std::make_pair(r.systemId, r.commodity)] = r;

    for (const Persist::SystemRow& sys : systems)
      MaterializeSystem(_world, setup, sys, drift);

    // No hand-placed home system or starter pirate: the universe is a uniform field
    // of systems, and dynamic spawning (SpawnDirector) provides pirates near
    // players. New commanders are placed docked at a name-chosen system (§6.10).

    printf("Galaxy: %zu systems %s.\n", systems.size(),
           _systems.empty() ? "generated (unseeded DB / no persistence)" : "loaded from the store");

    return setup;
  }
}
