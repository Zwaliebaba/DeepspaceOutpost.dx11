#pragma once

// PlayerSession - player identity, roster and private-status messages (NeuronCore).
//
// Phase G (multiplayer gameplay) shared plumbing. Three catalog messages carry the
// identity/HUD data the thin client can't derive itself:
//
//   ClientHello  - client -> server, the opening handshake: protocol version + the
//                  commander name the player chose. Rides the Control lane like
//                  AssignPlayer (the reply), but the other way.
//   PlayerInfo   - server -> client, one roster entry (entity + name + legal status)
//                  broadcast to everyone. Names/legal status change rarely, so they
//                  ride a reliable event, NOT the hot snapshot stream.
//   PlayerStatus - server -> client, the owning player's own vitals for the HUD
//                  (energy/shields/fuel/credits/...). Sent only to that one session,
//                  on change. Fields for systems not built yet (shields, fuel) are
//                  present so later phases don't force a wire-format change; the
//                  server leaves them zero until those systems land.
//
// Data/protocol only (shared schema); the behaviour - sanitize/de-dupe names,
// decide legal status, populate vitals - lives server-side in GameLogic/Server.

#include <cstdint>
#include <string>
#include <tuple>
#include <vector>

#include "Messages/Registry.h"   // MessageId/Traits/Serialize + REGISTER_MESSAGE

namespace Neuron::Msg
{
  // The handshake carries the wire protocol version, defined once as
  // Neuron::Msg::PROTOCOL_VERSION in Framing.h (do not redefine it here).

  // client -> server: the opening handshake (protocol version + chosen name).
  // Since B1 this is the FRONT DOOR: the server ignores an unknown endpoint until
  // a valid, version-checked ClientHello arrives on the Control lane, and THAT
  // spawns the session (no more spawn-on-first-input). The reply is HelloAck (or
  // HelloReject on a version mismatch).
  struct ClientHello
  {
    static constexpr MessageId    Id    = static_cast<MessageId>(0x0002);   // core/session
    static constexpr MessageScope Scope = MessageScope::Control;
    static constexpr MessageKind  Kind  = MessageKind::Command;
    static constexpr MessageLane  Lane  = MessageLane::Control;
    static constexpr Direction    Dir   = Direction::ClientToServer;

    uint32_t protocolVersion = 0;
    std::string commanderName;
    auto Fields()       { return std::tie(protocolVersion, commanderName); }
    auto Fields() const { return std::tie(protocolVersion, commanderName); }
  };

  // Why the server refused a ClientHello (HelloReject.reason).
  enum class HelloRejectReason : uint8_t
  {
    ProtocolMismatch = 1,   // the client's PROTOCOL_VERSION is not the server's
  };

  // server -> client: the handshake was ACCEPTED. "You are player P, controlling
  // entity N"; the sessionToken is the session's identity (B2) - a CSPRNG token
  // the client stamps on every subsequent datagram, so the server authenticates
  // by token, not by source address. Subsumes and retires AssignPlayer (0x0001,
  // reserved), folding the protocol-version echo and the token into the one
  // handshake reply.
  //
  // The playerId (C) is the §12 identity - Account → Empire → owns N entities:
  // player, not hull. entityId is the PRIMARY controlled entity (further owned
  // units arrive with the F-track). Layout note: this message was extended in
  // place (with a PROTOCOL_VERSION bump) rather than via a successor id, under
  // the owner-approved pre-launch no-back-compat rule; once anything is deployed,
  // layout changes take a NEW id per the §4.3 permanent-ABI discipline.
  struct HelloAck
  {
    static constexpr MessageId    Id    = static_cast<MessageId>(0x0003);   // core/session
    static constexpr MessageScope Scope = MessageScope::Control;
    static constexpr MessageKind  Kind  = MessageKind::Event;
    static constexpr MessageLane  Lane  = MessageLane::Control;
    static constexpr Direction    Dir   = Direction::ServerToClient;

    uint64_t sessionToken = 0;   // the session's CSPRNG identity token (B2)
    uint32_t playerId = 0;       // the player identity (C); stable across reconnects
    uint32_t entityId = 0;       // the PRIMARY entity this player controls
    uint16_t protocolVersion = 0;// the server's PROTOCOL_VERSION (echo)
    auto Fields()       { return std::tie(sessionToken, playerId, entityId, protocolVersion); }
    auto Fields() const { return std::tie(sessionToken, playerId, entityId, protocolVersion); }
  };

