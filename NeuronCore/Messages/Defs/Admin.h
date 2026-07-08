#pragma once

// Admin - the ServerManager management-channel wire schema (NeuronCore).
//
// The dedicated server, when an admin key is configured, opens a SECOND UDP port
// (the management channel) and speaks these messages over the same tested reliable
// lanes as the game (Msg::MessageEndpoint / ReliableChannel). They let a standalone
// ServerManager app observe a running server - who is connected, what is happening,
// and the server's health - with settings/command management a later extension of
// the same channel (reserved ids below). See sm.md for the full design.
//
// These live in the DEBUG/TOOLING wire range (0x0F00-0x0FFF, MessageId.h): they are
// wire-visible diagnostics, not gameplay. The handshake pair rides the Control lane
// (Scope::Control); the observe-only data messages ride Gameplay (Scope::Wire), and
// are queued on the Bulk lane when REPLAYED to a freshly connected manager so a
// large backlog cannot head-of-line-block live traffic.
//
// v1 is READ-ONLY: the only manager -> server message is the handshake. Every other
// message is server -> manager. Command messages (kick/broadcast/save/settings) are
// reserved at 0x0F10+ and do not change this handshake or identity model.
//
// Data/protocol only (shared schema); the server-side behaviour - authenticate the
// key, keep the event ring, gather roster/health - lives in NeuronServer's
// AdminChannel and the Server host.

#include <cstdint>
#include <string>
#include <tuple>

#include "Messages/Registry.h"   // MessageId/Traits/Serialize + REGISTER_MESSAGE

namespace Neuron::Msg
{
  // The management channel versions with the same wire PROTOCOL_VERSION as the game
  // (defined once in Framing.h): admin messages are ordinary catalog messages, so a
  // mismatched manager is rejected exactly like a mismatched game client.

  // manager -> server: the opening handshake. Carries the wire protocol version and
  // the shared admin secret (the DSO_ADMIN_KEY the server was started with). The
  // datagram's session-token field is 0 (unauthenticated) on this message; the reply
  // hands back the admin token stamped on every subsequent manager datagram. Rides
  // the reliable Control lane, redelivered until the server replies with
  // AdminHelloAck (or AdminHelloReject).
  struct AdminHello
  {
    static constexpr MessageId    Id    = static_cast<MessageId>(0x0F00);   // debug/tooling (wire)
    static constexpr MessageScope Scope = MessageScope::Control;
    static constexpr MessageKind  Kind  = MessageKind::Command;
    static constexpr MessageLane  Lane  = MessageLane::Control;
    static constexpr Direction    Dir   = Direction::ClientToServer;

    uint32_t    protocolVersion = 0;
    std::string adminKey;   // the shared secret; length-capped by the string leaf
    auto Fields()       { return std::tie(protocolVersion, adminKey); }
    auto Fields() const { return std::tie(protocolVersion, adminKey); }
  };

  // Why the server refused an AdminHello (AdminHelloReject.reason).
  enum class AdminRejectReason : uint8_t
  {
    ProtocolMismatch = 1,   // the manager's PROTOCOL_VERSION is not the server's
    BadKey           = 2,   // the admin key did not match
    ServerBusy       = 3,   // the admin-session cap is already reached
  };

  // server -> manager: the handshake was ACCEPTED. Hands the manager its admin token
  // (a CSPRNG identity from SecureRandom64, the same model as a player session token)
  // plus the identity block the manager's header bar shows.
  struct AdminHelloAck
  {
    static constexpr MessageId    Id    = static_cast<MessageId>(0x0F01);   // debug/tooling (wire)
    static constexpr MessageScope Scope = MessageScope::Control;
    static constexpr MessageKind  Kind  = MessageKind::Event;
    static constexpr MessageLane  Lane  = MessageLane::Control;
    static constexpr Direction    Dir   = Direction::ServerToClient;

