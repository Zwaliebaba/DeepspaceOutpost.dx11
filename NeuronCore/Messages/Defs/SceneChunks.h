#pragma once

// SceneChunks - the per-system scene manifest + the mining beam cue (NeuronCore).
//
// A system's scene (its POIs: belts, encounter sites, nav beacons - scene.md) is
// cold chart data: the client needs the area list to show "local areas" and let
// the player target a POI jump, but it does not change per tick. So POIs ship as a
// pulled, chunked manifest on the reliable Bulk lane - the SAME pattern as the
// galaxy manifest (GalaxyChunks.h): the client asks for a system's POIs, the
// server answers with anchors + kinds + positions. Individual belt rocks are NOT
// in here - they are ordinary replicated entities inside AOI.
//
// MiningTick is the discrete beam cue: an AOI-scoped event the server emits when a
// unit extracts ore, so the client draws the mining beam + plays the pickup. The
// extraction already happened server-side; the beam is cosmetic.
//
// Data/protocol only; the scene lives server-side (GameLogic/SceneSystem.h, the
// GameServer scene chunk builder).

#include <cstdint>
#include <tuple>
#include <vector>

#include "Messages/Registry.h"   // MessageId/Traits/Serialize + REGISTER_MESSAGE

namespace Neuron::Msg
{
  // One POI as the chart needs it (a NestedRecord: Fields() but no id/traits - it
  // only travels inside a SceneChunk). `kind` is GameLogic::PoiKind; the client
  // maps it to a glyph + label. Position is the absolute int64 anchor.
  struct ScenePoiEntry
  {
    uint32_t poiId = 0;
    uint8_t kind = 0;
    int64_t x = 0;
    int64_t y = 0;
    int64_t z = 0;
    int32_t radius = 0;

    auto Fields()       { return std::tie(poiId, kind, x, y, z, radius); }
    auto Fields() const { return std::tie(poiId, kind, x, y, z, radius); }
  };

  // client -> server: send me the POIs of system `systemId`. Bulk/reliable.
  struct SceneChunkRequest
  {
    static constexpr MessageId    Id    = static_cast<MessageId>(0x1020);   // game-specific band
    static constexpr MessageScope Scope = MessageScope::Wire;
    static constexpr MessageKind  Kind  = MessageKind::Command;
    static constexpr MessageLane  Lane  = MessageLane::Bulk;
    static constexpr Direction    Dir   = Direction::ClientToServer;

    uint32_t systemId = 0;

    auto Fields()       { return std::tie(systemId); }
    auto Fields() const { return std::tie(systemId); }
  };

  // server -> client: a system's scene. Small (a handful of POIs), so it fits one
  // chunk; `systemId` echoes the request.
  struct SceneChunk
  {
    static constexpr MessageId    Id    = static_cast<MessageId>(0x1021);
    static constexpr MessageScope Scope = MessageScope::Wire;
    static constexpr MessageKind  Kind  = MessageKind::Event;
    static constexpr MessageLane  Lane  = MessageLane::Bulk;
    static constexpr Direction    Dir   = Direction::ServerToClient;

    uint32_t systemId = 0;
    std::vector<ScenePoiEntry> pois;

    auto Fields()       { return std::tie(systemId, pois); }
    auto Fields() const { return std::tie(systemId, pois); }
  };

  // server -> client (AOI-scoped): a mining unit cut `units` of `commodity` off
  // `rock` this cycle - draw the beam + pickup. Cosmetic; the hold already changed
  // (a CargoManifest resend carries the authoritative amount).
  struct MiningTick
  {
    static constexpr MessageId    Id    = static_cast<MessageId>(0x1022);
    static constexpr MessageScope Scope = MessageScope::Wire;
    static constexpr MessageKind  Kind  = MessageKind::Event;
    static constexpr MessageLane  Lane  = MessageLane::Gameplay;
    static constexpr Direction    Dir   = Direction::ServerToClient;

    uint32_t miner = 0;      // the mining unit (entity index)
    uint32_t rock = 0;       // the rock being cut (entity index)
    uint8_t commodity = 0;
    int32_t units = 0;

    auto Fields()       { return std::tie(miner, rock, commodity, units); }
    auto Fields() const { return std::tie(miner, rock, commodity, units); }
  };
}

REGISTER_MESSAGE(SceneChunkRequest);
REGISTER_MESSAGE(SceneChunk);
REGISTER_MESSAGE(MiningTick);
