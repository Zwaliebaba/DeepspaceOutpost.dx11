#pragma once

// Framing - the 'NMSG' wire envelope and length-prefixed records (NeuronCore).
//
// One envelope replaces the old per-schema magics. A packet is a magic + protocol
// version + lane byte, followed by zero or more records, each a (MessageId,
// payloadLength, payloadBytes) triple. The MANDATORY per-record length lets a
// single reliable packet carry several messages and bounds each decoder to exactly
// its own bytes (so a malformed message can't run a reader into the next record).
//
// Mechanism only: this is the transport binding shared by client and server. The
// reliable lanes (Control/Gameplay/Bulk) each carry their own packet stream over a
// ReliableChannel; the Unreliable lane carries self-superseding datagrams. Higher
// layers decode each record by MessageId and publish it onto the bus.

#include <cstddef>
#include <cstdint>
#include <vector>

#include "DataWriter.h"
#include "DataReader.h"

#include "MessageId.h"
#include "MessageTraits.h"
#include "Serialize.h"

namespace Neuron::Msg
{
  inline constexpr uint32_t MESSAGE_MAGIC = 0x4E4D5347;   // 'NMSG'
  inline constexpr uint16_t PROTOCOL_VERSION = 1;

  // Read the leading magic of a datagram without consuming it (route by protocol).
  [[nodiscard]] inline uint32_t PeekMessageMagic(const uint8_t* _data, std::size_t _size)
  {
    if (_data == nullptr || _size < 4)
      return 0;
    DataReader r(_data, _size);
    return r.ReadU32();
  }

  // Build one packet for a lane, appending typed messages as length-prefixed
  // records. A static_assert (via Encode's Message concept) keeps the payload to a
  // real message; callers only put wire-scoped messages on a wire lane.
  class PacketWriter
  {
  public:
    explicit PacketWriter(MessageLane _lane)
    {
      m_w.WriteU32(MESSAGE_MAGIC);
      m_w.WriteU16(PROTOCOL_VERSION);
      m_w.WriteU8(static_cast<uint8_t>(_lane));
    }

    template <Message M>
    void Add(const M& _m)
    {
      const std::vector<uint8_t> payload = Encode(_m);
      m_w.WriteU16(Raw(M::Id));
      m_w.WriteU16(static_cast<uint16_t>(payload.size()));
      for (uint8_t b : payload)
        m_w.WriteU8(b);
    }

    [[nodiscard]] const std::vector<uint8_t>& Bytes() const { return m_w.Bytes(); }
    [[nodiscard]] std::size_t Size() const { return m_w.Size(); }

  private:
    DataWriter m_w;
  };

  // One decoded record: a message id and its exact payload bytes.
  struct Record
  {
    MessageId id = MessageId::Invalid;
    std::vector<uint8_t> payload;
  };

  struct PacketHeader
  {
    MessageLane lane = MessageLane::Unreliable;
  };

  // Parse a packet into its header and records. Returns false on a foreign/old/
  // truncated packet or a record that claims more bytes than the packet holds, so
  // a malformed datagram is dropped whole rather than partially applied.
  [[nodiscard]] inline bool ReadPacket(const uint8_t* _data, std::size_t _size,
                                       PacketHeader& _hdr, std::vector<Record>& _out)
  {
    DataReader r(_data, _size);
    if (r.ReadU32() != MESSAGE_MAGIC)
      return false;
    if (r.ReadU16() != PROTOCOL_VERSION)
      return false;
    _hdr.lane = static_cast<MessageLane>(r.ReadU8());
    if (!r.Ok())
      return false;

    _out.clear();
    while (r.Remaining() > 0)
    {
      Record rec;
      rec.id = static_cast<MessageId>(r.ReadU16());
      const uint16_t len = r.ReadU16();
      if (!r.Ok())
        return false;
      if (len > r.Remaining())
        return false;   // record overruns the packet
      rec.payload.resize(len);
      for (uint16_t i = 0; i < len; ++i)
        rec.payload[i] = r.ReadU8();
      if (!r.Ok())
        return false;
      _out.push_back(std::move(rec));
    }
    return true;
  }

  // Decode a record into a typed message, checking the id matches first. The reader
  // is bounded to the record's own payload (the length prefix did that).
  template <Message M>
  [[nodiscard]] bool DecodeRecord(const Record& _rec, M& _out)
  {
    if (_rec.id != M::Id)
      return false;
    DataReader r(_rec.payload.data(), _rec.payload.size());
    return Decode(r, _out);
  }
}
