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
// (version 2, E2) is compact:
//   * the packet HEADER carries a reference origin as a full int64 (the viewer's
//     position); each entity then stores its position as an int32 OFFSET from that
//     origin. The absolute world stays UNBOUNDED int64 - only the offset is int32,
//     and every entity in a snapshot is within the viewer's area of interest (a few
//     million units at most), so the offset always fits int32 no matter how large
//     the galaxy grows. Exact (integer subtraction, no float loss).
//   * the basis is quantized to int16 components and speed to uint16 fixed-point
//     (see Quantization.h).
// ~32 bytes/entity, down from 58 (plus 24 bytes of reference in the header, once
// per packet, amortized over every entity in it). Encoding is hand-rolled little-
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
  inline constexpr std::size_t SNAPSHOT_HEADER_SIZE = 4 + 2 + 4 + 4 + (8 * 3) + 2;
      // magic(4)+version(2)+tick(4)+viewerId(4)+refOrigin i64x3(24)+count(2) = 40 (E2)
  inline constexpr std::size_t SNAPSHOT_ENTITY_SIZE = 4 + (4 * 3) + (2 * 6) + 2 + 2;
      // id(4) + i32 pos OFFSET(12) + i16 nose/roof(12) + u16 speed(2) + i16 type(2) = 32 (E2)

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

    // Reference origin for the compact wire encoding (E2): entity positions are
    // sent as int32 offsets from this int64 point. The server sets it to the
    // viewer's position so offsets stay small (AOI-bounded); it defaults to 0, so
    // an in-memory snapshot with modest absolute positions round-trips unchanged.
    int64_t refX = 0;
    int64_t refY = 0;
    int64_t refZ = 0;

    std::vector<EntitySnapshot> entities;
  };

  // Narrow an AOI-bounded position offset to int32, saturating (never wrapping) if
  // it somehow exceeds the range - so a caller bug degrades to a clamped position
  // rather than an entity teleported by integer wraparound.
  [[nodiscard]] inline int32_t OffsetToI32(int64_t _v)
  {
    constexpr int64_t lo = -2147483647 - 1;   // INT32_MIN
    constexpr int64_t hi = 2147483647;        // INT32_MAX
    return static_cast<int32_t>(_v < lo ? lo : (_v > hi ? hi : _v));
  }

  // Encode one entity record (32 bytes): its position as an int32 OFFSET from the
  // reference origin (see the file header) - AOI-bounded, so it always fits int32
  // however large the absolute int64 world grows, and clamped (never wrapped) on an
  // out-of-AOI bug - plus its quantized basis/speed and type. Shared by the full
  // snapshot and the delta snapshot (E2b) so the entity layout lives in ONE place.
  inline void WriteEntityRecord(DataWriter& _w, const EntitySnapshot& _e,
                                int64_t _refX, int64_t _refY, int64_t _refZ)
  {
    _w.WriteU32(_e.id);
    _w.WriteI32(OffsetToI32(_e.x - _refX));
    _w.WriteI32(OffsetToI32(_e.y - _refY));
    _w.WriteI32(OffsetToI32(_e.z - _refZ));
    _w.WriteU16(static_cast<uint16_t>(QuantizeUnit(_e.noseX)));
    _w.WriteU16(static_cast<uint16_t>(QuantizeUnit(_e.noseY)));
    _w.WriteU16(static_cast<uint16_t>(QuantizeUnit(_e.noseZ)));
    _w.WriteU16(static_cast<uint16_t>(QuantizeUnit(_e.roofX)));
    _w.WriteU16(static_cast<uint16_t>(QuantizeUnit(_e.roofY)));
    _w.WriteU16(static_cast<uint16_t>(QuantizeUnit(_e.roofZ)));
    _w.WriteU16(QuantizeSpeed(_e.speed));
    _w.WriteU16(static_cast<uint16_t>(_e.type));
  }

  // Decode one entity record, reconstructing the absolute int64 position from the
  // reference origin + int32 offset. The caller checks the reader's Ok() afterward.
  [[nodiscard]] inline EntitySnapshot ReadEntityRecord(DataReader& _r,
                                                       int64_t _refX, int64_t _refY, int64_t _refZ)
  {
    EntitySnapshot e;
    e.id = _r.ReadU32();
    e.x = _refX + static_cast<int64_t>(_r.ReadI32());
    e.y = _refY + static_cast<int64_t>(_r.ReadI32());
    e.z = _refZ + static_cast<int64_t>(_r.ReadI32());
    e.noseX = DequantizeUnit(static_cast<int16_t>(_r.ReadU16()));
    e.noseY = DequantizeUnit(static_cast<int16_t>(_r.ReadU16()));
    e.noseZ = DequantizeUnit(static_cast<int16_t>(_r.ReadU16()));
    e.roofX = DequantizeUnit(static_cast<int16_t>(_r.ReadU16()));
    e.roofY = DequantizeUnit(static_cast<int16_t>(_r.ReadU16()));
    e.roofZ = DequantizeUnit(static_cast<int16_t>(_r.ReadU16()));
    e.speed = DequantizeSpeed(_r.ReadU16());
    e.type = static_cast<int16_t>(_r.ReadU16());
    return e;
  }

  inline void WriteSnapshot(DataWriter& _w, const WorldSnapshot& _snap)
  {
    _w.WriteU32(SNAPSHOT_MAGIC);
    _w.WriteU16(SNAPSHOT_VERSION);
    _w.WriteU32(_snap.tick);
    _w.WriteU32(_snap.viewerId);
    _w.WriteI64(_snap.refX);   // reference origin: positions below are int32 offsets from it
    _w.WriteI64(_snap.refY);
    _w.WriteI64(_snap.refZ);
    _w.WriteU16(static_cast<uint16_t>(_snap.entities.size()));

    for (const EntitySnapshot& e : _snap.entities)
      WriteEntityRecord(_w, e, _snap.refX, _snap.refY, _snap.refZ);
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
    _out.refX = _r.ReadI64();
    _out.refY = _r.ReadI64();
    _out.refZ = _r.ReadI64();
    const uint16_t count = _r.ReadU16();

    _out.entities.clear();
    _out.entities.reserve(count);
    for (uint16_t i = 0; i < count; ++i)
      _out.entities.push_back(ReadEntityRecord(_r, _out.refX, _out.refY, _out.refZ));

    return _r.Ok();
  }
}
