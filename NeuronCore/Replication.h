#pragma once

// Replication - the entity-snapshot wire schema (NeuronCore, shared protocol).
//
// This is the ONLY thing client and server share about a moving entity: a flat
// data DTO, never behaviour. The server fills snapshots from its authoritative
// GameLogic components (WorldTransform + Flight) and sends them; the client reads
// them into its presentation layer to interpolate and dead-reckon. Neither side
// shares the other's logic - only this layout.
//
// The in-memory EntitySnapshot still carries the absolute int64 position and a
// float orientation (nose = forward, roof = up; side is recovered as their cross
// product) so the interpolator and render path are unchanged. The WIRE encoding
// (version 2, E2) is compact: position as absolute int32 (the galaxy spans only
// +/-~2.2e8 units - well inside int32's +/-2.1e9, so this is exact, no float loss),
// the basis quantized to int16 components, and speed to uint16 fixed-point (see
// Quantization.h). ~32 bytes/entity, down from 58. Encoding is hand-rolled little-
// endian binary via DataWriter/DataReader, prefixed with a magic + version so a
// stale or foreign packet is rejected rather than misread.
//
// No-back-compat (pre-launch): version 2 REPLACES version 1 outright - client and
// server are always the same build, so there is no dual-format negotiation. Once
// anything ships, a wire-layout change takes a new version with a real negotiation.

#include <cstdint>
#include <vector>

#include "DataWriter.h"
#include "DataReader.h"
#include "Quantization.h"

namespace Neuron::Net
{
  inline constexpr uint32_t SNAPSHOT_MAGIC = 0x4E534E50;   // 'NSNP'
  inline constexpr uint16_t SNAPSHOT_VERSION = 2;   // 2 (E2): int32 pos + quantized basis/speed

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
  inline constexpr std::size_t SNAPSHOT_HEADER_SIZE = 4 + 2 + 4 + 4 + 2;   // magic+version+tick+viewerId+count
  inline constexpr std::size_t SNAPSHOT_ENTITY_SIZE = 4 + (4 * 3) + (2 * 6) + 2 + 2;
      // id(4) + i32 pos(12) + i16 nose/roof(12) + u16 speed(2) + i16 type(2) = 32 (E2)

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

    int16_t type = 0;                // renderable ship type (legacy SHIP_*; 0 = default)

    [[nodiscard]] friend bool operator==(const EntitySnapshot&, const EntitySnapshot&) = default;
  };

  // A server tick's worth of entity snapshots, addressed to one viewer. `viewerId`
  // is the recipient's own entity index, so the client can identify its own ship
  // (and use it as the camera origin) straight from the snapshot - no separate
  // handshake required. 0xFFFFFFFF means "not addressed to a specific viewer".
  struct WorldSnapshot
  {
    uint32_t tick = 0;
    uint32_t viewerId = 0xFFFFFFFFu;
    std::vector<EntitySnapshot> entities;
  };

  inline void WriteSnapshot(DataWriter& _w, const WorldSnapshot& _snap)
  {
    _w.WriteU32(SNAPSHOT_MAGIC);
    _w.WriteU16(SNAPSHOT_VERSION);
    _w.WriteU32(_snap.tick);
    _w.WriteU32(_snap.viewerId);
    _w.WriteU16(static_cast<uint16_t>(_snap.entities.size()));

    for (const EntitySnapshot& e : _snap.entities)
    {
      _w.WriteU32(e.id);
      // Position as absolute int32 - exact within the galaxy's +/-~2.2e8 extent
      // (see the file header). A far outlier (never produced by the AOI/landmark
      // send path) would saturate rather than wrap; it stays in bounds today.
      _w.WriteI32(static_cast<int32_t>(e.x));
      _w.WriteI32(static_cast<int32_t>(e.y));
      _w.WriteI32(static_cast<int32_t>(e.z));
      _w.WriteU16(static_cast<uint16_t>(QuantizeUnit(e.noseX)));
      _w.WriteU16(static_cast<uint16_t>(QuantizeUnit(e.noseY)));
      _w.WriteU16(static_cast<uint16_t>(QuantizeUnit(e.noseZ)));
      _w.WriteU16(static_cast<uint16_t>(QuantizeUnit(e.roofX)));
      _w.WriteU16(static_cast<uint16_t>(QuantizeUnit(e.roofY)));
      _w.WriteU16(static_cast<uint16_t>(QuantizeUnit(e.roofZ)));
      _w.WriteU16(QuantizeSpeed(e.speed));
      _w.WriteU16(static_cast<uint16_t>(e.type));
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
    _out.viewerId = _r.ReadU32();
    const uint16_t count = _r.ReadU16();

    _out.entities.clear();
    _out.entities.reserve(count);
    for (uint16_t i = 0; i < count; ++i)
    {
      EntitySnapshot e;
      e.id = _r.ReadU32();
      e.x = static_cast<int64_t>(_r.ReadI32());   // sign-extend back to the int64 world
      e.y = static_cast<int64_t>(_r.ReadI32());
      e.z = static_cast<int64_t>(_r.ReadI32());
      e.noseX = DequantizeUnit(static_cast<int16_t>(_r.ReadU16()));
      e.noseY = DequantizeUnit(static_cast<int16_t>(_r.ReadU16()));
      e.noseZ = DequantizeUnit(static_cast<int16_t>(_r.ReadU16()));
      e.roofX = DequantizeUnit(static_cast<int16_t>(_r.ReadU16()));
      e.roofY = DequantizeUnit(static_cast<int16_t>(_r.ReadU16()));
      e.roofZ = DequantizeUnit(static_cast<int16_t>(_r.ReadU16()));
      e.speed = DequantizeSpeed(_r.ReadU16());
      e.type = static_cast<int16_t>(_r.ReadU16());
      _out.entities.push_back(e);
    }

    return _r.Ok();
  }
}
