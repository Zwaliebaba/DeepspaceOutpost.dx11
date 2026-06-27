#pragma once

// Neuron::ECS - the in-house Entity Component System.
//
// This is the foundation the de-globalised Universe is built on (roadmap A2):
// game state stops being loose globals and becomes components on entities,
// owned by a Registry. Storage is sparse-set per component type (cache-friendly
// dense iteration, O(1) lookup); entities are generational handles so stale
// references are detected after a slot is recycled.
//
// Header-only and dependency-free (std only) so it is trivially testable and
// usable from any module without pulling in the heavy NeuronCore.h.

#include <cstddef>
#include <cstdint>
#include <memory>
#include <vector>

namespace Neuron::ECS
{
  inline constexpr uint32_t INVALID_INDEX = 0xFFFFFFFFu;

  // Generational entity handle. `index` addresses the slot; `generation`
  // invalidates handles to a slot that has since been destroyed and recycled.
  struct EntityId
  {
    uint32_t index = INVALID_INDEX;
    uint32_t generation = 0;

    [[nodiscard]] bool operator==(const EntityId& _o) const { return index == _o.index && generation == _o.generation; }
    [[nodiscard]] bool operator!=(const EntityId& _o) const { return !(*this == _o); }
  };

  namespace Detail
  {
    // Process-wide component type ids, assigned on first use (no RTTI).
    inline uint32_t NextComponentTypeId()
    {
      static uint32_t next = 0;
      return next++;
    }
  }

  template <typename T>
  [[nodiscard]] uint32_t ComponentTypeId()
  {
    static const uint32_t id = Detail::NextComponentTypeId();
    return id;
  }

  // Type-erased pool interface so the Registry can hold pools of any component
  // type and remove an entity's components on destruction.
  class IPool
  {
  public:
    virtual ~IPool() = default;
    virtual void Remove(uint32_t _entityIndex) = 0;
    [[nodiscard]] virtual bool Has(uint32_t _entityIndex) const = 0;
  };

  // Sparse-set storage for one component type.
  template <typename T>
  class Pool final : public IPool
  {
  public:
    T& Set(uint32_t _entityIndex, const T& _value)
    {
      if (_entityIndex >= m_sparse.size())
        m_sparse.resize(_entityIndex + 1, INVALID_INDEX);

      const uint32_t slot = m_sparse[_entityIndex];
      if (slot != INVALID_INDEX)
      {
        m_components[slot] = _value;
        return m_components[slot];
      }

      m_sparse[_entityIndex] = static_cast<uint32_t>(m_dense.size());
      m_dense.push_back(_entityIndex);
      m_components.push_back(_value);
      return m_components.back();
    }

    [[nodiscard]] bool Has(uint32_t _entityIndex) const override
    {
      return _entityIndex < m_sparse.size() && m_sparse[_entityIndex] != INVALID_INDEX;
    }

    [[nodiscard]] T* TryGet(uint32_t _entityIndex)
    {
      return Has(_entityIndex) ? &m_components[m_sparse[_entityIndex]] : nullptr;
    }

    void Remove(uint32_t _entityIndex) override
    {
      if (!Has(_entityIndex))
        return;

      // swap-and-pop to keep the dense arrays contiguous
      const uint32_t slot = m_sparse[_entityIndex];
      const uint32_t lastSlot = static_cast<uint32_t>(m_dense.size() - 1);
      const uint32_t lastEntity = m_dense[lastSlot];

      m_components[slot] = m_components[lastSlot];
      m_dense[slot] = lastEntity;
      m_sparse[lastEntity] = slot;

      m_components.pop_back();
      m_dense.pop_back();
      m_sparse[_entityIndex] = INVALID_INDEX;
    }

    // Visit every stored component as (entityIndex, component&).
    template <typename Fn>
    void Each(Fn&& _fn)
    {
      for (std::size_t i = 0; i < m_dense.size(); ++i)
        _fn(m_dense[i], m_components[i]);
    }

    [[nodiscard]] std::size_t Size() const { return m_dense.size(); }

  private:
    std::vector<uint32_t> m_sparse;   // entityIndex -> slot
    std::vector<uint32_t> m_dense;    // slot -> entityIndex
    std::vector<T> m_components;      // slot -> component
  };

