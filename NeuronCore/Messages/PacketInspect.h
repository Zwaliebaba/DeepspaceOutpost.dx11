#pragma once

// PacketInspect - decode a captured datagram to (lane, records) without the game.
//
// The core of a standalone packet-dump tool: given raw bytes and the message
// registry, identify the datagram family by magic and list its records (id, name,
// length). Handles the two message-bearing families - 'NMSG' (the unreliable lane,
// length-prefixed records) and 'NRLB' (a reliable lane wrapping a ReliableChannel
// packet). Snapshot datagrams are a separate stream and are reported as foreign.
//
// Pure (bytes + registry in, struct/string out) and bounds-safe (a malformed or
// truncated capture yields ok=false, never a crash), so it doubles as a robustness
// check and is unit-tested headlessly.

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "DataReader.h"
#include "ReliableChannel.h"   // Net::EVENT_MAGIC / EVENT_VERSION (inner reliable packet)

#include "MessageId.h"
#include "MessageTraits.h"
#include "Framing.h"           // PeekMessageMagic, MESSAGE_MAGIC, ReadPacket, Record
#include "MessageEndpoint.h"   // RELIABLE_MAGIC, RELIABLE_LANE_COUNT
#include "Registry.h"
#include "CatalogTools.h"      // LaneName, HexId

namespace Neuron::Msg
{
  enum class PacketKind : uint8_t { Unknown, Unreliable, Reliable };

  struct InspectedRecord
  {
    MessageId id = MessageId::Invalid;
    std::string name;       // from the registry; "<unknown>" if not catalogued
    std::size_t length = 0; // payload bytes
  };

  struct InspectedPacket
  {
    PacketKind kind = PacketKind::Unknown;
    MessageLane lane = MessageLane::Unreliable;
    std::vector<InspectedRecord> records;
    bool ok = false;
  };

  [[nodiscard]] inline std::string LookupName(const MessageRegistry& _reg, MessageId _id)
  {
    const MessageInfo* i = _reg.Find(_id);
    return i != nullptr ? std::string(i->name) : std::string("<unknown>");
  }

  [[nodiscard]] inline InspectedPacket InspectPacket(const uint8_t* _data, std::size_t _size,
                                                     const MessageRegistry& _reg)
  {
    InspectedPacket out;
    const uint32_t magic = PeekMessageMagic(_data, _size);

    // 'NMSG' - unreliable lane: length-prefixed records (reuse the framing parser).
    if (magic == MESSAGE_MAGIC)
    {
      PacketHeader hdr;
      std::vector<Record> recs;
      if (!ReadPacket(_data, _size, hdr, recs))
        return out;
      out.kind = PacketKind::Unreliable;
      out.lane = hdr.lane;
      for (const Record& r : recs)
        out.records.push_back(InspectedRecord{ r.id, LookupName(_reg, r.id), r.payload.size() });
      out.ok = true;
      return out;
    }

    // 'NRLB' - reliable lane: [magic][lane] then an inner ReliableChannel packet.
    if (magic == RELIABLE_MAGIC)
    {
      Net::DataReader r(_data, _size);
      (void)r.ReadU32();                       // RELIABLE_MAGIC
      const uint8_t lane = r.ReadU8();
      if (!r.Ok() || lane >= RELIABLE_LANE_COUNT)
        return out;
      if (r.ReadU32() != Net::EVENT_MAGIC)     // inner packet self-identifies
        return out;
      if (r.ReadU16() != Net::EVENT_VERSION)
        return out;
      (void)r.ReadU32();                       // cumulative ack
      const uint16_t count = r.ReadU16();
      if (!r.Ok())
        return out;

      for (uint16_t i = 0; i < count; ++i)
      {
        (void)r.ReadU32();                     // seq
        const uint16_t type = r.ReadU16();
        const uint16_t len = r.ReadU16();
        if (!r.Ok() || len > r.Remaining())
          return out;
        for (uint16_t b = 0; b < len; ++b)
          (void)r.ReadU8();                    // skip the payload bytes
        if (!r.Ok())
          return out;
        const MessageId id = static_cast<MessageId>(type);
        out.records.push_back(InspectedRecord{ id, LookupName(_reg, id), len });
      }
      out.kind = PacketKind::Reliable;
      out.lane = static_cast<MessageLane>(lane);
      out.ok = true;
      return out;
    }

    return out;   // foreign/snapshot/short -> ok = false
  }

  [[nodiscard]] inline std::string FormatPacket(const InspectedPacket& _p)
  {
    if (!_p.ok)
      return "<malformed or foreign packet>\n";
    std::string out = (_p.kind == PacketKind::Unreliable ? "NMSG" : "NRLB");
    out += " lane=";    out += LaneName(_p.lane);
    out += " records="; out += std::to_string(_p.records.size());
    out += '\n';
    for (const InspectedRecord& r : _p.records)
    {
      out += "  ";        out += HexId(r.id);
      out += "  ";        out += r.name;
      out += "  len=";    out += std::to_string(r.length);
      out += '\n';
    }
    return out;
  }
}
