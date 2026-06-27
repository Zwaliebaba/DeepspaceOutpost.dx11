#pragma once

// AreaOfInterest - per-viewer snapshot culling over the spatial grid (GameLogic).
//
// A server with up to 100 players cannot send every player the whole world each
// tick. Interest management fixes that: each viewer is only told about entities
// near it. The seamless int64 world is indexed by the invisible Spatial::Grid;
// AreaOfInterest rebuilds that index from the world each tick and, for a given
// viewer, snapshots only the entities in the cells around it. The world stays
// seamless to players - the grid is purely an internal acceleration structure,
// and the cell is also the unit a zone/shard is later carved along.
//
// Broad phase is cell-granular (a cube of +/- radiusCells around the viewer's
// cell); callers wanting an exact sphere can filter the result further. Server-
// side only; pure apart from the world it reads, so it is unit-tested headlessly.

#include <cstdint>
#include <vector>

#include "ECS.h"
#include "SpatialGrid.h"
#include "Replication.h"

#include "SimComponents.h"
#include "SnapshotBuilder.h"

namespace Neuron::GameLogic
{
  class AreaOfInterest
  {
  public:
    explicit AreaOfInterest(int64_t _cellSize) : m_grid(_cellSize) {}

    [[nodiscard]] int64_t CellSize() const { return m_grid.CellSize(); }
    [[nodiscard]] std::size_t OccupiedCellCount() const { return m_grid.OccupiedCellCount(); }

    // Re-index every entity that has a WorldTransform by its current cell. Cheap
    // to call each tick; the grid is rebuilt from scratch so it never drifts.
    void Rebuild(ECS::Registry& _world)
    {
      m_grid = Spatial::Grid(m_grid.CellSize());
      m_index.clear();
      _world.Each<WorldTransform>([this](ECS::EntityId _id, WorldTransform& _t)
      {
        m_grid.Insert(_id.index, _t.position);
        m_index[_id.index] = _id;
      });
    }

    // Build a snapshot for a viewer at `_viewerPos` containing only the entities
    // within `_radiusCells` cells of it. Call Rebuild() first each tick.
    [[nodiscard]] Net::WorldSnapshot SnapshotFor(ECS::Registry& _world, uint32_t _tick,
                                                 const Math::Vector3i64& _viewerPos, int _radiusCells) const
    {
      Net::WorldSnapshot snap;
      snap.tick = _tick;

      std::vector<uint64_t> nearby;
      m_grid.QueryNear(_viewerPos, _radiusCells, nearby);

      for (uint64_t raw : nearby)
      {
        const auto it = m_index.find(static_cast<uint32_t>(raw));
        if (it == m_index.end())
          continue;
        const ECS::EntityId id = it->second;
        if (const WorldTransform* t = _world.TryGet<WorldTransform>(id))
          snap.entities.push_back(MakeEntitySnapshot(_world, id, *t));
      }

      return snap;
    }

  private:
    Spatial::Grid m_grid;
    std::unordered_map<uint32_t, ECS::EntityId> m_index;   // grid id (index) -> full handle
  };
}
