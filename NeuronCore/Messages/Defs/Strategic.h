#pragma once

// Strategic - the empire-scale summary tier (NeuronCore, Track E3).
//
// The snapshot stream is the TACTICAL clock: entity-level state for what's on
// screen, at tick rate. The strategic tier is the slow clock (§12's decoupled
// clocks): a per-system rollup - how many friendlies and hostiles, and an alert
// level - sent a few times a second for the systems a player has a stake in. The
// client renders it on the galactic chart (and, later, as the iconic-LOD glyphs of
// the H render track, which reuse this same shape) without ever streaming those
// distant entities individually.
//
// Data/protocol only: the server aggregates the counts from the live world at
// strategic cadence (GameLogic StrategicView + the send loop); this carries the
// result. Rides the reliable Gameplay lane - a summary must not be lost, but it is
// rare enough that reliability is cheap.

#include <cstdint>
#include <tuple>

#include "Messages/Registry.h"   // MessageId/Traits/Serialize + REGISTER_MESSAGE

namespace Neuron::Msg
{
  // A system's alert level in a StrategicSummary.
  enum class StrategicAlert : uint8_t
  {
    None        = 0,   // no hostiles detected
    UnderAttack = 1,   // hostiles present in the system
    Lost        = 2,   // (post-F3) an owned station in the system has fallen
  };

  // server -> client: a per-system strategic rollup. Sent for each system the player
  // has presence in (v1: their current system; post-F: owned units/stations too), at
  // the strategic cadence, so the chart shows empire-scale activity without the
  // tactical snapshot stream reaching those systems.
  struct StrategicSummary
  {
    static constexpr MessageId    Id    = static_cast<MessageId>(0x1004);   // game extension band
    static constexpr MessageScope Scope = MessageScope::Wire;
    static constexpr MessageKind  Kind  = MessageKind::Event;
    static constexpr MessageLane  Lane  = MessageLane::Gameplay;
    static constexpr Direction    Dir   = Direction::ServerToClient;

    uint32_t systemId = 0;        // the galaxy system this summarizes
    uint16_t friendlyCount = 0;   // player-faction combatants in the system
    uint16_t hostileCount = 0;    // pirate/hostile combatants in the system
    uint8_t  alert = 0;           // a StrategicAlert
    auto Fields()       { return std::tie(systemId, friendlyCount, hostileCount, alert); }
    auto Fields() const { return std::tie(systemId, friendlyCount, hostileCount, alert); }
  };
}

REGISTER_MESSAGE(StrategicSummary);
