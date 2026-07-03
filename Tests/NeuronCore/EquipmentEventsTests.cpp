#include <gtest/gtest.h>

#include "Messages/Defs/EquipmentEvents.h"   // EcmPulse / EscapePodUsed
#include "Messages/Serialize.h"              // Encode / Decode
#include "Messages/Registry.h"

using namespace Neuron::Msg;

TEST(EquipmentEvents, EcmPulseRoundTrips)
{
  EcmPulse in;
  in.source = 77;

  EcmPulse out;
  ASSERT_TRUE(Decode(Encode(in), out));
  EXPECT_EQ(out.source, 77u);
}

TEST(EquipmentEvents, EscapePodUsedRoundTrips)
{
  EscapePodUsed in;
  in.entityId = 5;

  EscapePodUsed out;
  ASSERT_TRUE(Decode(Encode(in), out));
  EXPECT_EQ(out.entityId, 5u);
}

TEST(EquipmentEvents, BothAreRegisteredWireGameplayEvents)
{
  const MessageInfo* pulse = GlobalRegistry().Find(EcmPulse::Id);
  ASSERT_NE(pulse, nullptr);
  EXPECT_TRUE(pulse->scope == MessageScope::Wire);
  EXPECT_TRUE(pulse->lane == MessageLane::Gameplay);
  EXPECT_TRUE(pulse->dir == Direction::ServerToClient);

  const MessageInfo* pod = GlobalRegistry().Find(EscapePodUsed::Id);
  ASSERT_NE(pod, nullptr);
  EXPECT_TRUE(pod->scope == MessageScope::Wire);
  EXPECT_TRUE(pod->dir == Direction::ServerToClient);

  EXPECT_TRUE(GlobalRegistry().DuplicateIds().empty());
  EXPECT_TRUE(GlobalRegistry().ScopeIdConsistent());
}
