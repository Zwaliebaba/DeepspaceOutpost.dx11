#include <gtest/gtest.h>

#include "Messages/Registry.h"
#include "MessageTestMessages.h"

using namespace Neuron::Msg;
using namespace MsgTest;

namespace
{
  // A "wire" message that wrongly uses a non-wire (high-bit) id.
  struct BadWire
  {
    static constexpr MessageId    Id    = static_cast<MessageId>(0x8123);
    static constexpr MessageScope Scope = MessageScope::Wire;
    static constexpr MessageKind  Kind  = MessageKind::Event;
    static constexpr MessageLane  Lane  = MessageLane::Gameplay;
    static constexpr Direction    Dir   = Direction::ServerToClient;
    uint8_t x = 0;
    auto Fields()       { return std::tie(x); }
    auto Fields() const { return std::tie(x); }
  };

  // A LocalOnly message that wrongly uses a low (wire-half) id.
  struct BadLocal
  {
    static constexpr MessageId    Id    = static_cast<MessageId>(0x0050);
    static constexpr MessageScope Scope = MessageScope::LocalOnly;
    static constexpr MessageKind  Kind  = MessageKind::Event;
    static constexpr MessageLane  Lane  = MessageLane::Unreliable;
    static constexpr Direction    Dir   = Direction::None;
    uint8_t x = 0;
    auto Fields()       { return std::tie(x); }
    auto Fields() const { return std::tie(x); }
  };
}

TEST(CatalogGovernance, WellFormedCatalogPassesAllChecks)
{
  MessageRegistry reg;
  reg.Add<Kitchen>("Kitchen");
  reg.Add<Pong>("Pong");
  reg.Add<CtrlMsg>("CtrlMsg");
  reg.Add<TickEv>("TickEv");

  EXPECT_TRUE(reg.DuplicateIds().empty());
  EXPECT_TRUE(reg.ScopeIdConsistent());
  EXPECT_TRUE(reg.WireHaveDirection());
}

TEST(CatalogGovernance, DuplicateIdsAreDetected)
{
  MessageRegistry reg;
  reg.Add<Kitchen>("Kitchen");
  reg.Add<Kitchen>("KitchenClone");   // same Id registered twice
  ASSERT_EQ(reg.DuplicateIds().size(), 1u);
  EXPECT_TRUE(reg.DuplicateIds()[0] == Kitchen::Id);
}

TEST(CatalogGovernance, FindResolvesById)
{
  MessageRegistry reg;
  reg.Add<Pong>("Pong");
  const MessageInfo* info = reg.Find(Pong::Id);
  ASSERT_NE(info, nullptr);
  EXPECT_TRUE(info->scope == MessageScope::Wire);
  EXPECT_TRUE(info->dir == Direction::ServerToClient);
  EXPECT_EQ(reg.Find(static_cast<MessageId>(0x7777)), nullptr);
}

// The high-bit id rule: wire-scoped messages must have ids in the low half, and
// non-wire messages in the top half. A registry that violates this fails the check.
TEST(CatalogGovernance, ScopeIdRuleCatchesMisScopedWireMessage)
{
  MessageRegistry reg;
  reg.Add<BadWire>("BadWire");
  EXPECT_FALSE(reg.ScopeIdConsistent());
}

TEST(CatalogGovernance, LocalOnlyMessageMustUseNonWireId)
{
  MessageRegistry reg;
  reg.Add<BadLocal>("BadLocal");
  EXPECT_FALSE(reg.ScopeIdConsistent());
}

TEST(CatalogGovernance, IdRangeHelpersClassifyCorrectly)
{
  EXPECT_FALSE(IsNonWireId(Kitchen::Id));   // 0x0101 - wire half
  EXPECT_TRUE(IsNonWireId(TickEv::Id));     // 0x8001 - non-wire half
}
