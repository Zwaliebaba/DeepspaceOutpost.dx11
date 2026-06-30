#pragma once

// Registry - the message catalog and its governance checks (NeuronCore).
//
// The catalog is the single source of truth for the wire ABI and for tooling. Each
// message is described once (from its static traits) into a MessageInfo and stored
// in a registry that tooling can enumerate (schema export, packet-dump decoder,
// compat diff) and CI can audit (no duplicate ids; wire/non-wire id-range rule;
// every wire message has a lane + direction).
//
// Mechanism only. Two ways in: REGISTER_MESSAGE(T) auto-adds a header-defined
// message to the process-wide GlobalRegistry() exactly once (an inline variable,
// so multiple translation units including the def don't double-register); or call
// MessageRegistry::Add<T>() on a local registry (used by unit tests).

#include <cstdint>
#include <unordered_set>
#include <vector>

#include "MessageId.h"
#include "MessageTraits.h"
#include "Serialize.h"

namespace Neuron::Msg
{
  struct MessageInfo
  {
    MessageId id = MessageId::Invalid;
    const char* name = "";
    MessageScope scope = MessageScope::LocalOnly;
    MessageKind kind = MessageKind::Event;
    MessageLane lane = MessageLane::Unreliable;
    Direction dir = Direction::None;
  };

  // Build the descriptor for a message type from its static traits.
  template <Message M>
  [[nodiscard]] constexpr MessageInfo DescribeMessage(const char* _name)
  {
    return MessageInfo{ M::Id, _name, M::Scope, M::Kind, M::Lane, M::Dir };
  }

  class MessageRegistry
  {
  public:
    void Register(const MessageInfo& _info) { m_infos.push_back(_info); }

    template <Message M>
    void Add(const char* _name) { Register(DescribeMessage<M>(_name)); }

    [[nodiscard]] const std::vector<MessageInfo>& All() const { return m_infos; }

    [[nodiscard]] const MessageInfo* Find(MessageId _id) const
    {
      for (const MessageInfo& i : m_infos)
        if (i.id == _id)
          return &i;
      return nullptr;
    }

    // --- Governance (used by the CI catalog test) -----------------------------

    // Ids registered more than once (each offending id reported once).
    [[nodiscard]] std::vector<MessageId> DuplicateIds() const
    {
      std::vector<MessageId> dups;
      std::unordered_set<uint16_t> seen;
      std::unordered_set<uint16_t> reported;
      for (const MessageInfo& i : m_infos)
      {
        const uint16_t raw = Raw(i.id);
        if (!seen.insert(raw).second && reported.insert(raw).second)
          dups.push_back(i.id);
      }
      return dups;
    }

    // Wire/Control messages must have ids in the low (wire-ABI) half; every other
    // scope must have an id in the top (non-wire) half. Keeps local events out of
    // the wire id space and makes a mis-scoped send detectable.
    [[nodiscard]] bool ScopeIdConsistent() const
    {
      for (const MessageInfo& i : m_infos)
      {
        const bool nonWireId = IsNonWireId(i.id);
        if (IsWireScope(i.scope) == nonWireId)   // wire id must NOT be non-wire, and vice versa
          return false;
      }
      return true;
    }

    // Every wire message must declare a real direction (so the inbound pipeline
    // can reject a peer sending the wrong way).
    [[nodiscard]] bool WireHaveDirection() const
    {
      for (const MessageInfo& i : m_infos)
        if (IsWireScope(i.scope) && i.dir == Direction::None)
          return false;
      return true;
    }

  private:
    std::vector<MessageInfo> m_infos;
  };

  // Process-wide catalog populated by REGISTER_MESSAGE.
  [[nodiscard]] inline MessageRegistry& GlobalRegistry()
  {
    static MessageRegistry registry;
    return registry;
  }

  namespace Detail
  {
    // Registering one message into the global catalog returns a bool we can store
    // in an inline variable, so the registration runs exactly once across all
    // translation units that include the message's def header.
    template <Message M>
    [[nodiscard]] bool RegisterGlobal(const char* _name)
    {
      GlobalRegistry().Add<M>(_name);
      return true;
    }
  }
}

// Add a header-defined message to the process-wide catalog. Place at namespace
// scope in the message's def header, after the struct.
#define REGISTER_MESSAGE(T) \
  namespace Neuron::Msg::Detail { inline const bool g_registered_##T = RegisterGlobal<T>(#T); }
