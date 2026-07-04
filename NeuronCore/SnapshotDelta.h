#pragma once

// SnapshotDelta - per-viewer delta compression of the snapshot stream (NeuronCore,
// Track E2b).
//
// A full snapshot (E2a) re-sends every visible entity every tick. Most entities
// don't change tick-to-tick (stations, planets, parked ships, idle/distant units),
// so a DELTA - "what changed since a baseline the receiver already holds" - is far
// smaller at fleet scale. A delta carries only:
//   * CHANGED entities (new, or wire-different from the baseline) as full records;
//   * REMOVED ids (present in the baseline, gone now);
//   * everything else is implied unchanged and omitted.
//
// Reference-origin subtlety (E2a): the wire position is an offset from a per-packet
// reference that tracks the MOVING viewer, so an entity's OFFSET changes every tick
// even when it hasn't moved. The change test therefore compares ABSOLUTE positions
// (stable), never offsets - see SameOnWire.
//
// Server-side rounding: two entities are "the same on the wire" when their QUANTIZED
// fields match, so a sub-quantum float wobble is NOT a change. The server must diff
// against what it effectively sent (quantized), and the client reconstructs from the
// same quantized baseline, so both agree bit-for-bit. This file is the pure codec +
// differ; choosing baselines, sequencing, acks and keyframe cadence is the server/
// client wiring (E2b-2). Everything here is header-only and unit-tested headlessly.

#include <cstddef>
#include <cstdint>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "DataWriter.h"
#include "DataReader.h"
#include "Replication.h"

namespace Neuron::Net
{
  // Snapshot "version" byte that marks a DELTA packet, so a receiver peeking the
  // header routes it to ReadSnapshotDelta instead of the full-snapshot ReadSnapshot
  // (which requires SNAPSHOT_VERSION). Shares the 'NSNP' magic.
  inline constexpr uint16_t SNAPSHOT_DELTA_VERSION = 3;

  // Header size for a delta packet: magic+version+tick+viewerId+ref(24)+baselineTick
  // + changedCount + removedCount. Per-changed-entity is SNAPSHOT_ENTITY_SIZE; each
  // removed id is 4 bytes.
  inline constexpr std::size_t SNAPSHOT_DELTA_HEADER_SIZE = 4 + 2 + 4 + 4 + (8 * 3) + 4 + 2 + 2;   // 46

  // Changes to apply on top of a baseline the receiver already holds.
  struct DeltaSnapshot
  {
    uint32_t tick = 0;
    uint32_t viewerId = 0xFFFFFFFFu;
    int64_t refX = 0;
    int64_t refY = 0;
    int64_t refZ = 0;
    uint32_t baselineTick = 0;                // the tick of the baseline this is against
    std::vector<EntitySnapshot> changed;      // new or wire-different entities (full records)
    std::vector<uint32_t> removed;            // baseline ids absent from the current tick
  };

  // Are two entities identical AS SENT ON THE WIRE? Absolute position is compared
  // exactly (int64, stable across a moving reference origin); orientation and speed
  // are compared at their QUANTIZED resolution, so a wobble below the grid is not a
  // change (the server-side-rounding rule). Ids must match (same entity).
  [[nodiscard]] inline bool SameOnWire(const EntitySnapshot& _a, const EntitySnapshot& _b)
  {
    return _a.id == _b.id
        && _a.x == _b.x && _a.y == _b.y && _a.z == _b.z
        && _a.type == _b.type
        && QuantizeUnit(_a.noseX) == QuantizeUnit(_b.noseX)
        && QuantizeUnit(_a.noseY) == QuantizeUnit(_b.noseY)
        && QuantizeUnit(_a.noseZ) == QuantizeUnit(_b.noseZ)
        && QuantizeUnit(_a.roofX) == QuantizeUnit(_b.roofX)
        && QuantizeUnit(_a.roofY) == QuantizeUnit(_b.roofY)
        && QuantizeUnit(_a.roofZ) == QuantizeUnit(_b.roofZ)
        && QuantizeSpeed(_a.speed) == QuantizeSpeed(_b.speed);
  }

  // Diff `_current` against `_baseline`: an entity is CHANGED if it is new (absent
  // from the baseline) or wire-different; an id in the baseline that is gone from
  // the current tick is REMOVED. The delta inherits the current tick's reference
  // origin (its changed entities are encoded against it).
  [[nodiscard]] inline DeltaSnapshot SnapshotDiff(const WorldSnapshot& _baseline,
                                                  const WorldSnapshot& _current)
  {
    DeltaSnapshot d;
    d.tick = _current.tick;
    d.viewerId = _current.viewerId;
    d.refX = _current.refX;
    d.refY = _current.refY;
    d.refZ = _current.refZ;
    d.baselineTick = _baseline.tick;

    std::unordered_map<uint32_t, const EntitySnapshot*> base;
    base.reserve(_baseline.entities.size() * 2);
    for (const EntitySnapshot& e : _baseline.entities)
      base[e.id] = &e;

    std::unordered_set<uint32_t> present;
    present.reserve(_current.entities.size() * 2);
    for (const EntitySnapshot& e : _current.entities)
    {
      present.insert(e.id);
      const auto it = base.find(e.id);
      if (it == base.end() || !SameOnWire(*it->second, e))
        d.changed.push_back(e);
    }
    for (const EntitySnapshot& e : _baseline.entities)
      if (present.find(e.id) == present.end())
        d.removed.push_back(e.id);

    return d;
  }

