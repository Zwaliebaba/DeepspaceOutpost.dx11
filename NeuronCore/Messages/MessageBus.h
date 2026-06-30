// MessageBus - the in-process, deferred pub/sub dispatcher (NeuronCore).
//
// The bus decouples the EDGES of the game (commands, facts, notifications, network
// ingress/egress, UI/audio/VFX triggers) - NOT the hot simulation inner loop, which
// stays direct ECS iteration. Publish() enqueues a typed message; handlers run only
// during Dispatch(), called once per server tick / client frame.
//
// Dispatch drains in GENERATIONS within one tick: a handler that publishes is
// processed in the next generation (never recursively mid-handler, never silently
// deferred to next frame), so a fire -> death -> despawn chain resolves this tick
// while staying re-entrancy-safe and deterministic. Ordering within a generation is
// stable FIFO by enqueue order. Overflow and unsubscribe-during-dispatch have
// explicit, defined behaviour, and per-message-id metrics are always collected.
//
// Mechanism only (no behaviour); the handlers registered on each side carry it.
// Single-threaded by contract today (sim loop owns the bus); Publish is the only
// method a future worker thread would touch, so a lock-free ingress can be added
// later without changing call sites.

#pragma once

#include <cstddef>
#include <cstdint>
#include <deque>
#include <functional>
#include <memory>
#include <unordered_map>
#include <vector>

#include "MessageId.h"
#include "MessageTraits.h"
#include "Serialize.h"   // the Message concept

namespace Neuron::Msg
{
  using SubscriptionToken = uint64_t;
  inline constexpr SubscriptionToken INVALID_SUBSCRIPTION = 0;

  // Safety bounds. A publish storm cannot wedge a tick (generation cap) nor grow
  // memory without limit (queue cap); both are surfaced as metrics, never silent.
  inline constexpr std::size_t MAX_GENERATIONS = 16;
  inline constexpr std::size_t MAX_QUEUE_DEPTH = 4096;

  struct DispatchStats
  {
    uint64_t published = 0;     // total Publish() calls
    uint64_t dispatched = 0;    // messages handed to (zero or more) handlers
    uint64_t dropped = 0;       // messages discarded on queue overflow
    uint64_t generations = 0;   // total generations drained (across all Dispatch calls)
    std::size_t maxQueueDepth = 0;
  };

  class MessageBus
  {
  public:
    // Register a typed handler; returns a token for Unsubscribe. A handler may be
    // added or removed during Dispatch (removal takes effect after the current
    // generation).
    template <Message M>
    SubscriptionToken Subscribe(std::function<void(const M&)> _fn)
    {
      const SubscriptionToken token = ++m_nextToken;
      Handler h;
      h.token = token;
      h.active = true;
      h.invoke = [fn = std::move(_fn)](const void* _p) { fn(*static_cast<const M*>(_p)); };
      m_handlers[Raw(M::Id)].push_back(std::move(h));
      return token;
    }

    void Unsubscribe(SubscriptionToken _token)
    {
      for (auto& entry : m_handlers)
        for (Handler& h : entry.second)
          if (h.token == _token)
          {
            h.active = false;       // deferred erase (safe during dispatch)
            m_dirty = true;
            return;
          }
    }

    // Enqueue a message for the next Dispatch(). The payload is copied; handlers
    // see it by const ref. On overflow the oldest non-Control message is dropped
    // (Control is never dropped - a lost session message is unrecoverable).
    template <Message M>
    void Publish(const M& _m)
    {
      ++m_stats.published;

      if (m_queue.size() >= MAX_QUEUE_DEPTH)
        EvictOneForOverflow();

      Queued q;
      q.id = M::Id;
      q.scope = M::Scope;
      q.seq = ++m_seq;
      q.payload = std::make_shared<M>(_m);
      m_queue.push_back(std::move(q));

      if (m_queue.size() > m_stats.maxQueueDepth)
        m_stats.maxQueueDepth = m_queue.size();
    }

    // Drain the queue, invoking handlers. Messages published by handlers are
    // processed in the following generation, up to MAX_GENERATIONS; any remainder
    // is dropped (counted) so a self-feeding publish loop cannot hang the tick.
    void Dispatch()
    {
      std::size_t gen = 0;
      while (!m_queue.empty())
      {
        if (gen >= MAX_GENERATIONS)
        {
          m_stats.dropped += m_queue.size();
          m_queue.clear();
          break;
        }

        // This generation's batch; messages published while handling it land in
        // the now-empty m_queue and form the next generation.
        std::deque<Queued> batch;
        batch.swap(m_queue);

        for (const Queued& q : batch)
        {
          auto it = m_handlers.find(Raw(q.id));
          if (it != m_handlers.end())
            for (const Handler& h : it->second)
              if (h.active)
                h.invoke(q.payload.get());
          ++m_stats.dispatched;
        }

        ++gen;
        ++m_stats.generations;
        if (m_dirty)
          PruneInactive();
      }
    }

    [[nodiscard]] std::size_t PendingCount() const { return m_queue.size(); }
    [[nodiscard]] const DispatchStats& Stats() const { return m_stats; }
    void ResetStats() { m_stats = DispatchStats{}; }

  private:
    struct Handler
    {
      SubscriptionToken token = INVALID_SUBSCRIPTION;
      bool active = false;
      std::function<void(const void*)> invoke;
    };

    struct Queued
    {
      MessageId id = MessageId::Invalid;
      MessageScope scope = MessageScope::LocalOnly;
      uint64_t seq = 0;
      std::shared_ptr<const void> payload;
    };

    void EvictOneForOverflow()
    {
      // Drop the oldest droppable (non-Control) message, keeping the newest state.
      for (auto it = m_queue.begin(); it != m_queue.end(); ++it)
        if (it->scope != MessageScope::Control)
        {
          m_queue.erase(it);
          ++m_stats.dropped;
          return;
        }
      // All-Control queue at capacity: never drop a session message, allow growth.
    }

    void PruneInactive()
    {
      for (auto& entry : m_handlers)
        std::erase_if(entry.second, [](const Handler& _h) { return !_h.active; });
      m_dirty = false;
    }

    std::unordered_map<uint16_t, std::vector<Handler>> m_handlers;
    std::deque<Queued> m_queue;
    DispatchStats m_stats;
    SubscriptionToken m_nextToken = INVALID_SUBSCRIPTION;
    uint64_t m_seq = 0;
    bool m_dirty = false;
  };
}
