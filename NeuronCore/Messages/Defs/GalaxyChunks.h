#pragma once

// GalaxyChunks - the request-driven galaxy manifest (NeuronCore).
//
// The procedural galaxy is generated server-side (GameLogic, behaviour), but the
// client needs the resulting system list to draw the galactic chart and pick a
// hyperspace destination. The client PULLS it in bounded ranges
// (GalaxyChunkRequest -> GalaxyChunk on the reliable Bulk lane) instead of the
// retired connect-time fire-hose: that bounds the connect burst and is the
// prerequisite for fog-of-war, where the server filters what it returns by what
// the player has explored (ARCHITECTURE.md 13.1-S2, 13.2.3-5).
//
// Replaces the hand-encoded manifest chunk id 0x0210 (GalaxyManifest.h, retired;
// the id stays reserved). These are ordinary catalog messages through the
// generic codec - one serialization path.

#include <cstdint>
#include <cstring>
#include <string>
#include <tuple>
#include <vector>

#include "Messages/Registry.h"   // MessageId/Traits/Serialize + REGISTER_MESSAGE

namespace Neuron::Net
{
  inline constexpr std::size_t GALAXY_NAME_MAX = 12;   // NUL-terminated system name

  // One system as the chart needs it: identity, absolute world position of its
  // planet, and the display attributes. Pure data - the server computes these
  // from GameLogic and ships them; the client only renders them. (This is the
  // client/server-side struct; GalaxySystemEntry below is its wire form.)
  struct GalaxySystemInfo
  {
    uint32_t id = 0;
    int64_t x = 0;
    int64_t y = 0;
    int64_t z = 0;
    char name[GALAXY_NAME_MAX] = {};
    uint8_t government = 0;
    uint8_t economy = 0;
    uint8_t techLevel = 0;
    uint16_t population = 0;
    uint16_t productivity = 0;
  };
}

namespace Neuron::Msg
{
  // The wire form of one system: a NestedRecord (Fields() but no id/traits of its
  // own - it only ever travels inside a GalaxyChunk). The name is a codec-native
  // string.
  struct GalaxySystemEntry
  {
    uint32_t id = 0;
    int64_t x = 0;
    int64_t y = 0;
    int64_t z = 0;
    std::string name;
    uint8_t government = 0;
    uint8_t economy = 0;
    uint8_t techLevel = 0;
    uint16_t population = 0;
    uint16_t productivity = 0;

    auto Fields()       { return std::tie(id, x, y, z, name, government, economy, techLevel, population, productivity); }
    auto Fields() const { return std::tie(id, x, y, z, name, government, economy, techLevel, population, productivity); }
  };

  // Most systems one GalaxyChunk carries: 16 entries at ~48 wire bytes each stay
  // comfortably inside SAFE_UDP_PAYLOAD with the reliable framing around them.
  inline constexpr uint16_t GALAXY_CHUNK_MAX_SYSTEMS = 16;

  // Most entries one GalaxyChunkRequest may ask for (the server clamps harder
  // requests); one request round is then at most a few chunk messages.
  inline constexpr uint16_t GALAXY_CHUNK_MAX_REQUEST = 64;

  // client -> server: send me up to `count` systems starting at `baseIndex`.
  struct GalaxyChunkRequest
  {
    static constexpr MessageId    Id    = static_cast<MessageId>(0x1002);   // game-specific band
    static constexpr MessageScope Scope = MessageScope::Wire;
    static constexpr MessageKind  Kind  = MessageKind::Command;
    static constexpr MessageLane  Lane  = MessageLane::Bulk;
    static constexpr Direction    Dir   = Direction::ClientToServer;

    uint32_t baseIndex = 0;
    uint16_t count = 0;

    auto Fields()       { return std::tie(baseIndex, count); }
    auto Fields() const { return std::tie(baseIndex, count); }
  };

  // server -> client: a slice of the galaxy. `total` lets the client size its
  // chart table and know when it has everything; an out-of-range request is
  // answered with an empty chunk still carrying `total`.
  struct GalaxyChunk
  {
    static constexpr MessageId    Id    = static_cast<MessageId>(0x1003);
    static constexpr MessageScope Scope = MessageScope::Wire;
    static constexpr MessageKind  Kind  = MessageKind::Event;
    static constexpr MessageLane  Lane  = MessageLane::Bulk;
    static constexpr Direction    Dir   = Direction::ServerToClient;

    uint32_t total = 0;
    uint32_t baseIndex = 0;
    std::vector<GalaxySystemEntry> systems;

    auto Fields()       { return std::tie(total, baseIndex, systems); }
    auto Fields() const { return std::tie(total, baseIndex, systems); }
  };

  // --- Wire <-> client/server struct conversion -------------------------------

  [[nodiscard]] inline GalaxySystemEntry ToWireEntry(const Net::GalaxySystemInfo& _s)
  {
    GalaxySystemEntry e;
    e.id = _s.id;
    e.x = _s.x;
    e.y = _s.y;
    e.z = _s.z;
    e.name = _s.name;   // char[] is NUL-terminated by construction
    e.government = _s.government;
    e.economy = _s.economy;
    e.techLevel = _s.techLevel;
    e.population = _s.population;
    e.productivity = _s.productivity;
    return e;
  }

  [[nodiscard]] inline Net::GalaxySystemInfo FromWireEntry(const GalaxySystemEntry& _e)
  {
    Net::GalaxySystemInfo s;
    s.id = _e.id;
    s.x = _e.x;
    s.y = _e.y;
    s.z = _e.z;
    const std::size_t n = _e.name.size() < Net::GALAXY_NAME_MAX - 1 ? _e.name.size()
                                                                    : Net::GALAXY_NAME_MAX - 1;
    std::memcpy(s.name, _e.name.data(), n);
    s.name[n] = '\0';
    s.government = _e.government;
    s.economy = _e.economy;
    s.techLevel = _e.techLevel;
    s.population = _e.population;
    s.productivity = _e.productivity;
    return s;
  }
}

REGISTER_MESSAGE(GalaxyChunkRequest);
REGISTER_MESSAGE(GalaxyChunk);
