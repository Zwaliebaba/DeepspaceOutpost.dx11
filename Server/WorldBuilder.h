#pragma once

// WorldBuilder - bootstrap the authoritative world (Server).
//
// Lays out the universe from durable SYSTEM ROWS (planet + market-carrying
// station per system, home included) and builds the chart manifest shipped to
// every client on connect. The rows come from the persistence store when the DB
// is seeded (the initial-loading mechanism, so ids/positions are stable); with no
// store the same rows are generated from the galaxy seed as a fallback, so the
// no-persistence server is byte-for-byte the old world. Each station's market
// starts at the generated baseline and is overlaid with any persisted drift.
// Pure world-construction - no sockets, no loop state.

#include <vector>

#include "ECS.h"
#include "Messages/Defs/GalaxyChunks.h"   // Net::GalaxySystemInfo
#include "PersistenceStore.h"             // Neuron::Persist::SystemRow / MarketRow

namespace DSOServer
{
  struct WorldSetup
  {
    // Static landmarks (planets + stations). They stay visible to nearby viewers
    // beyond the ship AOI radius (see AppendLandmarks); never moved or (planets)
    // destroyed, so the ids stay valid for the process lifetime.
    std::vector<Neuron::ECS::EntityId> landmarks;

    // The chart manifest: every system, home (id -1) included, so players can
    // always teleport back.
    std::vector<Neuron::Net::GalaxySystemInfo> manifest;
  };

  // Build the world into `_world` from the given system rows (empty ⇒ generate the
  // default galaxy from the seed), overlaying `_marketDrift` onto each station's
  // baseline market. Returns the landmarks + manifest the send path and sessions
  // need.
  [[nodiscard]] WorldSetup BuildWorld(Neuron::ECS::Registry& _world,
                                      const std::vector<Neuron::Persist::SystemRow>& _systems,
                                      const std::vector<Neuron::Persist::MarketRow>& _marketDrift);
}
