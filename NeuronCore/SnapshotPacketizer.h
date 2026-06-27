#pragma once

// SnapshotPacketizer - split a world snapshot into MTU-bounded datagrams that
// need no reliability layer (NeuronCore, shared protocol).
//
// Snapshot replication is unreliable BY DESIGN: each per-entity update is a
// complete truth at a server tick, so a dropped datagram is simply corrected by
// the next tick's snapshot. The only rules that make this safe are:
//   1. every datagram is independently decodable (a self-contained partial
//      snapshot carrying its tick), and
//   2. an entity is never split across datagrams - we pack only WHOLE entities,
//      so a single lost packet costs at most a handful of entities for one tick,
//      never a half-decoded entity or a stalled reassembly.
// IP fragmentation would break rule 1 (all fragments must arrive), so we keep
// every datagram at or below a fragmentation-safe payload and let the network
// carry each as one packet.
//
// Reliability is reserved for the MINORITY of traffic that cannot be superseded
// (despawns, one-shot events); the bulk state below flows lossily and self-heals.

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <vector>

#include "DataWriter.h"
#include "Replication.h"

namespace Neuron::Net
{
  // How many whole entities fit in one datagram of `_maxPayload` bytes (at least
  // one, so a small MTU still makes progress even if a packet must exceed it).
  [[nodiscard]] inline std::size_t EntitiesPerDatagram(std::size_t _maxPayload)
  {
    if (_maxPayload <= SNAPSHOT_HEADER_SIZE)
      return 1;
    const std::size_t fit = (_maxPayload - SNAPSHOT_HEADER_SIZE) / SNAPSHOT_ENTITY_SIZE;
    return fit == 0 ? 1 : fit;
  }

  // Split `_snap` into one or more datagrams, each holding whole entities and
  // (capacity permitting) staying within `_maxPayload`. Every datagram repeats
  // the snapshot tick, so each is applied independently by the receiver. An empty
  // snapshot still yields a single header-only datagram (a tick keep-alive).
  [[nodiscard]] inline std::vector<std::vector<uint8_t>> PacketizeSnapshot(
      const WorldSnapshot& _snap, std::size_t _maxPayload = SAFE_UDP_PAYLOAD)
  {
    std::vector<std::vector<uint8_t>> datagrams;
    const std::size_t perDatagram = EntitiesPerDatagram(_maxPayload);

    if (_snap.entities.empty())
    {
      WorldSnapshot keepAlive;
      keepAlive.tick = _snap.tick;
      DataWriter w;
      WriteSnapshot(w, keepAlive);
      datagrams.push_back(w.Bytes());
      return datagrams;
    }

    for (std::size_t i = 0; i < _snap.entities.size(); i += perDatagram)
    {
      const std::size_t end = std::min(i + perDatagram, _snap.entities.size());

      WorldSnapshot part;
      part.tick = _snap.tick;
      part.entities.assign(_snap.entities.begin() + static_cast<std::ptrdiff_t>(i),
                           _snap.entities.begin() + static_cast<std::ptrdiff_t>(end));

      DataWriter w;
      WriteSnapshot(w, part);
      datagrams.push_back(w.Bytes());
    }

    return datagrams;
  }
}
