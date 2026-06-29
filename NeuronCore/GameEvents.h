#pragma once

// GameEvents - the reliable event type tags still carried as raw EventType.
//
// Most reliable events (AssignPlayer, EntityDespawn, EntityDeath, Chat) and the
// station request/response have been folded onto the unified message catalog
// (Messages/Defs/CoreEvents.h, StationProtocol.h) and ride the reliable lanes via
// Msg::SendReliable / Msg::TryDecode. The galaxy manifest is the last hold-out: it
// is chunked, fixed-layout display data and pairs naturally with the Bulk lane, so
// it is folded in the lane-split step. Until then it keeps its own EventType tag.

#include <cstdint>

namespace Neuron::Net
{
  enum class EventType : uint16_t
  {
    GalaxyManifest = 7,  // server -> client: a chunk of the galaxy's system list
  };
}