    uint64_t adminToken = 0;        // CSPRNG identity; stamp on all subsequent datagrams
    uint16_t protocolVersion = 0;   // the server's PROTOCOL_VERSION (echo)
    uint32_t gameLogicVersion = 0;  // GameLogic::Version()
    uint32_t tickRateMs = 0;        // the server's fixed tick period (Cfg::TICK_SLEEP_MS)
    uint32_t serverTick = 0;        // world tick at accept (an uptime baseline)
    uint16_t gamePort = 0;          // the UDP port this server serves the game on
    auto Fields()       { return std::tie(adminToken, protocolVersion, gameLogicVersion, tickRateMs, serverTick, gamePort); }
    auto Fields() const { return std::tie(adminToken, protocolVersion, gameLogicVersion, tickRateMs, serverTick, gamePort); }
  };

  // server -> manager: the handshake was REFUSED (no admin session provisioned). The
  // manager shows a connect error; a bad-key reject also arms the server's per-endpoint
  // failure mute (see AdminChannel).
  struct AdminHelloReject
  {
    static constexpr MessageId    Id    = static_cast<MessageId>(0x0F02);   // debug/tooling (wire)
    static constexpr MessageScope Scope = MessageScope::Control;
    static constexpr MessageKind  Kind  = MessageKind::Event;
    static constexpr MessageLane  Lane  = MessageLane::Control;
    static constexpr Direction    Dir   = Direction::ServerToClient;

    uint8_t reason = 0;   // an AdminRejectReason
    auto Fields()       { return std::tie(reason); }
    auto Fields() const { return std::tie(reason); }
  };

  // How a player is bound to the server right now (AdminPlayerInfo.state).
  enum class AdminPlayerState : uint8_t
  {
    Live    = 0,   // a spawned, controllable session
    Loading = 1,   // version-checked, awaiting a persistence load before spawn (B4)
    Pending = 2,   // a pre-hello shell (received a reliable datagram, no hello yet)
  };

  // server -> manager: one roster row. Sent on join / name change / score change
  // (change-gated by the server), and REPLAYED for every live session when a manager
  // connects. Per-row rather than one roster blob so ReliableChannel's MTU packing
  // does the work and the same message doubles as the on-change delta.
  struct AdminPlayerInfo
  {
    static constexpr MessageId    Id    = static_cast<MessageId>(0x0F03);   // debug/tooling (wire)
    static constexpr MessageScope Scope = MessageScope::Wire;
    static constexpr MessageKind  Kind  = MessageKind::Event;
    static constexpr MessageLane  Lane  = MessageLane::Gameplay;
    static constexpr Direction    Dir   = Direction::ServerToClient;

    uint32_t    playerId = 0;       // the roster key (stable across reconnects)
    uint32_t    entityId = 0;       // the player's primary hull
    std::string name;
    uint32_t    address = 0;        // client IPv4 in host byte order (admin-visible)
    uint16_t    port = 0;
    uint32_t    rttMs = 0;          // the session's reported round-trip time (E1)
    int32_t     score = 0;
    uint8_t     state = 0;          // an AdminPlayerState
    uint32_t    connectedTick = 0;  // world tick the session first went live
    auto Fields()       { return std::tie(playerId, entityId, name, address, port, rttMs, score, state, connectedTick); }
    auto Fields() const { return std::tie(playerId, entityId, name, address, port, rttMs, score, state, connectedTick); }
  };

  // Why a player left (AdminPlayerGone.reason).
  enum class AdminGoneReason : uint8_t
  {
    Disconnected = 0,   // quit or idle-reaped
    Kicked       = 1,   // removed by an admin command (future; reserved)
  };

  // server -> manager: a player left the roster (idle-reaped or disconnected). The
  // manager drops the row.
  struct AdminPlayerGone
  {
    static constexpr MessageId    Id    = static_cast<MessageId>(0x0F04);   // debug/tooling (wire)
    static constexpr MessageScope Scope = MessageScope::Wire;
    static constexpr MessageKind  Kind  = MessageKind::Event;
    static constexpr MessageLane  Lane  = MessageLane::Gameplay;
    static constexpr Direction    Dir   = Direction::ServerToClient;

