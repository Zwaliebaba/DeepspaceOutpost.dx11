#pragma once

// Reliable - send/decode catalog messages over a ReliableChannel (NeuronCore).
//
// The reliable lanes still use the existing ReliableChannel transport (seq/ack/
// ordered); a catalog message rides it as the channel's (type, payload) pair,
// where the type tag is the message's MessageId and the payload is its generic
// Serialize encoding. These two helpers are the reliable-lane analogue of the
// unreliable lane's PacketWriter/DecodeRecord, so call sites never hand-encode.
//
// (The physical Control/Gameplay/Bulk channel split is a later step; these helpers
// are independent of how many channels exist.)

#include <cstdint>

#include "ReliableChannel.h"   // Net::ReliableChannel, Net::ReliableMessage
#include "MessageId.h"
#include "Serialize.h"

namespace Neuron::Msg
{
  // Queue a catalog message on a reliable channel; returns its assigned sequence.
  template <Message M>
  uint32_t SendReliable(Net::ReliableChannel& _ch, const M& _m)
  {
    return _ch.Send(Raw(M::Id), Encode(_m));
  }

  // Decode a delivered reliable message into M, but only if its id matches (so a
  // caller can try several types in turn, exactly like the old Decode* helpers).
  template <Message M>
  [[nodiscard]] bool TryDecode(const Net::ReliableMessage& _rm, M& _out)
  {
    if (_rm.type != Raw(M::Id))
      return false;
    Net::DataReader r(_rm.payload.data(), _rm.payload.size());
    return Decode(r, _out);
  }
}
