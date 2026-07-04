#pragma once

// SnapshotStream - the per-session delta snapshot stream (NeuronCore, Track E2b).
//
// Pairs a server-side ENCODER with a client-side DECODER around the E2b-1 delta
// codec, turning the tick-by-tick snapshot flow into: an occasional full snapshot
// (keyframe) plus small deltas against a baseline the client has acknowledged.
//
//   * The encoder keeps a ring of the snapshots it recently sent. Each tick it
//     deltas the current snapshot against the one the client last ACKED (looked up
//     by tick in the ring) and sends that delta if it fits one datagram; otherwise,
//     or when no acked baseline exists, or on the periodic keyframe cadence, it
//     sends a full snapshot.
//   * The decoder keeps a ring of the snapshots it has fully reconstructed. A full
//     snapshot that arrives WHOLE (complete flag) enters the ring; a delta is
//     applied against the baseline tick it names (found in the ring). The client
//     acks the latest tick it holds, so the (RTT-lagged) baseline the server picks
//     is always still in the client's ring.
//
// Loss self-heals: a dropped delta just leaves the client's ack where it was, so
// the server keeps deltaing against that older baseline until the client catches
// up; a periodic keyframe bounds the worst case. Both rings are bounded. Reordering
// is safe (deltas look their baseline up by tick; the interpolator drops stale
// ticks). Pure protocol logic - no sockets - so the pair is unit-tested headlessly.

#include <cstddef>
#include <cstdint>
#include <deque>
#include <utility>
#include <vector>

#include "DataWriter.h"
#include "DataReader.h"
#include "Replication.h"
#include "SnapshotDelta.h"
#include "SnapshotPacketizer.h"

namespace Neuron::Net
{
  // Force a full keyframe at least this often (ticks), so a client that has lost its
  // baseline recovers within ~1 s even if acks are flaky.
  inline constexpr uint32_t SNAPSHOT_KEYFRAME_INTERVAL = 30;

  // How many recently sent/reconstructed snapshots each side remembers. Must exceed
  // the worst-case ack lag (RTT in ticks) plus the keyframe interval so an acked
  // baseline is always still present.
  inline constexpr std::size_t SNAPSHOT_BASELINE_RING = 48;

  namespace Detail
  {
    struct RingEntry
    {
      uint32_t tick = 0;
      WorldSnapshot snap;
    };

    [[nodiscard]] inline const WorldSnapshot* FindInRing(const std::deque<RingEntry>& _ring, uint32_t _tick)
    {
      for (const RingEntry& e : _ring)
        if (e.tick == _tick)
          return &e.snap;
      return nullptr;
    }

    inline void PushRing(std::deque<RingEntry>& _ring, const WorldSnapshot& _s)
    {
      _ring.push_back(RingEntry{ _s.tick, _s });
      while (_ring.size() > SNAPSHOT_BASELINE_RING)
        _ring.pop_front();
    }
  }

  // Server side: encode a session's snapshot stream as deltas + periodic keyframes.
  class SnapshotStreamEncoder
  {
  public:
    // Encode `_current` for a client that has acknowledged snapshot tick `_ackedTick`
    // (0 = nothing acked yet). Returns the datagram(s) to send. Sends a single-
    // datagram DELTA against the acked baseline when one exists and fits; otherwise a
    // full snapshot (also forced on the keyframe cadence). Records `_current` so a
    // later ack can name it as a baseline.
    [[nodiscard]] std::vector<std::vector<uint8_t>> Encode(const WorldSnapshot& _current, uint32_t _ackedTick)
    {
      m_lastWasDelta = false;
      std::vector<std::vector<uint8_t>> out;

      const bool keyframeDue = (m_sinceKeyframe >= SNAPSHOT_KEYFRAME_INTERVAL);
      const WorldSnapshot* baseline =
          (!keyframeDue && _ackedTick != 0) ? Detail::FindInRing(m_ring, _ackedTick) : nullptr;

      if (baseline != nullptr)
      {
        DataWriter w;
        WriteSnapshotDelta(w, SnapshotDiff(*baseline, _current));
        if (w.Size() <= SAFE_UDP_PAYLOAD)
        {
          out.push_back(w.Bytes());
          m_lastWasDelta = true;
          ++m_sinceKeyframe;
        }
      }
      if (!m_lastWasDelta)
      {
        out = PacketizeSnapshot(_current);
        m_sinceKeyframe = 0;
      }

      Detail::PushRing(m_ring, _current);
      return out;
    }

    [[nodiscard]] bool LastWasDelta() const { return m_lastWasDelta; }

  private:
    std::deque<Detail::RingEntry> m_ring;
    uint32_t m_sinceKeyframe = SNAPSHOT_KEYFRAME_INTERVAL;   // force a keyframe on the first tick
    bool m_lastWasDelta = false;
  };

  // Client side: decode the stream back into full snapshots to render, and track the
  // latest complete baseline to acknowledge.
  class SnapshotStreamDecoder
  {
  public:
    // Decode one datagram into the full snapshot `_out` to hand the interpolator.
    // Returns false (and does not touch `_out`) on a wrong magic/version, a truncated
    // packet, or a delta whose baseline the client no longer holds (dropped - a
    // keyframe will recover it). A whole full snapshot or an applied delta becomes a
    // new baseline the client can acknowledge.
    [[nodiscard]] bool Decode(const uint8_t* _data, std::size_t _size, WorldSnapshot& _out)
    {
      const uint16_t version = PeekSnapshotVersion(_data, _size);

      if (version == SNAPSHOT_VERSION)
      {
        DataReader r(_data, _size);
        WorldSnapshot full;
        if (!ReadSnapshot(r, full))
          return false;
        if (full.complete)
          Store(full);   // only a whole-tick snapshot is a baseline the client fully holds
        _out = std::move(full);
        return true;
      }

      if (version == SNAPSHOT_DELTA_VERSION)
      {
        DataReader r(_data, _size);
        DeltaSnapshot d;
        if (!ReadSnapshotDelta(r, d))
          return false;
        const WorldSnapshot* base = Detail::FindInRing(m_ring, d.baselineTick);
        if (base == nullptr)
          return false;   // baseline lost or aged out: drop and wait for a keyframe
        _out = ApplyDelta(*base, d);
        Store(_out);
        return true;
      }

      return false;
    }

    // The latest baseline tick the client holds (0 until the first one), sent back to
    // the server so it can delta against it.
    [[nodiscard]] uint32_t AckTick() const { return m_haveLatest ? m_latestTick : 0; }

  private:
    void Store(const WorldSnapshot& _s)
    {
      Detail::PushRing(m_ring, _s);
      if (!m_haveLatest || _s.tick >= m_latestTick)
      {
        m_latestTick = _s.tick;
        m_haveLatest = true;
      }
    }

    std::deque<Detail::RingEntry> m_ring;
    uint32_t m_latestTick = 0;
    bool m_haveLatest = false;
  };
}
