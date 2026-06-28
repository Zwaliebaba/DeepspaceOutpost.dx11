#pragma once

// NetEntityId - a generation-stamped entity reference for the wire (NeuronCore).
//
// ECS::EntityId carries a generation, but snapshots and events have historically
// sent the bare slot `index`, which the server resolves with Registry::LiveEntity.
// Under slot recycling a stale index can resolve to a DIFFERENT entity - a latent
// authority hole. A NetEntityId carries the generation the sender last saw, so the
// server can reject a stale/foreign reference before acting on it.
//
// Pure data (no ECS dependency) so it stays in the shared protocol layer; the
// field codec for it lives in Serialize.h.

#include <cstdint>

namespace Neuron::Msg
{
  inline constexpr uint32_t NET_ENTITY_INVALID = 0xFFFFFFFFu;

  struct NetEntityId
  {
    uint32_t index = NET_ENTITY_INVALID;
    uint32_t generation = 0;

    [[nodiscard]] constexpr bool operator==(const NetEntityId& _o) const = default;
    [[nodiscard]] constexpr bool IsNull() const { return index == NET_ENTITY_INVALID; }
  };

  inline constexpr NetEntityId NULL_ENTITY{};
}
