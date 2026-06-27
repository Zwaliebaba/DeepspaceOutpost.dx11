#pragma once

// DespawnTracker - detect entities that left the world (GameLogic, server-side).
//
// The unreliable snapshot stream conveys presence by inclusion, but it cannot
// convey ABSENCE: a viewer that stops hearing about an entity cannot tell "it
// despawned" from "its packet was lost" or "it left my area of interest". So a
// real removal must be sent as a reliable event. This tracker diffs the set of
// live entity ids each tick and reports the ones that vanished, which the server
// turns into EntityDespawn events on the ReliableChannel.
//
// Pure (just set arithmetic), so it is unit-tested headlessly.

#include <cstdint>
#include <unordered_set>
#include <vector>

namespace Neuron::GameLogic
{
  class DespawnTracker
  {
  public:
    // Given the entity ids present THIS tick, return those that were present last
    // tick but are now gone. Updates the tracked set in place.
    std::vector<uint32_t> Update(const std::vector<uint32_t>& _currentIds)
    {
      std::unordered_set<uint32_t> current(_currentIds.begin(), _currentIds.end());

      std::vector<uint32_t> despawned;
      for (uint32_t id : m_previous)
      {
        if (current.find(id) == current.end())
          despawned.push_back(id);
      }

      m_previous = std::move(current);
      return despawned;
    }

    [[nodiscard]] std::size_t TrackedCount() const { return m_previous.size(); }

  private:
    std::unordered_set<uint32_t> m_previous;
  };
}
