#pragma once

// TransformHistory - a short per-entity ring of past transforms for lag
// compensation (GameLogic, server-side, Track E1).
//
// Each tick the server records where every combat-relevant entity (WorldTransform
// + Combatant) IS, into a small fixed ring. When a client fires, the server rewinds
// the candidate targets to where the shooter SAW them - `rtt/2` (network) plus the
// client's render interpolation delay (A3) ago - so a shot aimed at a target's
// rendered position connects. Only the TARGETS are rewound; the shooter stays
// authoritative-current (favour-the-shooter).
//
// This is DERIVED state: it never feeds back into the authoritative simulation, so
// determinism is unaffected - the hit test is a pure read of recorded history. The
// rewind depth is CLAMPED to the ring length, which also bounds how far a client
// that over-reports its RTT (E1a stores the client's self-report) can rewind.
//
// Generation-safe (the C1 OwnershipIndex lesson): each sample stores the full
// entity generation, so a recycled index whose slot still holds an older tenant's
// sample is rejected (the fire path falls back to the live transform).

#include <cstddef>
#include <cstdint>
#include <unordered_map>

#include "ECS.h"
#include "SimComponents.h"   // WorldTransform, Flight, Combatant, Math vectors

namespace Neuron::GameLogic
{
  // How many past ticks of transform we keep. 15 ticks at ~30 Hz is half a second -
  // covers any realistic RTT/2 + interpolation delay, and caps the rewind a
  // misreporting client can force.
  inline constexpr uint32_t LAGCOMP_HISTORY_TICKS = 15;

  // The fixed simulation step in milliseconds (~30 Hz). Used only to convert a
  // latency in ms into a whole number of ticks to rewind; the sim itself is
  // tick-driven, not wall-clock-driven.
  inline constexpr double LAGCOMP_MS_PER_TICK = 1000.0 / 30.0;

  // The client renders ~one snapshot interval in the PAST (A3's InterpolationAlpha),
  // so the shooter saw each target that much later than the raw network delay. One
  // snapshot interval ≈ one tick at the current per-tick snapshot cadence.
  inline constexpr double LAGCOMP_INTERP_DELAY_MS = LAGCOMP_MS_PER_TICK;

  // How many ticks to rewind targets for a shooter whose round-trip time is
  // `_rttMs`: (rtt/2 + interpolation delay) rounded to whole ticks, CLAMPED to the
  // history ring. Pure arithmetic - unit-tested headlessly.
  [[nodiscard]] inline uint32_t LagCompTicks(uint32_t _rttMs)
  {
    const double back = (static_cast<double>(_rttMs) * 0.5 + LAGCOMP_INTERP_DELAY_MS) / LAGCOMP_MS_PER_TICK;
    if (back <= 0.0)
      return 0;
    const uint32_t ticks = static_cast<uint32_t>(back + 0.5);   // round to nearest
    return ticks < LAGCOMP_HISTORY_TICKS ? ticks : (LAGCOMP_HISTORY_TICKS - 1);
  }

  class TransformHistory
  {
  public:
    // Record the current transform of every combat-relevant entity into its ring.
    // Called once per tick, after the simulation has advanced (so the newest entry
    // is the world as it stands going into the next tick's fire resolution).
    void Capture(ECS::Registry& _world)
    {
      _world.Each<WorldTransform, Combatant>([&](ECS::EntityId _id, WorldTransform& _t, Combatant&)
      {
        Ring& ring = m_rings[_id.index];
        Frame& s = ring.slots[ring.head];
        s.generation = _id.generation;
        s.position = _t.position;
        // Nose is stored for future missile/travel validation (A5); the laser cone
        // is measured around the SHOOTER's nose, so it isn't consumed here.
        const Flight* f = _world.TryGet<Flight>(_id);
        s.nose = (f != nullptr) ? f->nose : Math::Vector3d{ 0.0, 0.0, 1.0 };

        ring.head = (ring.head + 1) % LAGCOMP_HISTORY_TICKS;
        if (ring.count < LAGCOMP_HISTORY_TICKS)
          ++ring.count;
      });
    }

    // Where `_id` was `_ticksBack` ticks ago (clamped to the available history).
    // Returns false - leaving the outputs untouched - when there is no history for
    // the entity, or the stored slot belongs to an earlier tenant of a recycled
    // index (generation mismatch); the caller then uses the live transform.
    [[nodiscard]] bool Sample(ECS::EntityId _id, uint32_t _ticksBack,
                              Math::Vector3i64& _outPos, Math::Vector3d& _outNose) const
    {
      const auto it = m_rings.find(_id.index);
      if (it == m_rings.end() || it->second.count == 0)
        return false;

      const Ring& ring = it->second;
      const uint32_t back = (_ticksBack < ring.count) ? _ticksBack : (ring.count - 1);
      // Newest sample sits at head-1; step `back` further into the past, modulo ring.
      const uint32_t idx = (ring.head + LAGCOMP_HISTORY_TICKS - 1 - back) % LAGCOMP_HISTORY_TICKS;
      const Frame& s = ring.slots[idx];
      if (s.generation != _id.generation)
        return false;   // recycled index: the slot is a different entity's past

      _outPos = s.position;
      _outNose = s.nose;
      return true;
    }

    void Clear() { m_rings.clear(); }
    [[nodiscard]] std::size_t TrackedCount() const { return m_rings.size(); }

  private:
    // One captured transform (renamed off "Sample" so it doesn't collide with the
    // public Sample() member function - the method name would hide the type).
    struct Frame
    {
      uint32_t generation = 0;
      Math::Vector3i64 position{};
      Math::Vector3d nose{ 0.0, 0.0, 1.0 };
    };

    struct Ring
    {
      Frame slots[LAGCOMP_HISTORY_TICKS];
      uint32_t head = 0;    // index the NEXT sample goes to; newest is head-1
      uint32_t count = 0;   // valid samples (saturates at LAGCOMP_HISTORY_TICKS)
    };

    std::unordered_map<uint32_t, Ring> m_rings;   // keyed by entity index
  };
}
