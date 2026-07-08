#pragma once

// UnitOrder - the pointer-first command protocol (NeuronCore, Track I / #22).
//
// The interaction redesign (docs/interaction.md) makes the game order-driven: the
// player never pilots a hull directly, they SELECT a unit they own and ORDER it
// (move here, approach that, dock, attack, collect). This restores the movement
// verb the free-camera migration retired - the ship's flight axes are always zero
// now (main.cpp), so an order is the only way a hull moves.
//
// Wire/Command on the RELIABLE Gameplay lane (a lost datagram must not eat a
// command, unlike the unreliable InputCommand heartbeat), C->S. The server
// validates ownership + target legality + range (ServerSessions / GameServer),
// records an ActiveOrder component, and the pure GameLogic OrderSystem translates
// it into a FlightIntent every tick through the SAME steering the NPC autopilot
// uses - ordered flight stays clamped by the ship's FlightCaps, so a client can no
// more out-fly its hull by ordering than by piloting. The outcome comes back as a
// UnitOrderAck.
//
// AbilityRequest is the discrete-action sibling: the G8 equipment activations
// (missile / ECM / energy bomb / escape pod) that used to ride the unreliable
// InputCommand flags move here onto the reliable lane, so a button press is never
// lost. The held laser is NOT an ability - it is the Attack order's engagement.
//
// Data/protocol only; validation and execution live server-side (GameLogic/
// OrderSystem.h + the GameServer order handlers).

#include <cstdint>
#include <tuple>

#include "Messages/Registry.h"   // MessageId/Traits/Serialize + REGISTER_MESSAGE

namespace Neuron::Msg
{
  // What a unit is ordered to do. Stop=1 so a zero/uninitialised order is never a
  // valid command. Patrol/Route are reserved for the F-track (living economy /
  // standing patrols) - allocated here so their values are permanent, rejected by
  // the validator until they land.
  enum class OrderKind : uint8_t
  {
    Stop = 1,       // hold station: level off, cut throttle
    Move = 2,       // fly to targetPos (a fixed world point), then hold
    Approach = 3,   // fly to `target` entity's CURRENT position, then hold
    Dock = 4,       // fly to `target` station and dock when in range
    Attack = 5,     // pursue + engage `target` combatant (fire on the order)
    Collect = 6,    // fly to `target` cargo canister; the scoop picks it up
    Escort = 7,     // follow `target` (F1: the owner's ship); reserved semantics
    Patrol = 8,     // reserved (F-track): patrol a point/region
    Route = 9,      // reserved (F-track): run a trade route
    Mine = 10,      // fly to `target` rock/belt anchor and cut ore (scene.md 3.7)
  };

  // How the server received a UnitOrder. Accepted = the order is now active;
  // everything else means nothing changed (the client clears its optimistic marker).
  enum class OrderStatus : uint8_t
  {
    Accepted = 0,    // recorded as the unit's active order
    NotYours = 1,    // the unit is not owned by this player
    BadTarget = 2,   // the target is dead / wrong type for this order
    Illegal = 3,     // the order is not (yet) supported (reserved kinds)
    OutOfRange = 4,  // reserved: the target/point is beyond an allowed distance
    Docked = 5,      // the unit is docked and cannot take a flight order
    Rejected = 6,    // catch-all refusal
    HoldFull = 7,    // a Mine order needs cargo room the hold does not have
    NoGear = 8,      // a Mine order needs a mining laser the unit is not carrying
  };

  // client -> server: order a unit you own. `target` is the entity the order acts
  // on (Approach/Dock/Attack/Collect/Escort); `targetX/Y/Z` are the destination
  // point (Move). Unused fields are ignored per order kind. Latest order wins.
  struct UnitOrder
  {
    static constexpr MessageId    Id    = static_cast<MessageId>(0x1010);   // game-specific band
    static constexpr MessageScope Scope = MessageScope::Wire;
    static constexpr MessageKind  Kind  = MessageKind::Command;
    static constexpr MessageLane  Lane  = MessageLane::Gameplay;            // reliable
    static constexpr Direction    Dir   = Direction::ClientToServer;

    uint32_t unitId = 0;                       // the ordered unit (entity index)
    OrderKind order = OrderKind::Stop;
    uint32_t target = 0xFFFFFFFFu;             // target entity index, or sentinel
    int64_t targetX = 0;
    int64_t targetY = 0;
    int64_t targetZ = 0;

    auto Fields()       { return std::tie(unitId, order, target, targetX, targetY, targetZ); }
    auto Fields() const { return std::tie(unitId, order, target, targetX, targetY, targetZ); }
  };

  // server -> client (the ordering owner): what happened to a UnitOrder. The
  // client surfaces a non-Accepted status as a rejection toast (I3).
  struct UnitOrderAck
  {
    static constexpr MessageId    Id    = static_cast<MessageId>(0x1011);
    static constexpr MessageScope Scope = MessageScope::Wire;
    static constexpr MessageKind  Kind  = MessageKind::Event;
    static constexpr MessageLane  Lane  = MessageLane::Gameplay;            // reliable
    static constexpr Direction    Dir   = Direction::ServerToClient;

    uint32_t unitId = 0;
    OrderKind order = OrderKind::Stop;
    OrderStatus status = OrderStatus::Rejected;

    auto Fields()       { return std::tie(unitId, order, status); }
    auto Fields() const { return std::tie(unitId, order, status); }
  };

  // A discrete equipment activation. Replaces the unreliable InputCommand fire
  // flags: on the reliable lane a button press is never dropped. `target` is the
  // missile lock (FireMissile) and ignored by the others.
  enum class AbilityKind : uint8_t
  {
    FireMissile = 1,
    Ecm = 2,
    EnergyBomb = 3,
    EscapePod = 4,
  };

  struct AbilityRequest
  {
    static constexpr MessageId    Id    = static_cast<MessageId>(0x1014);
    static constexpr MessageScope Scope = MessageScope::Wire;
    static constexpr MessageKind  Kind  = MessageKind::Command;
    static constexpr MessageLane  Lane  = MessageLane::Gameplay;            // reliable
    static constexpr Direction    Dir   = Direction::ClientToServer;

    AbilityKind kind = AbilityKind::FireMissile;
    uint32_t target = 0xFFFFFFFFu;   // FireMissile: the locked target index

    auto Fields()       { return std::tie(kind, target); }
    auto Fields() const { return std::tie(kind, target); }
  };
}

REGISTER_MESSAGE(UnitOrder);
REGISTER_MESSAGE(UnitOrderAck);
REGISTER_MESSAGE(AbilityRequest);
