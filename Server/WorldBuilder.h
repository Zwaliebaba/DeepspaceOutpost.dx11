#pragma once

// WorldBuilder - bootstrap the authoritative world (Server).
//
// Fills a fresh registry with the hand-placed home system (planet, station with
// its market, one opt-in pirate) and the procedural galaxy (a planet + market-
// carrying station per generated system), and builds the chart manifest shipped
// to every client on connect. Pure world-construction - no sockets, no loop
// state - so the server main stays orchestration only.

#include <vector>

#include "ECS.h"
#include "GalaxyManifest.h"

namespace DSOServer
{
  struct WorldSetup
  {
    // Static landmarks (planets + stations). They stay visible to nearby viewers
    // beyond the ship AOI radius (see AppendLandmarks); never moved or (planets)
    // destroyed, so the ids stay valid for the process lifetime.
    std::vector<Neuron::ECS::EntityId> landmarks;

    // The chart manifest: the procedural systems plus the hand-placed home
    // system (id -1) so players can always teleport back.
    std::vector<Neuron::Net::GalaxySystemInfo> manifest;
  };

  // Build the world into `_world`; returns the landmarks + manifest the send
  // path and sessions need.
  [[nodiscard]] WorldSetup BuildWorld(Neuron::ECS::Registry& _world);
}
