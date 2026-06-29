#pragma once

// CoreEvents - the reliable session/lifecycle/chat events, as catalog messages.
//
// These fold the old GameEvents.h hand-rolled codecs onto the message system: the
// connect handshake (AssignPlayer), the lifecycle facts a snapshot can't express by
// superseding state (EntityDespawn, EntityDeath), and chat. Each is a typed struct
// described once via Fields(); the generic codec produces a payload byte-identical
// to the legacy Encode* (see the parity tests), so this is a framing/ownership
// change, not a wire-format change. They ride the reliable lanes via
// Msg::SendReliable / Msg::TryDecode.
//
// Entity ids stay bare indices here (matching the legacy events and the snapshot
// stream); promoting them to generation-stamped NetEntityId is a separate step.

#include <cstdint>
#include <string>
#include <tuple>

#include "Messages/Registry.h"   // MessageId/Traits/Serialize + REGISTER_MESSAGE

namespace Neuron::Msg
{
  // "You control entity N" - the connect handshake reply (session/control lane).
  struct AssignPlayer
  {
    static constexpr MessageId    Id    = static_cast<MessageId>(0x0001);   // core/session
    static constexpr MessageScope Scope = MessageScope::Control;
    static constexpr MessageKind  Kind  = MessageKind::Event;
    static constexpr MessageLane  Lane  = MessageLane::Control;
    static constexpr Direction    Dir   = Direction::ServerToClient;

    uint32_t entityId = 0;
    auto Fields()       { return std::tie(entityId); }
    auto Fields() const { return std::tie(entityId); }
  };

  // An entity left the world (the thing absence can't convey on the snapshot stream).
  struct EntityDespawn
  {
    static constexpr MessageId    Id    = static_cast<MessageId>(0x0200);   // replication/lifecycle
    static constexpr MessageScope Scope = MessageScope::Wire;
    static constexpr MessageKind  Kind  = MessageKind::Event;
    static constexpr MessageLane  Lane  = MessageLane::Gameplay;
    static constexpr Direction    Dir   = Direction::ServerToClient;

    uint32_t entityId = 0;
    auto Fields()       { return std::tie(entityId); }
    auto Fields() const { return std::tie(entityId); }
  };

  // A kill: victim + killer (entity indices).
  struct EntityDeath
  {
    static constexpr MessageId    Id    = static_cast<MessageId>(0x0201);
    static constexpr MessageScope Scope = MessageScope::Wire;
    static constexpr MessageKind  Kind  = MessageKind::Event;
    static constexpr MessageLane  Lane  = MessageLane::Gameplay;
    static constexpr Direction    Dir   = Direction::ServerToClient;

    uint32_t victim = 0;
    uint32_t killer = 0;
    auto Fields()       { return std::tie(victim, killer); }
    auto Fields() const { return std::tie(victim, killer); }
  };

  // A chat line: sender + UTF-8 text (length-capped by the string leaf).
  struct Chat
  {
    static constexpr MessageId    Id    = static_cast<MessageId>(0x0300);   // chat/social
    static constexpr MessageScope Scope = MessageScope::Wire;
    static constexpr MessageKind  Kind  = MessageKind::Event;
    static constexpr MessageLane  Lane  = MessageLane::Gameplay;
    static constexpr Direction    Dir   = Direction::Both;

    uint32_t sender = 0;
    std::string text;
    auto Fields()       { return std::tie(sender, text); }
    auto Fields() const { return std::tie(sender, text); }
  };
}

REGISTER_MESSAGE(AssignPlayer);
REGISTER_MESSAGE(EntityDespawn);
REGISTER_MESSAGE(EntityDeath);
REGISTER_MESSAGE(Chat);