  // server -> client: the handshake was REFUSED (no session provisioned). Today the
  // only reason is a protocol-version mismatch; the client shows a connect error.
  struct HelloReject
  {
    static constexpr MessageId    Id    = static_cast<MessageId>(0x0004);   // core/session
    static constexpr MessageScope Scope = MessageScope::Control;
    static constexpr MessageKind  Kind  = MessageKind::Event;
    static constexpr MessageLane  Lane  = MessageLane::Control;
    static constexpr Direction    Dir   = Direction::ServerToClient;

    uint8_t reason = 0;   // a HelloRejectReason
    auto Fields()       { return std::tie(reason); }
    auto Fields() const { return std::tie(reason); }
  };

  // server -> client: one player's roster entry (name + legal status), broadcast to
  // every session on join, on a name change, and on a wanted-level change. Since C
  // the roster is keyed by PLAYER (playerId), not hull - required the moment one
  // player owns two hulls; entityId is their primary ship (what nameplates attach
  // to today). Extended in place under the pre-launch no-back-compat rule (see the
  // HelloAck layout note).
  struct PlayerInfo
  {
    static constexpr MessageId    Id    = static_cast<MessageId>(0x0301);   // player identity
    static constexpr MessageScope Scope = MessageScope::Wire;
    static constexpr MessageKind  Kind  = MessageKind::Event;
    static constexpr MessageLane  Lane  = MessageLane::Gameplay;
    static constexpr Direction    Dir   = Direction::ServerToClient;

    uint32_t playerId = 0;   // the roster's identity key (C)
    uint32_t entityId = 0;   // the player's PRIMARY hull (nameplate anchor)
    std::string name;
    int32_t wantedLevel = 0;
    auto Fields()       { return std::tie(playerId, entityId, name, wantedLevel); }
    auto Fields() const { return std::tie(playerId, entityId, name, wantedLevel); }
  };

  // server -> client (owning session only): the local player's vitals for the HUD.
  // Sent on change. Shields/fuel/score are zero until their systems land (G1/G3/G7).
  struct PlayerStatus
  {
    static constexpr MessageId    Id    = static_cast<MessageId>(0x0302);   // player identity
    static constexpr MessageScope Scope = MessageScope::Wire;
    static constexpr MessageKind  Kind  = MessageKind::Event;
    static constexpr MessageLane  Lane  = MessageLane::Gameplay;
    static constexpr Direction    Dir   = Direction::ServerToClient;

    int32_t energy = 0;
    int32_t frontShield = 0;
    int32_t aftShield = 0;
    int32_t fuel = 0;
    int32_t credits = 0;
    int32_t missiles = 0;
    int32_t cargoUsed = 0;
    int32_t wantedLevel = 0;
    int32_t score = 0;
    int32_t laserTemp = 0;   // laser temperature (G8): overheat blocks firing
    auto Fields()       { return std::tie(energy, frontShield, aftShield, fuel, credits, missiles, cargoUsed, wantedLevel, score, laserTemp); }
    auto Fields() const { return std::tie(energy, frontShield, aftShield, fuel, credits, missiles, cargoUsed, wantedLevel, score, laserTemp); }
  };

  // server -> client (owning session only): the local player's full per-commodity
  // cargo hold. The HUD tracks each commodity's tonnage, which the client can't
  // derive from the aggregate PlayerStatus.cargoUsed - so when scooping loot (or a
  // respawn empties the hold) the server resends the whole manifest. `units[i]` is
  // the held amount of commodity i (0..COMMODITY_COUNT-1).
  struct CargoManifest
  {
    static constexpr MessageId    Id    = static_cast<MessageId>(0x0303);   // player identity
    static constexpr MessageScope Scope = MessageScope::Wire;
    static constexpr MessageKind  Kind  = MessageKind::Event;
    static constexpr MessageLane  Lane  = MessageLane::Gameplay;
    static constexpr Direction    Dir   = Direction::ServerToClient;

    std::vector<int32_t> units;
    auto Fields()       { return std::tie(units); }
    auto Fields() const { return std::tie(units); }
  };
}

REGISTER_MESSAGE(ClientHello);
REGISTER_MESSAGE(HelloAck);
REGISTER_MESSAGE(HelloReject);
REGISTER_MESSAGE(PlayerInfo);
REGISTER_MESSAGE(PlayerStatus);
REGISTER_MESSAGE(CargoManifest);
