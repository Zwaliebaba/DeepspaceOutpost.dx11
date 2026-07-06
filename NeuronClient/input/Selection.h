#pragma once

// Selection - the pure, CI-testable selected-unit set behind Track H2 of the
// Homeworld-style input migration (input.md §3 H2). It decouples "what the
// player has selected" from the single g_missile_lock_target global that used
// to mean selection, orbit subject, reticle and missile target all at once
// (input.md §1.3, coupling C1).
//
// It carries NO rendering, NO input, NO DirectXMath - the client glue feeds it
// picked entity ids and an ownership predicate and reads back the set. It holds
// a small fixed-capacity set (band-select is multi-unit-ready even though only
// the primary ship is an own unit pre-F1), so it is allocation-free and the
// edge cases (dedupe, capacity, own-unit filtering) are pinned by the headless
// suite rather than verified only in-app.

#include <array>
#include <cstddef>
#include <cstdint>

namespace Neuron::Input
{
  // The sentinel a pick returns for "nothing" (mirrors the client's 0xFFFFFFFF).
  inline constexpr uint32_t SELECTION_NONE = 0xFFFFFFFFu;

  // The most units a single selection can hold. One is enough today (the primary
  // ship is the only own unit); the band mechanism fills more once escorts and
  // ownership replication land (input.md §2.4), so the cap ships ready.
  inline constexpr std::size_t MAX_SELECTED = 32;

  class Selection
  {
  public:
    // Replace the whole set with a single entity (a plain click). SELECTION_NONE
    // clears. The commanded PRIMARY becomes this entity.
    void Set(uint32_t _id)
    {
      m_count = 0;
      if (_id != SELECTION_NONE)
        m_ids[m_count++] = _id;
    }

    // Add an entity to the set (Shift+click / band accumulation), deduped and
    // capacity-bounded. SELECTION_NONE and duplicates are ignored.
    void Add(uint32_t _id)
    {
      if (_id == SELECTION_NONE || Contains(_id) || m_count >= MAX_SELECTED)
        return;
      m_ids[m_count++] = _id;
    }

    void Clear() { m_count = 0; }

    [[nodiscard]] bool Empty() const { return m_count == 0; }
    [[nodiscard]] std::size_t Count() const { return m_count; }

    // The PRIMARY selected entity - the one an order commands and the camera
    // focuses - or SELECTION_NONE when the set is empty. The first entity added.
    [[nodiscard]] uint32_t Primary() const
    {
      return m_count > 0 ? m_ids[0] : SELECTION_NONE;
    }

    [[nodiscard]] uint32_t At(std::size_t _i) const
    {
      return _i < m_count ? m_ids[_i] : SELECTION_NONE;
    }

    [[nodiscard]] bool Contains(uint32_t _id) const
    {
      for (std::size_t i = 0; i < m_count; ++i)
        if (m_ids[i] == _id) return true;
      return false;
    }

    // True when the set holds at least one entity the predicate calls an own
    // unit - the gate for whether the command grammar (orders / grid) applies.
    // `_isOwnUnit(id)` is the client's ownership join (today: id == LocalPlayer).
    template <typename IsOwnUnit>
    [[nodiscard]] bool ContainsOwn(IsOwnUnit _isOwnUnit) const
    {
      for (std::size_t i = 0; i < m_count; ++i)
        if (_isOwnUnit(m_ids[i])) return true;
      return false;
    }

    // Drop an entity that left the world (death / despawn); keeps the order
    // stable so Primary() only changes when the primary itself is removed.
    void Remove(uint32_t _id)
    {
      std::size_t w = 0;
      for (std::size_t r = 0; r < m_count; ++r)
        if (m_ids[r] != _id)
          m_ids[w++] = m_ids[r];
      m_count = w;
    }

  private:
    std::array<uint32_t, MAX_SELECTED> m_ids{};
    std::size_t                        m_count = 0;
  };
}