  // The ECS world: owns entities and their component pools.
  class Registry
  {
  public:
    [[nodiscard]] EntityId Create()
    {
      uint32_t index;
      if (!m_free.empty())
      {
        index = m_free.back();
        m_free.pop_back();
      }
      else
      {
        index = static_cast<uint32_t>(m_generations.size());
        m_generations.push_back(0);
        m_alive.push_back(false);
      }
      m_alive[index] = true;
      return EntityId{ index, m_generations[index] };
    }

    [[nodiscard]] bool IsValid(EntityId _e) const
    {
      return _e.index < m_generations.size()
          && m_alive[_e.index]
          && m_generations[_e.index] == _e.generation;
    }

    void Destroy(EntityId _e)
    {
      if (!IsValid(_e))
        return;

      for (auto& pool : m_pools)
        if (pool)
          pool->Remove(_e.index);

      m_alive[_e.index] = false;
      ++m_generations[_e.index];
      m_free.push_back(_e.index);
    }

    template <typename T>
    T& Add(EntityId _e, const T& _value = T{})
    {
      return PoolFor<T>().Set(_e.index, _value);
    }

    template <typename T>
    [[nodiscard]] bool Has(EntityId _e) const
    {
      if (!IsValid(_e))
        return false;
      const Pool<T>* p = TryPoolFor<T>();
      return p != nullptr && p->Has(_e.index);
    }

    template <typename T>
    [[nodiscard]] T* TryGet(EntityId _e)
    {
      if (!IsValid(_e))
        return nullptr;
      Pool<T>* p = TryPoolFor<T>();
      return p != nullptr ? p->TryGet(_e.index) : nullptr;
    }

    template <typename T>
    [[nodiscard]] T& Get(EntityId _e)
    {
      return *TryGet<T>(_e);
    }

    template <typename T>
    void Remove(EntityId _e)
    {
      if (!IsValid(_e))
        return;
      if (Pool<T>* p = TryPoolFor<T>())
        p->Remove(_e.index);
    }

    // Visit every entity that has component T as (EntityId, T&).
    template <typename T, typename Fn>
    void Each(Fn&& _fn)
    {
      Pool<T>* p = TryPoolFor<T>();
      if (p == nullptr)
        return;
      p->Each([&](uint32_t _idx, T& _c)
      {
        _fn(EntityId{ _idx, m_generations[_idx] }, _c);
      });
    }

    // Visit every entity that has BOTH components A and B as (EntityId, A&, B&).
    // Iterates the A pool and probes B, so it is cheapest when A is the rarer
    // component. (Systems use this, e.g. "every ship with Transform and Motion".)
    template <typename A, typename B, typename Fn>
    void Each(Fn&& _fn)
    {
      Pool<A>* pa = TryPoolFor<A>();
      Pool<B>* pb = TryPoolFor<B>();
      if (pa == nullptr || pb == nullptr)
        return;
      pa->Each([&](uint32_t _idx, A& _a)
      {
        if (B* b = pb->TryGet(_idx))
          _fn(EntityId{ _idx, m_generations[_idx] }, _a, *b);
      });
    }

    [[nodiscard]] std::size_t AliveCount() const
    {
      std::size_t n = 0;
      for (bool a : m_alive)
        if (a)
          ++n;
      return n;
    }

  private:
    template <typename T>
    Pool<T>& PoolFor()
    {
      const uint32_t id = ComponentTypeId<T>();
      if (id >= m_pools.size())
        m_pools.resize(id + 1);
      if (!m_pools[id])
        m_pools[id] = std::make_unique<Pool<T>>();
      return *static_cast<Pool<T>*>(m_pools[id].get());
    }

    template <typename T>
    [[nodiscard]] Pool<T>* TryPoolFor()
    {
      const uint32_t id = ComponentTypeId<T>();
      if (id >= m_pools.size() || !m_pools[id])
        return nullptr;
      return static_cast<Pool<T>*>(m_pools[id].get());
    }

    template <typename T>
    [[nodiscard]] const Pool<T>* TryPoolFor() const
    {
      const uint32_t id = ComponentTypeId<T>();
      if (id >= m_pools.size() || !m_pools[id])
        return nullptr;
      return static_cast<const Pool<T>*>(m_pools[id].get());
    }

    std::vector<uint32_t> m_generations;             // slot -> current generation
    std::vector<bool> m_alive;                       // slot -> alive?
    std::vector<uint32_t> m_free;                     // recycled slots
    std::vector<std::unique_ptr<IPool>> m_pools;      // componentTypeId -> pool
  };
}
