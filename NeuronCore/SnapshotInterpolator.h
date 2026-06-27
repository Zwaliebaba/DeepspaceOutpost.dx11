#pragma once

// SnapshotInterpolator - smooth replicated motion between snapshots (NeuronCore).
//
// The client renders a short time in the PAST and interpolates between the two
// most recent snapshots that bracket the render time, so motion stays smooth even
// though snapshots arrive at a coarse, jittery tick rate (and some are lost). This
// is the client-side complement to the reliability-free transport: SnapshotReceiver
// keeps only the freshest state (good for logic), the interpolator keeps the last
// two states per entity (needed to tween for rendering).
//
// Ingest is last-writer-wins by tick, same as the receiver - a stale/reordered
// datagram never displaces a newer one. Sample(id, alpha) blends position from the
// previous tick toward the current by alpha in [0,1]; orientation and speed are
// taken from the freshest state (cheap, and good enough until orientation tweening
// is needed). LocalOffset() rebases an absolute snapshot into the float render
// frame around a floating origin (the local player), reusing Vector3i64::RelativeTo.

#include <cmath>
#include <cstdint>
#include <unordered_map>
#include <vector>

#include "Vector3i64.h"
#include "Vector3d.h"
#include "DataReader.h"
#include "Replication.h"

namespace Neuron::Net
{
  class SnapshotInterpolator
  {
  public:
    // Decode and ingest one datagram. Returns false if it failed to decode.
    bool Apply(const uint8_t* _data, std::size_t _size)
    {
      DataReader reader(_data, _size);
      WorldSnapshot snap;
      if (!ReadSnapshot(reader, snap))
        return false;
      Ingest(snap);
      return true;
    }

    // Ingest an already-decoded snapshot (also used directly by tests).
    void Ingest(const WorldSnapshot& _snap)
    {
      if (_snap.tick > m_latestTick)
        m_latestTick = _snap.tick;

      if (_snap.viewerId != 0xFFFFFFFFu)
        m_viewerId = _snap.viewerId;   // the snapshot tells us our own entity id

      for (const EntitySnapshot& e : _snap.entities)
      {
        History& h = m_history[e.id];
        if (_snap.tick > h.currTick)
        {
          // A newer tick: the old current becomes the previous to tween from.
          if (h.currTick != 0)
          {
            h.prev = h.curr;
            h.prevTick = h.currTick;
            h.hasPrev = true;
          }
          h.curr = e;
          h.currTick = _snap.tick;
        }
        else if (_snap.tick == h.currTick)
        {
          h.curr = e;   // same tick, more of the same snapshot
        }
        // else: older than current -> drop.
      }
    }

    // Interpolated state of `_id` at `_alpha` in [0,1] between its previous and
    // current snapshot. Returns false if the entity is unknown. With only one
    // snapshot so far, returns that snapshot unblended.
    [[nodiscard]] bool Sample(uint32_t _id, double _alpha, EntitySnapshot& _out) const
    {
      auto it = m_history.find(_id);
      if (it == m_history.end())
        return false;

      const History& h = it->second;
      if (!h.hasPrev)
      {
        _out = h.curr;
        return true;
      }

      const double a = _alpha < 0.0 ? 0.0 : (_alpha > 1.0 ? 1.0 : _alpha);

      _out = h.curr;   // orientation + speed from the freshest state
      _out.x = h.prev.x + static_cast<int64_t>(std::llround(static_cast<double>(h.curr.x - h.prev.x) * a));
      _out.y = h.prev.y + static_cast<int64_t>(std::llround(static_cast<double>(h.curr.y - h.prev.y) * a));
      _out.z = h.prev.z + static_cast<int64_t>(std::llround(static_cast<double>(h.curr.z - h.prev.z) * a));
      return true;
    }

    // Interpolated state of every known entity at `_alpha`.
    [[nodiscard]] std::vector<EntitySnapshot> SampleAll(double _alpha) const
    {
      std::vector<EntitySnapshot> out;
      out.reserve(m_history.size());
      EntitySnapshot e;
      for (const auto& [id, h] : m_history)
      {
        (void)h;
        if (Sample(id, _alpha, e))
          out.push_back(e);
      }
      return out;
    }

    // Forget entities not refreshed within `_maxAge` ticks of the latest tick.
    void EvictStale(uint32_t _maxAge)
    {
      for (auto it = m_history.begin(); it != m_history.end();)
      {
        if (m_latestTick - it->second.currTick > _maxAge)
          it = m_history.erase(it);
        else
          ++it;
      }
    }

    [[nodiscard]] std::size_t Count() const { return m_history.size(); }
    [[nodiscard]] uint32_t LatestTick() const { return m_latestTick; }

    // The local viewer's own entity id, learned from the snapshots (0xFFFFFFFF
    // until the first snapshot arrives).
    [[nodiscard]] uint32_t ViewerId() const { return m_viewerId; }

  private:
    struct History
    {
      EntitySnapshot prev;
      EntitySnapshot curr;
      uint32_t prevTick = 0;
      uint32_t currTick = 0;
      bool hasPrev = false;
    };

    std::unordered_map<uint32_t, History> m_history;
    uint32_t m_latestTick = 0;
    uint32_t m_viewerId = 0xFFFFFFFFu;
  };

  // Rebase an absolute snapshot into the local float render frame around
  // `_originAbs` (the floating origin, typically the local player's world
  // position). The delta is small (within an interest cell) so it fits a double.
  [[nodiscard]] inline Math::Vector3d LocalOffset(const EntitySnapshot& _e, const Math::Vector3i64& _originAbs)
  {
    double dx, dy, dz;
    Math::RelativeTo(Math::Vector3i64{ _e.x, _e.y, _e.z }, _originAbs, dx, dy, dz);
    return Math::Vector3d{ dx, dy, dz };
  }
}
