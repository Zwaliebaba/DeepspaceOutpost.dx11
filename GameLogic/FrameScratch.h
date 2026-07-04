#pragma once

// FrameScratch - persistent per-tick working-set storage (GameLogic, D2).
//
// Several per-tick systems (StepCollisions, StepCombat, StepAi, StepMissiles,
// ScoopSystem, StepLoot) snapshot a small working set into a local vector/map,
// use it, and discard it before returning - a genuine allocate-then-free every
// tick. FrameScratch bundles that storage in one place so the caller (GameServer)
// can own a single instance and pass it down by reference: the vectors/maps keep
// their allocated capacity across ticks (clear()-not-free), and nothing is a
// hidden global - the owner is explicit and the lifetime is exactly one server
// process.
//
// Every field is cleared by its owning function before use, so reusing the
// storage across ticks changes only WHEN memory is freed, never what a system
// computes - this is a pure allocator optimization, not a behaviour change (see
// the D1 BroadphaseEquivalenceTests, which exercise these systems through their
// default scratch and still assert bit-identical outcomes).
//
// Defined here (not in each system's own header) so the small per-system unit
// structs (CollisionUnit/CombatUnit/LootCan) can be shared without a circular
// include: CollisionSystem.h/CombatSystem.h/LootSystem.h each include THIS
// header, not the other way around. Combatant/LootItem are forward-declared
// since a pointer member doesn't need their complete type here.

#include <cstdint>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "ECS.h"
#include "Vector3i64.h"
#include "Broadphase.h"   // BROADPHASE_CELL, Spatial::Grid (D1's broadphase, reused here)

namespace Neuron::GameLogic
{
  struct Combatant;   // CombatSystem.h
  struct LootItem;    // LootSystem.h

  // StepCollisions' per-tick working entry: a hull snapshot (position + a live
  // pointer into its Combatant) plus whether it's a station hull.
  struct CollisionUnit
  {
    ECS::EntityId id;
    Math::Vector3i64 pos;
    Combatant* c = nullptr;
    bool station = false;
  };

  // StepCombat's per-tick working entry: the same hull snapshot, minus the
  // station flag StepCombat has no use for.
  struct CombatUnit
  {
    ECS::EntityId id;
    Math::Vector3i64 pos;
    Combatant* c = nullptr;
  };

  // ScoopSystem's per-tick canister snapshot (position + a live pointer into its
  // LootItem, nulled out once claimed by a player this tick).
  struct LootCan
  {
    ECS::EntityId id;
    Math::Vector3i64 pos;
    LootItem* loot = nullptr;
  };

  struct FrameScratch
  {
    // StepCollisions. The grid is also persistent (D1 built a fresh one every
    // call): Clear() empties its cells without discarding the outer map's bucket
    // array, so re-inserting this tick's units doesn't have to re-grow it back.
    Spatial::Grid collisionGrid{ BROADPHASE_CELL };
    std::vector<CollisionUnit> collisionUnits;
    std::vector<CollisionUnit> collisionPlanets;
    std::unordered_set<uint32_t> collisionDead;
    std::vector<uint64_t> collisionNearby;

    // StepCombat
    Spatial::Grid combatGrid{ BROADPHASE_CELL };
    std::vector<CombatUnit> combatUnits;
    std::vector<uint64_t> combatNearby;
    std::unordered_map<uint32_t, int> combatDamage;
    std::unordered_map<uint32_t, uint32_t> combatAttacker;
    std::unordered_map<uint32_t, Math::Vector3i64> combatAttackerPos;

    // StepAi
    std::vector<ECS::EntityId> aiPilots;

    // StepMissiles (+ the ECM-pulse out-list GameServer relays alongside it)
    std::vector<ECS::EntityId> missileIds;
    std::vector<uint32_t> ecmPulses;

    // ScoopSystem
    Spatial::Grid scoopGrid{ BROADPHASE_CELL };
    std::vector<LootCan> scoopCans;
    std::vector<uint64_t> scoopNearby;
    std::vector<ECS::EntityId> scoopConsumed;

    // StepLoot
    std::vector<ECS::EntityId> lootExpired;
  };

  namespace Detail
  {
    // Fallback scratch for callers that don't own a FrameScratch - every existing
    // unit test calls these systems without one. A single process-wide instance is
    // safe here: GoogleTest runs its cases sequentially in one thread, and every
    // field is cleared by its owning function before use, so nothing SEMANTIC
    // carries between calls - only allocated capacity is shared. Production code
    // (GameServer) owns and passes its own instance explicitly instead.
    [[nodiscard]] inline FrameScratch& DefaultScratch()
    {
      static FrameScratch s;
      return s;
    }
  }
}
