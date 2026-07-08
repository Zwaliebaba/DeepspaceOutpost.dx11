#pragma once

// InputCommand - the client -> server per-frame heartbeat, as a catalog message.
//
// Re-cut to the pure heartbeat/ack (protocol v4, roadmap #6). The pointer-first
// client does not fly the hull (movement is a UnitOrder) and the discrete
// equipment activations ride the reliable AbilityRequest, so the legacy flight
// axes and ability flags are gone from the wire. What remains is exactly what
// must ride the highest-frequency client->server stream:
//   * `sequence`        - self-superseding freshness (latest wins server-side)
//                         and the liveness signal SafeParkSilent watches;
//   * `ackSnapshotTick` - the snapshot-stream ack (E2b) the server's delta
//                         encoder baselines against.
//
// Self-superseding, latest-sequence-wins (the server keeps the highest `sequence`
// and drops stale/duplicate datagrams), so it is sent unreliably and often - it is
// never queued on a reliable lane (a static trait enforces this). Shared protocol
// only: no behaviour here.

#include <cstdint>
#include <tuple>

#include "Messages/Registry.h"   // pulls MessageId/Traits/Serialize + REGISTER_MESSAGE

namespace Neuron::Msg
{
  // Sentinel: no entity target. Shared by AbilityRequest.target (a missile launch
  // without a lock) and the server-internal FireWeapon commands.
  inline constexpr uint32_t NO_MISSILE_TARGET = 0xFFFFFFFFu;

  struct InputCommand
  {
    static constexpr MessageId    Id    = static_cast<MessageId>(0x0100);   // input range
    static constexpr MessageScope Scope = MessageScope::Wire;
    static constexpr MessageKind  Kind  = MessageKind::Command;
    static constexpr MessageLane  Lane  = MessageLane::Unreliable;
    static constexpr Direction    Dir   = Direction::ClientToServer;

    uint32_t sequence = 0;          // monotonically increasing; latest wins on the server
    uint32_t ackSnapshotTick = 0;   // latest snapshot tick held as a complete baseline (0 = none yet)

    auto Fields()       { return std::tie(sequence, ackSnapshotTick); }
    auto Fields() const { return std::tie(sequence, ackSnapshotTick); }
  };
}

REGISTER_MESSAGE(InputCommand);
