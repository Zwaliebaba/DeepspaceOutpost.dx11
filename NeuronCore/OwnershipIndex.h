#pragma once

// OwnershipIndex - the relational "who owns which entities" index (NeuronCore, C).
//
// §12 locks the identity model as Account → Empire → owns N entities, and the
// Darwinia-style trajectory (§13.2.3) needs "all my units" as a CHEAP query - not
// a full-registry scan per player per tick. This is that index: a hash map from an
// owner id (the server's PlayerId today; empires later) to a dense list of the
// entities they own, maintained by the code that grants/revokes ownership (the
// ECS has no component add/remove hooks by design, so maintenance is explicit at
// the few spawn/destroy/grant sites - ServerSessions today).
//
// EntityIds are stored with their GENERATION, so a stale handle from before a
// slot was recycled never matches the new occupant (Remove is a no-op for it,
// and callers validate with Registry::IsValid before using Owned() entries whose
// entity may have died out from under the index).
//
// Deliberately NOT part of Registry itself: ownership is a game-domain relation
// (most entities - NPCs, canisters, missiles - have no owner), and the registry
// stays a pure component container. Header-only, std-only, unit-tested headlessly.

#include <cstddef>
#include <cstdint>
#include <unordered_map>
#include <vector>

#include "ECS.h"

namespace Neuron::ECS
{
  class OwnershipIndex
  {
  public:
    // Register `_entity` as owned by `_owner` (nonzero; 0 means "unowned" and is
    // ignored). Idempotent: re-adding the exact same handle is a no-op, so a
    // grant site can't double-count. The per-owner lists are tiny (a ship, later
    // a handful of drones/outposts), so the dedupe scan is O(mine).
    void Add(uint32_t _owner, EntityId _entity)
    {
      if (_owner == 0)
        return;
      std::vector<EntityId>& owned = m_owned[_owner];
      for (const EntityId e : owned)
        if (e == _entity)
          return;
      owned.push_back(_entity);
    }

    // Unregister one entity. Matches the FULL handle (index + generation): a
    // stale pre-recycle handle does not remove the slot's new occupant. Order is
    // not preserved (swap-remove); an owner whose last entity goes is dropped.
    void Remove(uint32_t _owner, EntityId _entity)
    {
      const auto it = m_owned.find(_owner);
      if (it == m_owned.end())
        return;
      std::vector<EntityId>& owned = it->second;
      for (std::size_t i = 0; i < owned.size(); ++i)
        if (owned[i] == _entity)
        {
          owned[i] = owned.back();
          owned.pop_back();
          break;
        }
      if (owned.empty())
        m_owned.erase(it);
    }

    // Everything `_owner` owns - O(1) to fetch, O(mine) to walk. Entries can be
    // stale if an owned entity was destroyed without a Remove (validate with
    // IsValid); the session teardown path calls Forget() to drop them wholesale.
    [[nodiscard]] const std::vector<EntityId>& Owned(uint32_t _owner) const
    {
      static const std::vector<EntityId> empty;
      const auto it = m_owned.find(_owner);
      return it != m_owned.end() ? it->second : empty;
    }

    // Drop every entry for `_owner` (the owner left the world; the caller has
    // already destroyed - or deliberately released - the entities themselves).
    void Forget(uint32_t _owner) { m_owned.erase(_owner); }

    [[nodiscard]] std::size_t OwnedCount(uint32_t _owner) const
    {
      const auto it = m_owned.find(_owner);
      return it != m_owned.end() ? it->second.size() : 0;
    }

    [[nodiscard]] std::size_t OwnerCount() const { return m_owned.size(); }

    void Clear() { m_owned.clear(); }

  private:
    std::unordered_map<uint32_t, std::vector<EntityId>> m_owned;
  };
}
