#pragma once

// Travel - the travel protocol: hyperspace jumps and the in-system jump drive
// (NeuronCore).
//
// Travel used to ride the station protocol (StationRequestKind::Teleport /
// JumpDrive intercepted before the station dispatcher), which made the message
// name lie about its handler and mixed commerce outcomes with travel outcomes
// (ARCHITECTURE.md 13.1-S3). These messages split it out: the client asks to
// travel, the server validates fuel/range/mass-lock through HyperspaceSystem and
// replies with what happened. The station protocol is docking + commerce again.
//
// Data/protocol only; the validation and the movement live server-side
// (GameLogic/HyperspaceSystem.h).

#include <cstdint>
#include <tuple>

#include "Messages/Registry.h"   // MessageId/Traits/Serialize + REGISTER_MESSAGE

namespace Neuron::Msg
{
  enum class TravelKind : uint8_t
  {
    Hyperspace = 1,     // fuel-gated jump to another system (may misfire)
    InSystemJump = 2,   // the jump drive: a fast hop toward the planet
    PoiJump = 3,        // targeted in-system jump to a scene POI (scene.md 3.6)
  };

  enum class TravelStatus : uint8_t
  {
    Arrived = 0,         // hyperspace success: relocated near the destination, in flight
    Witchspace = 1,      // misjump: fuel spent, flung into a Thargoid ambush
    Jumped = 2,          // in-system jump succeeded
    NotEnoughFuel = 3,   // the jump costs more fuel than the tank holds
    OutOfRange = 4,      // the destination is beyond even a full tank
    UnknownSystem = 5,   // no such destination system
    MassLocked = 6,      // an in-system jump is blocked by nearby mass
    Rejected = 7,        // not travel-capable / nothing to jump toward
    UnknownPoi = 8,      // a PoiJump named a POI that isn't in this system
  };

  // client -> server: take me somewhere. `systemId` is the destination system
  // for a hyperspace jump (ignored by the in-system jump drive).
  struct TravelRequest
  {
    static constexpr MessageId    Id    = static_cast<MessageId>(0x1000);   // game-specific band
    static constexpr MessageScope Scope = MessageScope::Wire;
    static constexpr MessageKind  Kind  = MessageKind::Command;
    static constexpr MessageLane  Lane  = MessageLane::Gameplay;
    static constexpr Direction    Dir   = Direction::ClientToServer;

    TravelKind kind = TravelKind::Hyperspace;
    uint32_t systemId = 0;
    uint32_t poiId = 0;   // PoiJump: the destination POI (ignored by the other kinds)

    auto Fields()       { return std::tie(kind, systemId, poiId); }
    auto Fields() const { return std::tie(kind, systemId, poiId); }
  };

  // server -> client: what the travel attempt did. Position/fuel changes ride
  // the snapshot stream and PlayerStatus as always; this is the outcome fact.
  struct TravelResponse
  {
    static constexpr MessageId    Id    = static_cast<MessageId>(0x1001);
    static constexpr MessageScope Scope = MessageScope::Wire;
    static constexpr MessageKind  Kind  = MessageKind::Event;
    static constexpr MessageLane  Lane  = MessageLane::Gameplay;
    static constexpr Direction    Dir   = Direction::ServerToClient;

    TravelKind kind = TravelKind::Hyperspace;
    TravelStatus status = TravelStatus::Rejected;

    auto Fields()       { return std::tie(kind, status); }
    auto Fields() const { return std::tie(kind, status); }
  };
}

REGISTER_MESSAGE(TravelRequest);
REGISTER_MESSAGE(TravelResponse);
