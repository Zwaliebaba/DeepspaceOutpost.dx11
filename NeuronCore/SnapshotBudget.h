#pragma once

// SnapshotBudget - cap a viewer's per-tick state stream, dropping the farthest
// entities first (NeuronCore, Track E2c).
//
// Even after quantization (E2a) and delta (E2b), an overloaded area of interest -
// a huge dogfight, a fleet passing through - can exceed a session's fair share of
// bandwidth for one tick. The budget bounds it: keep the entities CLOSEST to the
// viewer (the ones that matter most on screen) up to the cap, and drop the rest;
// a dropped entity simply updates on a later tick, or once the viewer nears it.
//
// The trim is DETERMINISTIC (distance, then id) so every client - and a replay -
// sees the same set, and it is a pure function of the entity list + viewer point,
// so it is unit-tested headlessly. Applied on the server BEFORE delta-encoding, so
// the baseline and the current tick agree on the trimmed set.

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <vector>

#include "Replication.h"

namespace Neuron::Net
{
  // Per-session per-tick byte budget for the unreliable state stream. Sized to a
  // few MTU datagrams so ordinary scenes never trim; only an overloaded AOI does.
  inline constexpr std::size_t SNAPSHOT_SEND_BUDGET_BYTES = 4 * SAFE_UDP_PAYLOAD;   // ~4800 B

  // Largest whole-entity count that fits `_budgetBytes` (one header + N entities).
  [[nodiscard]] inline std::size_t SnapshotEntityBudget(std::size_t _budgetBytes = SNAPSHOT_SEND_BUDGET_BYTES)
  {
    if (_budgetBytes <= SNAPSHOT_HEADER_SIZE)
      return 0;
    return (_budgetBytes - SNAPSHOT_HEADER_SIZE) / SNAPSHOT_ENTITY_SIZE;
  }

  // Keep the `_maxEntities` entities closest to the viewer (`_vx,_vy,_vz`), dropping
  // the farthest. Sorts by squared distance ascending, tie-broken by id, so the trim
  // is identical on every client. Returns the number dropped. A no-op (and no sort)
  // when the list already fits.
  inline std::size_t TrimSnapshotToBudget(std::vector<EntitySnapshot>& _entities,
                                          int64_t _vx, int64_t _vy, int64_t _vz,
                                          std::size_t _maxEntities)
  {
    if (_entities.size() <= _maxEntities)
      return 0;

    // Squared distance stays well within int64: entities in a snapshot are within
    // the viewer's AOI, so each delta is small regardless of the absolute world size.
    const auto dist2 = [&](const EntitySnapshot& _e) -> int64_t
    {
      const int64_t dx = _e.x - _vx;
      const int64_t dy = _e.y - _vy;
      const int64_t dz = _e.z - _vz;
      return dx * dx + dy * dy + dz * dz;
    };

    std::sort(_entities.begin(), _entities.end(),
              [&](const EntitySnapshot& _a, const EntitySnapshot& _b)
              {
                const int64_t da = dist2(_a);
                const int64_t db = dist2(_b);
                if (da != db)
                  return da < db;
                return _a.id < _b.id;   // deterministic tie-break
              });

    const std::size_t dropped = _entities.size() - _maxEntities;
    _entities.resize(_maxEntities);
    return dropped;
  }
}
