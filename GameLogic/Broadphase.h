#pragma once

// Broadphase - the shared cell size + candidate-gathering discipline for D1's
// grid conversions (GameLogic).
//
// Every per-tick pairwise loop (collisions, combat target scans, scooping) narrows
// its candidates through a Spatial::Grid rebuilt every call (D2: the Grid OBJECT
// persists in FrameScratch and is reused for its allocated capacity, but its
// CONTENT is unconditionally cleared and refilled from exactly the entity set that
// call scans - so it can never carry stale entries from a previous tick). The
// conversions are BEHAVIOUR-PRESERVING by construction:
//
//   * entries are keyed by the system's own dense-array index (units[i], cans[i]),
//     NOT the entity id, and candidates are sorted ascending - so iterating them
//     is a strict subsequence of the original full scan, preserving every outcome
//     including tie-breaks;
//   * the cell is >= every exact range the systems test (max: the energy bomb's
//     16 384), so a +/-1-cell query provably contains everything in range and the
//     narrowing can only drop pairs the exact test would have rejected anyway;
//   * the exact (Chebyshev / cone / distance) test still runs on every candidate.
//
// Sorting also keeps the hash-map cell iteration order out of outcomes entirely
// (the D1 determinism rule). Single-subject, per-event scans (ResolvePlayerFire,
// ActivateEcm, DetonateEnergyBomb) are NOT converted: they are O(n) per rare
// event, and a fresh grid build is itself O(n) - they only profit from a
// persistent per-tick grid, which is future work at fleet scale (it must solve
// staleness across phases first).

#include <algorithm>
#include <cstdint>
#include <vector>

#include "SpatialGrid.h"   // Neuron::Spatial::Grid (NeuronCore, header-only)
#include "Vector3i64.h"

namespace Neuron::GameLogic
{
  // One cell serves every exact range with a +/-1-cell query (largest: the energy
  // bomb radius, exactly 16 384). AOI keeps its own coarser 100 000 grid.
  inline constexpr int64_t BROADPHASE_CELL = 16384;

  // Gather the sorted dense-array indices within `_radiusCells` of `_pos`.
  // Ascending order = the original scan order (minus provably-out-of-range
  // entries); it also removes hash-map iteration order from outcomes.
  inline void QuerySortedNeighbours(const Spatial::Grid& _grid, const Math::Vector3i64& _pos,
                                    int _radiusCells, std::vector<uint64_t>& _out)
  {
    _out.clear();
    _grid.QueryNear(_pos, _radiusCells, _out);
    std::sort(_out.begin(), _out.end());
  }

  // Cells needed so a +/-N-cell query covers `_range` (1 for every range today).
  [[nodiscard]] inline int CellsForRange(int64_t _range)
  {
    return static_cast<int>((_range + BROADPHASE_CELL - 1) / BROADPHASE_CELL);
  }
}
