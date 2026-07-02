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

  // server -> client: one player's roster entry (name + legal status), broadcast to
  // every session on join, on a name change, and on a wanted-level change.
  struct PlayerInfo
  {
    static constexpr MessageId    Id    = static_cast<MessageId>(0x0301);   // player identity
    static constexpr MessageScope Scope = MessageScope::Wire;
    static constexpr MessageKind  Kind  = MessageKind::Event;
    static constexpr MessageLane  Lane  = MessageLane::Gameplay;
    static constexpr Direction    Dir   = Direction::ServerToClient;

    uint32_t entityId = 0;
    std::string name;
    int32_t wantedLevel = 0;
    auto Fields()       { return std::tie(entityId, name, wantedLevel); }
    auto Fields() const { return std::tie(entityId, name, wantedLevel); }
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
    auto Fields()       { return std::tie(energy, frontShield, aftShield, fuel, credits, missiles, cargoUsed, wantedLevel, score); }
    auto Fields() const { return std::tie(energy, frontShield, aftShield, fuel, credits, missiles, cargoUsed, wantedLevel, score); }
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
REGISTER_MESSAGE(PlayerInfo);
REGISTER_MESSAGE(PlayerStatus);
REGISTER_MESSAGE(CargoManifest);
