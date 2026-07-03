#pragma once

// OnChangeCache - resend a keyed value only when it actually changed (NeuronServer).
//
// The server pushes per-client private state (HUD vitals, and later more) every
// tick it changes - but MUST NOT push it every tick, or a slowly-refilling shield
// becomes a 30 Hz reliable-message stream per player. This is the generic piece:
// a map of key -> last-sent value; `Changed()` compares the candidate against the
// cache and stores it when it differs (or is new). `Prune()` drops entries whose
// key no longer exists (a disconnected client), so the cache cannot grow without
// bound over a long server uptime.
//
// Equality is pluggable: catalog messages compare via their Fields() tuple rather
// than an operator== they don't define, so the caller supplies a comparator like
//   struct Eq { bool operator()(const M& a, const M& b) const
//               { return a.Fields() == b.Fields(); } };
//
// Header-only, std-only - unit-tested headlessly.

#include <cstddef>
#include <functional>
#include <unordered_map>
#include <utility>

namespace Neuron::Server
{
  template <typename Key, typename Value, typename Eq = std::equal_to<Value>>
  class OnChangeCache
  {
  public:
    // True when `_value` differs from the cached copy for `_key` (or the key is
    // new); the cache then remembers it - i.e. "should I send this?".
    [[nodiscard]] bool Changed(const Key& _key, const Value& _value)
    {
      const auto it = m_last.find(_key);
      if (it != m_last.end() && Eq{}(it->second, _value))
        return false;
      if (it != m_last.end())
        it->second = _value;
      else
        m_last.emplace(_key, _value);
      return true;
    }

    // Drop every entry whose key fails `_keep(key)` (e.g. a reaped session).
    template <typename Keep>
    void Prune(Keep&& _keep)
    {
      for (auto it = m_last.begin(); it != m_last.end();)
      {
        if (!_keep(it->first))
          it = m_last.erase(it);
        else
          ++it;
      }
    }

    [[nodiscard]] std::size_t Size() const { return m_last.size(); }

  private:
    std::unordered_map<Key, Value> m_last;
  };
}
