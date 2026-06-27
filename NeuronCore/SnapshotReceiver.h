#pragma once

// SnapshotReceiver - reassemble lossy, out-of-order state datagrams into the
// current best-known world (NeuronCore, shared protocol).
//
// This is the receive side of the reliability-free replication scheme. It applies
// each datagram independently and keeps, per entity, only the freshest update it
// has seen (last-writer-wins by tick). That single rule absorbs the two things an
// unreliable channel does to us:
//   * loss      - a missing datagram just means this entity keeps last tick's
//                 value until the next datagram refreshes it;
//   * reordering - a late datagram carrying an OLDER tick than one already applied
//                 is dropped, so stale data can never overwrite fresh.
//
// Despawns cannot be expressed by absence in a partial-snapshot stream, so the
// receiver evicts entities it has not heard about for a while (EvictStale); hard,
// immediate removals will ride the reliable event channel later.

#include <cstdint>
#include <unordered_map>
#include <vector>

#include "DataReader.h"
#include "Replication.h"

namespace Neuron::Net
{
  class SnapshotReceiver
  {
  public:
    // Decode and apply one received datagram. Returns false if it failed to
    // decode (bad magic / version / truncated) - the caller simply drops it.
    bool Apply(const uint8_t* _data, std::size_t _size)
    {
      DataReader reader(_data, _size);
      WorldSnapshot snap;
      if (!ReadSnapshot(reader, snap))
        return false;

      if (snap.tick > m_latestTick)
        m_latestTick = snap.tick;

      for (const EntitySnapshot& e : snap.entities)
      {
        auto it = m_tracked.find(e.id);
        if (it == m_tracked.end() || snap.tick > it->second.tick)
          m_tracked[e.id] = Tracked{ e, snap.tick };
        // else: an older or equal tick for an entity we already have -> drop.
      }
      return true;
    }

    // Current best-known state for `_id`, or nullptr if unknown.
    [[nodiscard]] const EntitySnapshot* Get(uint32_t _id) const
    {
      auto it = m_tracked.find(_id);
      return it != m_tracked.end() ? &it->second.snapshot : nullptr;
    }

    // The tick at which `_id` was last updated (0 if unknown).
    [[nodiscard]] uint32_t TickOf(uint32_t _id) const
    {
      auto it = m_tracked.find(_id);
      return it != m_tracked.end() ? it->second.tick : 0;
    }

    [[nodiscard]] std::size_t Count() const { return m_tracked.size(); }
    [[nodiscard]] uint32_t LatestTick() const { return m_latestTick; }

    // Snapshot of all currently-known entities (for the client to render).
    [[nodiscard]] std::vector<EntitySnapshot> Entities() const
    {
      std::vector<EntitySnapshot> out;
      out.reserve(m_tracked.size());
      for (const auto& [id, t] : m_tracked)
        out.push_back(t.snapshot);
      return out;
    }

    // Forget entities not refreshed within `_maxAge` ticks of the latest tick
    // seen. Stands in for despawn until reliable removal events exist.
    void EvictStale(uint32_t _maxAge)
    {
      for (auto it = m_tracked.begin(); it != m_tracked.end();)
      {
        if (m_latestTick - it->second.tick > _maxAge)
          it = m_tracked.erase(it);
        else
          ++it;
      }
    }

  private:
    struct Tracked
    {
      EntitySnapshot snapshot;
      uint32_t tick = 0;
    };

    std::unordered_map<uint32_t, Tracked> m_tracked;
    uint32_t m_latestTick = 0;
  };
}
