#pragma once

// MessageEndpoint - a peer's reliable transport, split across lanes (NeuronCore).
//
// One ReliableChannel per reliable lane (Control, Gameplay, Bulk) instead of a
// single shared channel, so a large/cold Bulk payload (the galaxy manifest) cannot
// head-of-line-block a gameplay death or a session-control message: each lane has
// its own sequence/ack space and is delivered independently.
//
// On the wire each lane's datagram is [RELIABLE_MAGIC][lane] + the lane channel's
// own packet, so ReliableChannel itself is unchanged (it keeps its seq/ack framing
// inside). Send routes a catalog message to the channel for its Lane trait; inbound
// datagrams route by the lane byte; Receive drains higher-priority lanes first
// (Control -> Gameplay -> Bulk). A lane emits a datagram only when it has something
// to (re)send or has newly received a packet to acknowledge, so idle lanes are
// silent.
//
// Header-only, std-only, socket-free (bytes in / bytes out), so loss/reorder and
// lane isolation are all unit-tested headlessly.

#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

#include "DataWriter.h"
#include "DataReader.h"
#include "ReliableChannel.h"

#include "MessageId.h"
#include "MessageTraits.h"
#include "Serialize.h"

namespace Neuron::Msg
{
  inline constexpr uint32_t RELIABLE_MAGIC = 0x4E524C42;   // 'NRLB'
  inline constexpr std::size_t RELIABLE_LANE_COUNT = 3;    // Control(0), Gameplay(1), Bulk(2)
  inline constexpr std::size_t RELIABLE_HEADER_SIZE = 4 + 1;   // magic + lane byte

  [[nodiscard]] constexpr bool IsReliableLane(MessageLane _l)
  {
    return _l == MessageLane::Control || _l == MessageLane::Gameplay || _l == MessageLane::Bulk;
  }

  [[nodiscard]] constexpr std::size_t LaneIndex(MessageLane _l) { return static_cast<std::size_t>(_l); }

  class MessageEndpoint
  {
  public:
    [[nodiscard]] Net::ReliableChannel& Channel(MessageLane _lane) { return m_lanes[LaneIndex(_lane)]; }

    // Queue a catalog message on the channel for its lane; returns the assigned seq.
    template <Message M>
    uint32_t Send(const M& _m)
    {
      static_assert(IsReliableLane(M::Lane), "MessageEndpoint carries reliable lanes only");
      return m_lanes[LaneIndex(M::Lane)].Send(Raw(M::Id), Encode(_m));
    }

    // Queue a pre-encoded payload on an explicit lane (for hand-encoded messages such
    // as the chunked galaxy manifest, which isn't routed through the generic codec).
    uint32_t SendRaw(MessageLane _lane, uint16_t _type, const std::vector<uint8_t>& _payload)
    {
      return m_lanes[LaneIndex(_lane)].Send(_type, _payload);
    }

    // Build the outgoing datagrams - one per lane that has anything to (re)send or a
    // new ack to deliver. Each is [RELIABLE_MAGIC][lane] + that lane's packet.
    [[nodiscard]] std::vector<std::vector<uint8_t>> WriteDatagrams(std::size_t _maxPayload = Net::SAFE_UDP_PAYLOAD)
    {
      std::vector<std::vector<uint8_t>> out;
      const std::size_t innerMax = _maxPayload > RELIABLE_HEADER_SIZE ? _maxPayload - RELIABLE_HEADER_SIZE : 1;
      for (std::size_t i = 0; i < RELIABLE_LANE_COUNT; ++i)
      {
        if (m_lanes[i].PendingOutgoing() == 0 && !m_dirty[i])
          continue;   // nothing to send and nothing new to acknowledge

        Net::DataWriter w;
        w.WriteU32(RELIABLE_MAGIC);
        w.WriteU8(static_cast<uint8_t>(i));
        for (uint8_t b : m_lanes[i].WritePacket(innerMax))
          w.WriteU8(b);
        out.push_back(w.Bytes());
        m_dirty[i] = false;
      }
      return out;
    }

    // Route an inbound reliable datagram to its lane channel. Returns false if it is
    // not one of ours (foreign magic / bad lane / malformed inner packet).
    bool OnDatagram(const uint8_t* _data, std::size_t _size)
    {
      Net::DataReader r(_data, _size);
      if (r.ReadU32() != RELIABLE_MAGIC)
        return false;
      const uint8_t lane = r.ReadU8();
      if (!r.Ok() || lane >= RELIABLE_LANE_COUNT || _size < RELIABLE_HEADER_SIZE)
        return false;
      if (!m_lanes[lane].ReadPacket(_data + RELIABLE_HEADER_SIZE, _size - RELIABLE_HEADER_SIZE))
        return false;
      m_dirty[lane] = true;   // received a packet -> (re)send our ack next WriteDatagrams
      return true;
    }

    // Pop the next delivered message, draining higher-priority lanes first so a
    // control/gameplay message is never stuck behind a bulk backlog.
    bool Receive(Net::ReliableMessage& _out)
    {
      for (std::size_t i = 0; i < RELIABLE_LANE_COUNT; ++i)
        if (m_lanes[i].Receive(_out))
          return true;
      return false;
    }

    [[nodiscard]] bool HasReady() const
    {
      for (const Net::ReliableChannel& ch : m_lanes)
        if (ch.HasReady())
          return true;
      return false;
    }

    [[nodiscard]] std::size_t PendingOutgoing() const
    {
      std::size_t n = 0;
      for (const Net::ReliableChannel& ch : m_lanes)
        n += ch.PendingOutgoing();
      return n;
    }

  private:
    std::array<Net::ReliableChannel, RELIABLE_LANE_COUNT> m_lanes;
    std::array<bool, RELIABLE_LANE_COUNT> m_dirty{};
  };
}
