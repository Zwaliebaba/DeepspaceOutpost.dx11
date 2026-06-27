#pragma once

// Replication - the entity-snapshot wire schema (NeuronCore, shared protocol).
//
// This is the ONLY thing client and server share about a moving entity: a flat
// data DTO, never behaviour. The server fills snapshots from its authoritative
// GameLogic components (WorldTransform + Flight) and sends them; the client reads
// them into its presentation layer to interpolate and dead-reckon. Neither side
// shares the other's logic - only this layout.
//
// The snapshot carries the absolute int64 position (exact across the huge world),
// a compact float orientation (nose = forward, roof = up; side is recovered as
// their cross product), and the current speed so the client can dead-reckon
// between snapshots. Encoding is hand-rolled little-endian binary via
// DataWriter/DataReader, prefixed with a magic + version so a stale or foreign
// packet is rejected rather than misread.

#include <cstdint>
#include <vector>

#include "DataWriter.h"
#include "DataReader.h"

namespace Neuron::Net
{
  inline constexpr uint32_t SNAPSHOT_MAGIC = 0x4E534E50;   // 'NSNP'
  inline constexpr uint16_t SNAPSHOT_VERSION = 1;

  // Read the leading little-endian u32 (the packet magic) without consuming a
  // reader, so a receiver can route a datagram to the right channel (snapshot vs
  // reliable event). Returns 0 for a too-short buffer.
  [[nodiscard]] inline uint32_t PeekMagic(const uint8_t* _data, std::size_t _size)
  {
    if (_size < 4)
      return 0;
    return static_cast<uint32_t>(_data[0]) | (static_cast<uint32_t>(_data[1]) << 8) |
           (static_cast<uint32_t>(_data[2]) << 16) | (static_cast<uint32_t>(_data[3]) << 24);
  }

  // Exact serialized sizes, so a packetizer can split a snapshot into datagrams
  // that hold only WHOLE entities and never exceed a target MTU. Must stay in
  // lock-step with WriteSnapshot/ReadSnapshot below.
  inline constexpr std::size_t SNAPSHOT_HEADER_SIZE = 4 + 2 + 4 + 2;   // magic+version+tick+count
  inline constexpr std::size_t SNAPSHOT_ENTITY_SIZE = 4 + (8 * 3) + (4 * 7);   // id + i64 pos + f32 orient/speed

  // A conservative UDP payload that avoids IP fragmentation across the public
  // internet (well under the 1500-byte Ethernet MTU minus IP+UDP headers, and at
  // the QUIC/IPv6-min safe size). State datagrams are kept at or below this so
  // each one travels and is applied independently - no reliability layer needed.
  inline constexpr std::size_t SAFE_UDP_PAYLOAD = 1200;

  // One replicated entity's presentation state.
  struct EntitySnapshot
  {
    uint32_t id = 0;                 // entity index (generation folded in by id scheme)

    int64_t x = 0;                   // absolute world position
    int64_t y = 0;
    int64_t z = 0;

    float noseX = 0.0f;              // forward (direction of travel)
    float noseY = 0.0f;
    float noseZ = 1.0f;
    float roofX = 0.0f;              // up
    float roofY = 1.0f;
    float roofZ = 0.0f;

    float speed = 0.0f;              // world units/tick along the nose (dead-reckoning)

    [[nodiscard]] friend bool operator==(const EntitySnapshot&, const EntitySnapshot&) = default;
  };

  // A server tick's worth of entity snapshots.
  struct WorldSnapshot
  {
    uint32_t tick = 0;
    std::vector<EntitySnapshot> entities;
  };

  inline void WriteSnapshot(DataWriter& _w, const WorldSnapshot& _snap)
  {
    _w.WriteU32(SNAPSHOT_MAGIC);
    _w.WriteU16(SNAPSHOT_VERSION);
    _w.WriteU32(_snap.tick);
    _w.WriteU16(static_cast<uint16_t>(_snap.entities.size()));

    for (const EntitySnapshot& e : _snap.entities)
    {
      _w.WriteU32(e.id);
      _w.WriteI64(e.x);
      _w.WriteI64(e.y);
      _w.WriteI64(e.z);
      _w.WriteF32(e.noseX);
      _w.WriteF32(e.noseY);
      _w.WriteF32(e.noseZ);
      _w.WriteF32(e.roofX);
      _w.WriteF32(e.roofY);
      _w.WriteF32(e.roofZ);
      _w.WriteF32(e.speed);
    }
  }

  // Decode a snapshot. Returns false (and leaves `_out` unspecified) if the magic
  // or version is wrong or the buffer is truncated - the caller drops the packet.
  [[nodiscard]] inline bool ReadSnapshot(DataReader& _r, WorldSnapshot& _out)
  {
    if (_r.ReadU32() != SNAPSHOT_MAGIC)
      return false;
    if (_r.ReadU16() != SNAPSHOT_VERSION)
      return false;

    _out.tick = _r.ReadU32();
    const uint16_t count = _r.ReadU16();

    _out.entities.clear();
    _out.entities.reserve(count);
    for (uint16_t i = 0; i < count; ++i)
    {
      EntitySnapshot e;
      e.id = _r.ReadU32();
      e.x = _r.ReadI64();
      e.y = _r.ReadI64();
      e.z = _r.ReadI64();
      e.noseX = _r.ReadF32();
      e.noseY = _r.ReadF32();
      e.noseZ = _r.ReadF32();
      e.roofX = _r.ReadF32();
      e.roofY = _r.ReadF32();
      e.roofZ = _r.ReadF32();
      e.speed = _r.ReadF32();
      _out.entities.push_back(e);
    }

    return _r.Ok();
  }
}
