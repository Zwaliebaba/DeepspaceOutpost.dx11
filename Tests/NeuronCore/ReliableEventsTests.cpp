#include <gtest/gtest.h>

#include <vector>

#include "DataWriter.h"
#include "ReliableChannel.h"
#include "Messages/Reliable.h"
#include "Messages/Registry.h"
#include "Messages/Defs/CoreEvents.h"
#include "StationProtocol.h"   // registers Station messages too

using namespace Neuron;

// Byte-parity: each folded event's generic encoding must match the old GameEvents
// hand-rolled layout, so the fold is a framing change, not a wire-format change.

TEST(ReliableEvents, AssignPlayerMatchesLegacyLayout)
{
  Net::DataWriter golden;
  golden.WriteU32(123);
  EXPECT_EQ(Msg::Encode(Msg::AssignPlayer{ 123 }), golden.Bytes());
}

TEST(ReliableEvents, DespawnMatchesLegacyLayout)
{
  Net::DataWriter golden;
  golden.WriteU32(99);
  EXPECT_EQ(Msg::Encode(Msg::EntityDespawn{ 99 }), golden.Bytes());
}

TEST(ReliableEvents, DeathMatchesLegacyLayout)
{
  Net::DataWriter golden;
  golden.WriteU32(3);
  golden.WriteU32(8);
  EXPECT_EQ(Msg::Encode(Msg::EntityDeath{ 3, 8 }), golden.Bytes());
}

TEST(ReliableEvents, ChatMatchesLegacyLayout)
{
  Net::DataWriter golden;
  golden.WriteU32(42);
  golden.WriteU16(5);
  for (char c : std::string("gg wp"))
    golden.WriteU8(static_cast<uint8_t>(c));
  EXPECT_EQ(Msg::Encode(Msg::Chat{ 42, "gg wp" }), golden.Bytes());
}

TEST(ReliableEvents, RoundTripOverReliableChannel)
{
  Net::ReliableChannel server;
  Net::ReliableChannel client;

  Msg::SendReliable(server, Msg::EntityDeath{ 7, 3 });
  Msg::SendReliable(server, Msg::Chat{ 1, "hi" });

  std::vector<uint8_t> pkt = server.WritePacket();
  ASSERT_TRUE(client.ReadPacket(pkt.data(), pkt.size()));

  Net::ReliableMessage m1;
  ASSERT_TRUE(client.Receive(m1));
  Msg::EntityDeath d;
  ASSERT_TRUE(Msg::TryDecode(m1, d));
  EXPECT_EQ(d.victim, 7u);
  EXPECT_EQ(d.killer, 3u);

  Net::ReliableMessage m2;
  ASSERT_TRUE(client.Receive(m2));
  Msg::Chat c;
  ASSERT_TRUE(Msg::TryDecode(m2, c));
  EXPECT_EQ(c.sender, 1u);
  EXPECT_TRUE(c.text == "hi");
}

TEST(ReliableEvents, FoldedMessagesAreInTheGlobalCatalog)
{
  const Msg::MessageRegistry& reg = Msg::GlobalRegistry();
  EXPECT_NE(reg.Find(Msg::AssignPlayer::Id), nullptr);
  EXPECT_NE(reg.Find(Msg::EntityDespawn::Id), nullptr);
  EXPECT_NE(reg.Find(Msg::EntityDeath::Id), nullptr);
  EXPECT_NE(reg.Find(Msg::Chat::Id), nullptr);
  EXPECT_NE(reg.Find(Msg::StationRequest::Id), nullptr);
  EXPECT_NE(reg.Find(Msg::StationResponse::Id), nullptr);

  // The whole registered catalog still obeys the governance rules.
  EXPECT_TRUE(reg.ScopeIdConsistent());
  EXPECT_TRUE(reg.WireHaveDirection());
  EXPECT_TRUE(reg.DuplicateIds().empty());
}
