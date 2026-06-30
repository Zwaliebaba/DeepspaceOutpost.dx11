#pragma once

// GalaxyManifest - the galaxy's system list as shipped to the client (NeuronCore).
//
// The procedural galaxy is generated server-side (GameLogic, behaviour), but the
// client needs the resulting system list to draw the galactic chart and pick a
// teleport destination. So the server ships a manifest of pure display data once,
// on connect, over the reliable channel; the client stores and renders it and
// never generates anything itself - keeping the rule "shared data, never
// behaviour". The list is batched into MTU-sized chunks so it rides the same
// reliable-event pipe as despawns and the handshake.

#include <algorithm>
#include <cstdint>
#include <cstddef>
#include <vector>

#include "DataWriter.h"
#include "DataReader.h"
#include "ReliableChannel.h"   // ReliableMessage, SAFE_UDP_PAYLOAD
#include "Messages/MessageId.h"  // Msg::MessageId

namespace Neuron::Net
{
  inline constexpr std::size_t GALAXY_NAME_MAX = 12;   // NUL-padded system name

  // Catalog id for one galaxy-manifest chunk (a Bulk-lane reliable message). The
  // chunk is fixed-layout display data with a fixed char[] array, hand-encoded
  // below, so it carries a reserved MessageId rather than going through the generic
  // codec. (It replaces the old EventType::GalaxyManifest tag.)
  inline constexpr Msg::MessageId GALAXY_MANIFEST_ID = static_cast<Msg::MessageId>(0x0210);

  // One system as the chart needs it: identity, absolute world position of its
  // planet, and the display attributes. Pure data - the server computes these
  // from GameLogic and ships them; the client only renders them.
  struct GalaxySystemInfo
  {
    uint32_t id = 0;
    int64_t x = 0;
    int64_t y = 0;
    int64_t z = 0;
    char name[GALAXY_NAME_MAX] = {};
    uint8_t government = 0;
    uint8_t economy = 0;
    uint8_t techLevel = 0;
    uint16_t population = 0;
    uint16_t productivity = 0;
  };

  // On-wire size of one system, and how many fit one reliable chunk under the
  // safe UDP payload (leaving room for the chunk header: total + base + count).
  inline constexpr std::size_t MANIFEST_ENTRY_SIZE = 4 + 8 * 3 + GALAXY_NAME_MAX + 1 + 1 + 1 + 2 + 2;
  inline constexpr std::size_t MANIFEST_CHUNK_HEADER = 4 + 4 + 2;
  inline constexpr std::size_t MANIFEST_SYSTEMS_PER_CHUNK =
      (SAFE_UDP_PAYLOAD - MANIFEST_CHUNK_HEADER) / MANIFEST_ENTRY_SIZE;

  inline void WriteSystemInfo(DataWriter& _w, const GalaxySystemInfo& _s)
  {
    _w.WriteU32(_s.id);
    _w.WriteI64(_s.x);
    _w.WriteI64(_s.y);
    _w.WriteI64(_s.z);
    for (std::size_t i = 0; i < GALAXY_NAME_MAX; ++i)
      _w.WriteU8(static_cast<uint8_t>(_s.name[i]));
    _w.WriteU8(_s.government);
    _w.WriteU8(_s.economy);
    _w.WriteU8(_s.techLevel);
    _w.WriteU16(_s.population);
    _w.WriteU16(_s.productivity);
  }

  [[nodiscard]] inline GalaxySystemInfo ReadSystemInfo(DataReader& _r)
  {
    GalaxySystemInfo s;
    s.id = _r.ReadU32();
    s.x = _r.ReadI64();
    s.y = _r.ReadI64();
    s.z = _r.ReadI64();
    for (std::size_t i = 0; i < GALAXY_NAME_MAX; ++i)
      s.name[i] = static_cast<char>(_r.ReadU8());
    s.name[GALAXY_NAME_MAX - 1] = '\0';   // always terminated
    s.government = _r.ReadU8();
    s.economy = _r.ReadU8();
    s.techLevel = _r.ReadU8();
    s.population = _r.ReadU16();
    s.productivity = _r.ReadU16();
    return s;
  }

  // Encode one chunk: the galaxy's total system count (so the client can size its
  // table up front), this chunk's base index, then its systems.
  [[nodiscard]] inline std::vector<uint8_t> EncodeManifestChunk(
      uint32_t _total, uint32_t _baseIndex, const GalaxySystemInfo* _items, uint16_t _n)
  {
    DataWriter w;
    w.WriteU32(_total);
    w.WriteU32(_baseIndex);
    w.WriteU16(_n);
    for (uint16_t i = 0; i < _n; ++i)
      WriteSystemInfo(w, _items[i]);
    return w.Bytes();
  }

  // Decode a manifest chunk, appending its systems to `_out` and reporting the
  // galaxy total and this chunk's base index. False on a foreign/truncated message.
  [[nodiscard]] inline bool DecodeManifestChunk(const ReliableMessage& _m, uint32_t& _total,
                                                uint32_t& _baseIndex, std::vector<GalaxySystemInfo>& _out)
  {
    if (_m.type != Msg::Raw(GALAXY_MANIFEST_ID))
      return false;
    DataReader r(_m.payload.data(), _m.payload.size());
    _total = r.ReadU32();
    _baseIndex = r.ReadU32();
    const uint16_t n = r.ReadU16();
    for (uint16_t i = 0; i < n; ++i)
      _out.push_back(ReadSystemInfo(r));
    return r.Ok();
  }

  // Queue the whole manifest onto a reliable channel as a sequence of chunks.
  inline void SendManifest(ReliableChannel& _ch, const std::vector<GalaxySystemInfo>& _systems)
  {
    const uint32_t total = static_cast<uint32_t>(_systems.size());
    for (std::size_t base = 0; base < _systems.size(); base += MANIFEST_SYSTEMS_PER_CHUNK)
    {
      const std::size_t n = std::min(MANIFEST_SYSTEMS_PER_CHUNK, _systems.size() - base);
      const std::vector<uint8_t> payload =
          EncodeManifestChunk(total, static_cast<uint32_t>(base), &_systems[base], static_cast<uint16_t>(n));
      _ch.Send(Msg::Raw(GALAXY_MANIFEST_ID), payload);
    }
  }
}