  // Reconstruct the current snapshot by applying `_delta` to `_baseline`: keep every
  // baseline entity that was not removed or replaced, then add the changed entities.
  [[nodiscard]] inline WorldSnapshot ApplyDelta(const WorldSnapshot& _baseline,
                                                const DeltaSnapshot& _delta)
  {
    WorldSnapshot out;
    out.tick = _delta.tick;
    out.viewerId = _delta.viewerId;
    out.refX = _delta.refX;
    out.refY = _delta.refY;
    out.refZ = _delta.refZ;

    std::unordered_set<uint32_t> removed(_delta.removed.begin(), _delta.removed.end());
    std::unordered_set<uint32_t> replaced;
    replaced.reserve(_delta.changed.size() * 2);
    for (const EntitySnapshot& e : _delta.changed)
      replaced.insert(e.id);

    out.entities.reserve(_baseline.entities.size() + _delta.changed.size());
    for (const EntitySnapshot& e : _baseline.entities)
      if (removed.find(e.id) == removed.end() && replaced.find(e.id) == replaced.end())
        out.entities.push_back(e);       // survived unchanged
    for (const EntitySnapshot& e : _delta.changed)
      out.entities.push_back(e);          // new or updated

    return out;
  }

  // Serialize a delta packet ('NSNP' magic, delta version). Changed entities use the
  // SAME 32-byte record as a full snapshot (encoded against this delta's reference);
  // removed ids are bare u32s.
  inline void WriteSnapshotDelta(DataWriter& _w, const DeltaSnapshot& _delta)
  {
    _w.WriteU32(SNAPSHOT_MAGIC);
    _w.WriteU16(SNAPSHOT_DELTA_VERSION);
    _w.WriteU32(_delta.tick);
    _w.WriteU32(_delta.viewerId);
    _w.WriteI64(_delta.refX);
    _w.WriteI64(_delta.refY);
    _w.WriteI64(_delta.refZ);
    _w.WriteU32(_delta.baselineTick);
    _w.WriteU16(static_cast<uint16_t>(_delta.changed.size()));
    _w.WriteU16(static_cast<uint16_t>(_delta.removed.size()));

    for (const EntitySnapshot& e : _delta.changed)
      WriteEntityRecord(_w, e, _delta.refX, _delta.refY, _delta.refZ);
    for (uint32_t id : _delta.removed)
      _w.WriteU32(id);
  }

  // Decode a delta packet. Returns false (leaving `_out` unspecified) on a wrong
  // magic/version or a truncated buffer, so the caller drops the datagram.
  [[nodiscard]] inline bool ReadSnapshotDelta(DataReader& _r, DeltaSnapshot& _out)
  {
    if (_r.ReadU32() != SNAPSHOT_MAGIC)
      return false;
    if (_r.ReadU16() != SNAPSHOT_DELTA_VERSION)
      return false;

    _out.tick = _r.ReadU32();
    _out.viewerId = _r.ReadU32();
    _out.refX = _r.ReadI64();
    _out.refY = _r.ReadI64();
    _out.refZ = _r.ReadI64();
    _out.baselineTick = _r.ReadU32();
    const uint16_t changedCount = _r.ReadU16();
    const uint16_t removedCount = _r.ReadU16();
    if (!_r.Ok())
      return false;

    _out.changed.clear();
    _out.changed.reserve(changedCount);
    for (uint16_t i = 0; i < changedCount; ++i)
      _out.changed.push_back(ReadEntityRecord(_r, _out.refX, _out.refY, _out.refZ));

    _out.removed.clear();
    _out.removed.reserve(removedCount);
    for (uint16_t i = 0; i < removedCount; ++i)
      _out.removed.push_back(_r.ReadU32());

    return _r.Ok();
  }

  // Peek the version word of an 'NSNP' datagram without consuming it, so a receiver
  // can route it to ReadSnapshot (full) or ReadSnapshotDelta (delta). Returns 0 for
  // a too-short or non-snapshot buffer.
  [[nodiscard]] inline uint16_t PeekSnapshotVersion(const uint8_t* _data, std::size_t _size)
  {
    if (_data == nullptr || _size < 6 || PeekMagic(_data, _size) != SNAPSHOT_MAGIC)
      return 0;
    return static_cast<uint16_t>(static_cast<uint16_t>(_data[4]) |
                                 (static_cast<uint16_t>(_data[5]) << 8));
  }
}
