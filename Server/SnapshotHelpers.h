#pragma once

// SnapshotHelpers - per-viewer snapshot trimmings (Server).
//
// Two small pieces of the send path that aren't general enough for GameLogic:
// the despawn-diff input (every live entity id) and the landmark append that
// keeps a system's planet/station visible across the whole system instead of
// popping at the ship-AOI boundary.

#include <cstdint>
#include <unordered_set>
#include <vector>

#include "ECS.h"
#include "Replication.h"
#include "Vector3i64.h"

#include "GameLogic.h"
#include "ServerConfig.h"

namespace DSOServer
{
  // Indices of every entity currently in the world (for the despawn diff).
  [[nodiscard]] inline std::vector<uint32_t> CurrentIds(Neuron::ECS::Registry& _world)
  {
    using namespace Neuron;
    std::vector<uint32_t> ids;
    _world.Each<GameLogic::WorldTransform>([&ids](ECS::EntityId _id, GameLogic::WorldTransform&)
    {
      ids.push_back(_id.index);
    });
    return ids;
  }

  // Add landmark entities (planets/stations) within LANDMARK_VIS_DIST of the
  // viewer to an already-built AOI snapshot, skipping any the AOI pass already
  // included. Keeps the planet/station the player is flying around from popping
  // out at the ship-AOI boundary, without widening interest for ordinary ships.
  inline void AppendLandmarks(Neuron::ECS::Registry& _world, Neuron::Net::WorldSnapshot& _snap,
                              const Neuron::Math::Vector3i64& _viewerPos,
                              const std::vector<Neuron::ECS::EntityId>& _landmarks)
  {
    using namespace Neuron;

    std::unordered_set<uint32_t> present;
    present.reserve(_snap.entities.size() * 2);
    for (const Net::EntitySnapshot& e : _snap.entities)
      present.insert(e.id);

    for (ECS::EntityId lm : _landmarks)
    {
      if (present.count(lm.index))
        continue;
      if (!_world.IsValid(lm))
        continue;   // a station can (just) be destroyed; its id then drops out
      const GameLogic::WorldTransform* t = _world.TryGet<GameLogic::WorldTransform>(lm);
      if (t == nullptr)
        continue;

      // Galaxy extent is +/-1e8, so each delta squared (<=4e16) and their sum
      // (<=1.2e17) stay well within int64; LANDMARK_VIS_DIST^2 is 4e12.
      const int64_t dx = t->position.x - _viewerPos.x;
      const int64_t dy = t->position.y - _viewerPos.y;
      const int64_t dz = t->position.z - _viewerPos.z;
      if (dx * dx + dy * dy + dz * dz <= Cfg::LANDMARK_VIS_DIST * Cfg::LANDMARK_VIS_DIST)
        _snap.entities.push_back(GameLogic::MakeEntitySnapshot(_world, lm, *t));
    }
  }
}
