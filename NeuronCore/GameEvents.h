#pragma once

// GameEvents - the concrete events carried over the ReliableChannel (NeuronCore).
//
// Shared data/protocol only: the event TYPE tags and their payload layout, so
// client and server encode/decode them identically. These are the things a
// snapshot can never express by superseding state - an entity blinking out, a
// kill, a line of chat - so they ride the reliable channel exactly once and in
// order. Behaviour (what a despawn DOES) lives on each side separately.

#include <cstdint>
#include <string>
#include <vector>

#include "DataWriter.h"
#include "DataReader.h"
#include "ReliableChannel.h"

namespace Neuron::Net
{
  enum class EventType : uint16_t
  {
    EntityDespawn = 1,   // an entity left the world (the thing absence can't convey)
    EntityDeath = 2,     // a kill: victim + killer
    Chat = 3,            // a chat line: sender + text
    AssignPlayer = 4,    // "you control entity N" - the connect handshake reply
    StationRequest = 5,  // client -> server: dock/undock/buy/sell
    StationResponse = 6, // server -> client: the authoritative result
  };

  // --- Assign player (connect handshake) ------------------------------------

  [[nodiscard]] inline std::vector<uint8_t> EncodeAssignPlayer(uint32_t _entityId)
  {
    DataWriter w;
    w.WriteU32(_entityId);
    return w.Bytes();
  }

  [[nodiscard]] inline bool DecodeAssignPlayer(const ReliableMessage& _m, uint32_t& _entityId)
  {
    if (_m.type != static_cast<uint16_t>(EventType::AssignPlayer))
      return false;
    DataReader r(_m.payload.data(), _m.payload.size());
    _entityId = r.ReadU32();
    return r.Ok();
  }

  // --- Entity despawn -------------------------------------------------------

  [[nodiscard]] inline std::vector<uint8_t> EncodeDespawn(uint32_t _entityId)
  {
    DataWriter w;
    w.WriteU32(_entityId);
    return w.Bytes();
  }

  [[nodiscard]] inline bool DecodeDespawn(const ReliableMessage& _m, uint32_t& _entityId)
  {
    if (_m.type != static_cast<uint16_t>(EventType::EntityDespawn))
      return false;
    DataReader r(_m.payload.data(), _m.payload.size());
    _entityId = r.ReadU32();
    return r.Ok();
  }

  // --- Entity death ---------------------------------------------------------

  [[nodiscard]] inline std::vector<uint8_t> EncodeDeath(uint32_t _victimId, uint32_t _killerId)
  {
    DataWriter w;
    w.WriteU32(_victimId);
    w.WriteU32(_killerId);
    return w.Bytes();
  }

  [[nodiscard]] inline bool DecodeDeath(const ReliableMessage& _m, uint32_t& _victimId, uint32_t& _killerId)
  {
    if (_m.type != static_cast<uint16_t>(EventType::EntityDeath))
      return false;
    DataReader r(_m.payload.data(), _m.payload.size());
    _victimId = r.ReadU32();
    _killerId = r.ReadU32();
    return r.Ok();
  }

  // --- Chat -----------------------------------------------------------------

  [[nodiscard]] inline std::vector<uint8_t> EncodeChat(uint32_t _senderId, const std::string& _text)
  {
    DataWriter w;
    w.WriteU32(_senderId);
    w.WriteU16(static_cast<uint16_t>(_text.size()));
    for (char c : _text)
      w.WriteU8(static_cast<uint8_t>(c));
    return w.Bytes();
  }

  [[nodiscard]] inline bool DecodeChat(const ReliableMessage& _m, uint32_t& _senderId, std::string& _text)
  {
    if (_m.type != static_cast<uint16_t>(EventType::Chat))
      return false;
    DataReader r(_m.payload.data(), _m.payload.size());
    _senderId = r.ReadU32();
    const uint16_t len = r.ReadU16();
    _text.clear();
    _text.reserve(len);
    for (uint16_t i = 0; i < len; ++i)
      _text.push_back(static_cast<char>(r.ReadU8()));
    return r.Ok();
  }

  // Convenience: queue a typed event onto a channel.
  inline uint32_t SendDespawn(ReliableChannel& _ch, uint32_t _entityId)
  {
    return _ch.Send(static_cast<uint16_t>(EventType::EntityDespawn), EncodeDespawn(_entityId));
  }

  inline uint32_t SendDeath(ReliableChannel& _ch, uint32_t _victimId, uint32_t _killerId)
  {
    return _ch.Send(static_cast<uint16_t>(EventType::EntityDeath), EncodeDeath(_victimId, _killerId));
  }

  inline uint32_t SendChat(ReliableChannel& _ch, uint32_t _senderId, const std::string& _text)
  {
    return _ch.Send(static_cast<uint16_t>(EventType::Chat), EncodeChat(_senderId, _text));
  }

  inline uint32_t SendAssignPlayer(ReliableChannel& _ch, uint32_t _entityId)
  {
    return _ch.Send(static_cast<uint16_t>(EventType::AssignPlayer), EncodeAssignPlayer(_entityId));
  }
}
