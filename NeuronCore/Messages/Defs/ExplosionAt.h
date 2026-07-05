#pragma once

// ExplosionAt - a world-anchored kill/VFX cue (NeuronCore, Track G1).
//
// NPC deaths ride EntityDeath, which every client in range already turns into the
// debris pop. A PLAYER death is different: the server sends EntityDeath only to the
// dying player (so nobody else briefly drops the still-alive, respawned hull), then
// teleports it to a station to respawn. Consequence: the KILLER never sees the kill.
// ExplosionAt closes that - broadcast at the victim's death position the instant it
// dies (before the respawn move), so the killer and bystanders see the explosion
// where it happened, decoupled from the victim entity's continued existence.
//
// Data/protocol only: purely a presentation cue (the client plays its existing
// debris VFX world-anchored at x,y,z, sized by scale). It carries an absolute int64
// world point, not an entity id, precisely because the entity lives on (respawned).

#include <cstdint>
#include <tuple>

#include "Messages/Registry.h"   // MessageId/Traits/Serialize + REGISTER_MESSAGE

namespace Neuron::Msg
{
  // server -> client (broadcast): play a debris explosion at an absolute world point.
  struct ExplosionAt
  {
    static constexpr MessageId    Id    = static_cast<MessageId>(0x1005);   // game extension band
    static constexpr MessageScope Scope = MessageScope::Wire;
    static constexpr MessageKind  Kind  = MessageKind::Event;
    static constexpr MessageLane  Lane  = MessageLane::Gameplay;            // reliable, but rare
    static constexpr Direction    Dir   = Direction::ServerToClient;

    int64_t x = 0;        // absolute world position of the blast
    int64_t y = 0;
    int64_t z = 0;
    uint8_t scale = 1;    // relative debris size (1 = a ship-sized pop)
    auto Fields()       { return std::tie(x, y, z, scale); }
    auto Fields() const { return std::tie(x, y, z, scale); }
  };
}

REGISTER_MESSAGE(ExplosionAt);
