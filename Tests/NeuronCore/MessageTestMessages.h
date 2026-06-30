#pragma once

// Shared message structs for the message-system unit tests. These are TEST-ONLY
// fixtures (not real catalog entries): a rich wire message exercising every leaf
// field type, a couple of small wire/control messages, and local-only messages for
// the bus. Ids follow the wire/non-wire range rule so the governance tests are
// meaningful (wire ids low, local ids in the 0x8000+ half).

#include <cstdint>
#include <optional>
#include <string>
#include <tuple>
#include <vector>

#include "Messages/MessageId.h"
#include "Messages/MessageTraits.h"
#include "Messages/NetEntityId.h"

namespace MsgTest
{
  using namespace Neuron::Msg;

  enum class Flavour : uint8_t { Plain = 0, Spicy = 7 };

  // Every supported leaf field type in one wire message.
  struct Kitchen
  {
    static constexpr MessageId    Id    = static_cast<MessageId>(0x0101);
    static constexpr MessageScope Scope = MessageScope::Wire;
    static constexpr MessageKind  Kind  = MessageKind::Command;
    static constexpr MessageLane  Lane  = MessageLane::Gameplay;
    static constexpr Direction    Dir   = Direction::ClientToServer;

    uint8_t  u8 = 0;
    uint16_t u16 = 0;
    uint32_t u32 = 0;
    uint64_t u64 = 0;
    int32_t  i32 = 0;
    int64_t  i64 = 0;
    float    f = 0.0f;
    double   d = 0.0;
    bool     flag = false;
    Flavour  fl = Flavour::Plain;
    NetEntityId ent{};
    std::optional<uint32_t> opt;
    std::string text;
    std::vector<uint16_t> nums;

    auto Fields()       { return std::tie(u8, u16, u32, u64, i32, i64, f, d, flag, fl, ent, opt, text, nums); }
    auto Fields() const { return std::tie(u8, u16, u32, u64, i32, i64, f, d, flag, fl, ent, opt, text, nums); }
  };

  struct Pong
  {
    static constexpr MessageId    Id    = static_cast<MessageId>(0x0102);
    static constexpr MessageScope Scope = MessageScope::Wire;
    static constexpr MessageKind  Kind  = MessageKind::Event;
    static constexpr MessageLane  Lane  = MessageLane::Gameplay;
    static constexpr Direction    Dir   = Direction::ServerToClient;
    uint32_t value = 0;
    auto Fields()       { return std::tie(value); }
    auto Fields() const { return std::tie(value); }
  };

  struct CtrlMsg
  {
    static constexpr MessageId    Id    = static_cast<MessageId>(0x0002);
    static constexpr MessageScope Scope = MessageScope::Control;
    static constexpr MessageKind  Kind  = MessageKind::Event;
    static constexpr MessageLane  Lane  = MessageLane::Control;
    static constexpr Direction    Dir   = Direction::ServerToClient;
    uint32_t value = 0;
    auto Fields()       { return std::tie(value); }
    auto Fields() const { return std::tie(value); }
  };

  struct TickEv
  {
    static constexpr MessageId    Id    = static_cast<MessageId>(0x8001);
    static constexpr MessageScope Scope = MessageScope::LocalOnly;
    static constexpr MessageKind  Kind  = MessageKind::Event;
    static constexpr MessageLane  Lane  = MessageLane::Unreliable;
    static constexpr Direction    Dir   = Direction::None;
    int n = 0;
    auto Fields()       { return std::tie(n); }
    auto Fields() const { return std::tie(n); }
  };

  struct FollowUp
  {
    static constexpr MessageId    Id    = static_cast<MessageId>(0x8002);
    static constexpr MessageScope Scope = MessageScope::LocalOnly;
    static constexpr MessageKind  Kind  = MessageKind::Event;
    static constexpr MessageLane  Lane  = MessageLane::Unreliable;
    static constexpr Direction    Dir   = Direction::None;
    int n = 0;
    auto Fields()       { return std::tie(n); }
    auto Fields() const { return std::tie(n); }
  };
}