    uint32_t playerId = 0;
    uint8_t  reason = 0;   // an AdminGoneReason
    auto Fields()       { return std::tie(playerId, reason); }
    auto Fields() const { return std::tie(playerId, reason); }
  };

  // What an event-feed line is about (AdminEvent.kind), for the manager's colouring
  // and filtering. The human-readable line is preformatted server-side in `text`, so
  // the manager needs no game knowledge to render the feed.
  enum class AdminEventKind : uint8_t
  {
    ServerStarted      = 1,
    PlayerJoined       = 2,
    PlayerLeft         = 3,
    PlayerReconnected  = 4,
    Kill               = 5,
    Chat               = 6,
    Crime              = 7,
    TickOverrun        = 8,
  };

  // server -> manager: one event-feed line. `sequence` is the server ring's monotonic
  // counter, so the manager can order the feed and detect the replay -> live seam.
  // Sent live as things happen, and the recent ring is replayed (Bulk lane) on connect.
  struct AdminEvent
  {
    static constexpr MessageId    Id    = static_cast<MessageId>(0x0F05);   // debug/tooling (wire)
    static constexpr MessageScope Scope = MessageScope::Wire;
    static constexpr MessageKind  Kind  = MessageKind::Event;
    static constexpr MessageLane  Lane  = MessageLane::Gameplay;
    static constexpr Direction    Dir   = Direction::ServerToClient;

    uint32_t    sequence = 0;   // the ring's monotonic id
    uint32_t    tick = 0;       // world tick when it happened
    uint8_t     kind = 0;       // an AdminEventKind
    uint32_t    subject = 0;    // playerId or entity index (kind-dependent; 0 = none)
    std::string text;           // preformatted display line
    auto Fields()       { return std::tie(sequence, tick, kind, subject, text); }
    auto Fields() const { return std::tie(sequence, tick, kind, subject, text); }
  };

  // server -> manager: a rolling health sample, one per admin-cadence window (~1 Hz).
  // Mirrors the server's TickSummary (the same numbers behind the console's [metrics]
  // line); the manager renders current values, meters and a short trend.
  struct AdminHealth
  {
    static constexpr MessageId    Id    = static_cast<MessageId>(0x0F06);   // debug/tooling (wire)
    static constexpr MessageScope Scope = MessageScope::Wire;
    static constexpr MessageKind  Kind  = MessageKind::Event;
    static constexpr MessageLane  Lane  = MessageLane::Gameplay;
    static constexpr Direction    Dir   = Direction::ServerToClient;

    uint32_t tick = 0;
    uint32_t uptimeSeconds = 0;
    float    avgTickMs = 0.f;
    float    maxTickMs = 0.f;
    uint32_t overruns = 0;         // cumulative since start
    uint32_t entities = 0;
    uint32_t sessions = 0;
    uint64_t bytesPerSecond = 0;   // game-socket send rate (from TickMetrics)
    uint64_t droppedEntities = 0;  // entities shed by the per-session send budget this window
    auto Fields()       { return std::tie(tick, uptimeSeconds, avgTickMs, maxTickMs, overruns, entities, sessions, bytesPerSecond, droppedEntities); }
    auto Fields() const { return std::tie(tick, uptimeSeconds, avgTickMs, maxTickMs, overruns, entities, sessions, bytesPerSecond, droppedEntities); }
  };

  // Reserved for the future command track (manager -> server, Control lane): kick,
  // broadcast, save, graceful shutdown, get/set settings. They arrive with the same
  // token discipline already in place and do not alter this handshake. Ids 0x0F10+.
}

REGISTER_MESSAGE(AdminHello);
REGISTER_MESSAGE(AdminHelloAck);
REGISTER_MESSAGE(AdminHelloReject);
REGISTER_MESSAGE(AdminPlayerInfo);
REGISTER_MESSAGE(AdminPlayerGone);
REGISTER_MESSAGE(AdminEvent);
REGISTER_MESSAGE(AdminHealth);
