#pragma once

// MessageEndpoint - a peer's reliable transport, split across lanes (NeuronCore).
//
// One ReliableChannel per reliable lane (Control, Gameplay, Bulk) instead of a
// single shared channel, so a large/cold Bulk payload (the galaxy manifest) cannot
// head-of-line-block a gameplay death or a session-control message: each lane has
// its own sequence/ack space and is delivered independently.
//
// On the wire each lane's datagram is [RELIABLE_MAGIC][lane][token u64] + the lane
// channel's own packet, so ReliableChannel itself is unchanged (it keeps its seq/ack
// framing inside). The token (B2) is the sender's session token, set with SetToken;
// a client stamps the token it learned from HelloAck (0 on the opening hello and on
// server->client packets). The server authenticates by token before routing (see
// PeekReliableToken); OnDatagram itself just skips the field. Send routes a catalog
// message to the channel for its Lane trait; inbound datagrams route by the lane
// byte; Receive drains higher-priority lanes first
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
  inline constexpr std::size_t RELIABLE_HEADER_SIZE = 4 + 1 + 8;   // magic + lane byte + session token

  [[nodiscard]] constexpr bool IsReliableLane(MessageLane _l)
  {
    return _l == MessageLane::Control || _l == MessageLane::Gameplay || _l == MessageLane::Bulk;
  }

  [[nodiscard]] constexpr std::size_t LaneIndex(MessageLane _l) { return static_cast<std::size_t>(_l); }

  // Read the session token from a reliable datagram without routing it (the server
  // authenticates by token before handing the datagram to a session's endpoint).
  // Returns 0 for a foreign/too-short datagram (which is then dropped as untokened).
  [[nodiscard]] inline uint64_t PeekReliableToken(const uint8_t* _data, std::size_t _size)
  {
    if (_data == nullptr || _size < RELIABLE_HEADER_SIZE)
      return 0;
    Net::DataReader r(_data, _size);
    if (r.ReadU32() != RELIABLE_MAGIC)
      return 0;
    r.ReadU8();   // lane
    return r.ReadU64();
  }

  class MessageEndpoint
  {
  public:
    // Queue a catalog message on the channel for its lane; returns the assigned seq.
    template <Message M>
    uint32_t Send(const M& _m)
    {
      static_assert(IsReliableLane(M::Lane), "MessageEndpoint carries reliable lanes only");
      return m_lanes[LaneIndex(M::Lane)].Send(Raw(M::Id), Encode(_m));
    }

    // Queue a pre-encoded payload on an explicit lane (transport-level escape hatch;
    // exercised by the lane tests - every production message is a catalog message).
    uint32_t SendRaw(MessageLane _lane, uint16_t _type, const std::vector<uint8_t>& _payload)
    {
      return m_lanes[LaneIndex(_lane)].Send(_type, _payload);
    }

    // Set the session token stamped on every outgoing datagram (B2). A client sets
    // the token from HelloAck; the server leaves it 0 (the client trusts the server
    // by address). Default 0 = unauthenticated (the opening ClientHello).
    void SetToken(uint64_t _token) { m_token = _token; }

    // Build the outgoing datagrams - one per lane that has anything to (re)send or a
    // new ack to deliver. Each is [RELIABLE_MAGIC][lane][token] + that lane's packet.
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
        w.WriteU64(m_token);
        for (uint8_t b : m_lanes[i].WritePacket(innerMax))
          w.WriteU8(b);
        out.push_back(w.Bytes());
        m_dirty[i] = false;
      }
      return out;
    }

    // Route an inbound reliable datagram to its lane channel. Returns false if it is
    // not one of ours (foreign magic / bad lane / malformed inner packet). The token
    // is skipped here - the server authenticates it before this point (see
    // PeekReliableToken), and the client doesn't authenticate the server by token.
    bool OnDatagram(const uint8_t* _data, std::size_t _size)
    {
      Net::DataReader r(_data, _size);
      if (r.ReadU32() != RELIABLE_MAGIC)
        return false;
      const uint8_t lane = r.ReadU8();
      r.ReadU64();   // session token (already authenticated / not our concern here)
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
    uint64_t m_token = 0;   // session token stamped on outgoing datagrams (B2)
  };
}
