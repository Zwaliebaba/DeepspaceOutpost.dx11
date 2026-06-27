#pragma once

// ReliableChannel - sequenced, acked, ordered messages over UDP (NeuronCore).
//
// The minority of traffic that CANNOT be superseded by a later snapshot -
// despawns, deaths, chat, inventory grants - needs to arrive exactly once and in
// order. This is a small reliable-ordered layer built on the same raw UDP datagram
// pipe as the (unreliable) snapshot stream, distinguished on the wire by its own
// magic. It is deliberately TCP-like at the message level but stays on UDP so it
// shares one socket/port with replication and never head-of-line-blocks it.
//
// Model (full duplex, one instance per peer):
//   * Send() queues an outgoing message and assigns it a monotonically increasing
//     sequence. It stays in the unacked set and is RESENT in every packet until
//     the peer acknowledges it.
//   * WritePacket() emits our cumulative ack (the highest contiguous sequence we
//     have received) followed by every unacked outgoing message that fits the MTU.
//   * ReadPacket() drops our outgoing messages the peer has cumulatively acked,
//     then delivers their messages in order: the expected sequence is handed out
//     immediately, a gap buffers later sequences until it is filled, and a
//     duplicate/old sequence is ignored.
//
// Pure and deterministic (no sockets here - just bytes in, bytes out), so loss,
// reordering and duplication are all driven directly in unit tests.

#include <cstdint>
#include <cstddef>
#include <deque>
#include <map>
#include <vector>

#include "DataWriter.h"
#include "DataReader.h"
#include "Replication.h"   // SAFE_UDP_PAYLOAD

namespace Neuron::Net
{
  inline constexpr uint32_t EVENT_MAGIC = 0x4E455654;   // 'NEVT'
  inline constexpr uint16_t EVENT_VERSION = 1;

  // A reliable message: an application-defined type tag plus an opaque payload
  // (encoded by the game layer, e.g. GameEvents.h).
  struct ReliableMessage
  {
    uint16_t type = 0;
    std::vector<uint8_t> payload;
  };

  class ReliableChannel
  {
  public:
    // Queue an outgoing reliable message; returns its assigned sequence number.
    uint32_t Send(uint16_t _type, const uint8_t* _data, std::size_t _len)
    {
      const uint32_t seq = m_nextSeq++;
      ReliableMessage m;
      m.type = _type;
      m.payload.assign(_data, _data + _len);
      m_unacked.emplace(seq, std::move(m));
      return seq;
    }

    uint32_t Send(uint16_t _type, const std::vector<uint8_t>& _payload)
    {
      return Send(_type, _payload.data(), _payload.size());
    }

    // Serialize a packet to transmit: our cumulative ack, then as many unacked
    // outgoing messages as fit `_maxPayload` (always at least one, so a single
    // oversized message still makes progress). Unacked messages reappear in the
    // next packet until the peer acks them.
    [[nodiscard]] std::vector<uint8_t> WritePacket(std::size_t _maxPayload = SAFE_UDP_PAYLOAD)
    {
      DataWriter w;
      w.WriteU32(EVENT_MAGIC);
      w.WriteU16(EVENT_VERSION);
      w.WriteU32(LocalAck());

      // Choose the messages that fit (header so far is magic+ver+ack+count).
      std::size_t used = 4 + 2 + 4 + 2;
      std::vector<const std::pair<const uint32_t, ReliableMessage>*> chosen;
      for (const auto& kv : m_unacked)
      {
        const std::size_t msgSize = 4 + 2 + 2 + kv.second.payload.size();
        if (!chosen.empty() && used + msgSize > _maxPayload)
          break;
        chosen.push_back(&kv);
        used += msgSize;
      }

      w.WriteU16(static_cast<uint16_t>(chosen.size()));
      for (const auto* kv : chosen)
      {
        w.WriteU32(kv->first);
        w.WriteU16(kv->second.type);
        w.WriteU16(static_cast<uint16_t>(kv->second.payload.size()));
        for (uint8_t b : kv->second.payload)
          w.WriteU8(b);
      }
      return w.Bytes();
    }

    // Parse an incoming packet: apply the peer's cumulative ack (drop our acked
    // outgoing messages), then queue their in-order messages for delivery.
    // Returns false on a malformed/foreign/truncated packet.
    bool ReadPacket(const uint8_t* _data, std::size_t _size)
    {
      DataReader r(_data, _size);
      if (r.ReadU32() != EVENT_MAGIC)
        return false;
      if (r.ReadU16() != EVENT_VERSION)
        return false;

      const uint32_t theirAck = r.ReadU32();
      for (auto it = m_unacked.begin(); it != m_unacked.end();)
      {
        if (it->first <= theirAck)
          it = m_unacked.erase(it);   // map is ordered by seq
        else
          break;
      }

      const uint16_t count = r.ReadU16();
      for (uint16_t i = 0; i < count; ++i)
      {
        const uint32_t seq = r.ReadU32();
        ReliableMessage m;
        m.type = r.ReadU16();
        const uint16_t len = r.ReadU16();
        m.payload.resize(len);
        for (uint16_t b = 0; b < len; ++b)
          m.payload[b] = r.ReadU8();

        if (!r.Ok())
          return false;   // truncated mid-message

        if (seq < m_nextExpected)
          continue;       // duplicate / already delivered

        if (seq == m_nextExpected)
        {
          m_ready.push_back(std::move(m));
          ++m_nextExpected;
          // Flush any buffered sequences now made contiguous.
          for (auto it = m_buffered.find(m_nextExpected);
               it != m_buffered.end();
               it = m_buffered.find(m_nextExpected))
          {
            m_ready.push_back(std::move(it->second));
            m_buffered.erase(it);
            ++m_nextExpected;
          }
        }
        else
        {
          m_buffered.emplace(seq, std::move(m));   // gap: buffer (emplace dedups)
        }
      }
      return r.Ok();
    }

    // Pop the next in-order delivered message. False when none are ready.
    bool Receive(ReliableMessage& _out)
    {
      if (m_ready.empty())
        return false;
      _out = std::move(m_ready.front());
      m_ready.pop_front();
      return true;
    }

    // Cumulative ack we advertise: the highest contiguous sequence received.
    [[nodiscard]] uint32_t LocalAck() const { return m_nextExpected - 1; }
    [[nodiscard]] std::size_t PendingOutgoing() const { return m_unacked.size(); }
    [[nodiscard]] uint32_t NextExpected() const { return m_nextExpected; }
    [[nodiscard]] bool HasReady() const { return !m_ready.empty(); }

  private:
    // Outgoing.
    uint32_t m_nextSeq = 1;                          // seq 0 means "none"
    std::map<uint32_t, ReliableMessage> m_unacked;   // ordered by seq

    // Incoming.
    uint32_t m_nextExpected = 1;
    std::map<uint32_t, ReliableMessage> m_buffered;  // out-of-order, seq -> msg
    std::deque<ReliableMessage> m_ready;             // delivered, awaiting Receive
  };
}
