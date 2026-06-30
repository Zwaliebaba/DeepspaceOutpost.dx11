#include <gtest/gtest.h>

#include <vector>

#include "DataWriter.h"
#include "Messages/PacketInspect.h"
#include "Messages/MessageEndpoint.h"
#include "Messages/Catalog.h"        // GlobalRegistry() populated with the real catalog

using namespace Neuron;

TEST(PacketInspect, DecodesUnreliableNmsgPacket)
{
  Msg::PacketWriter pw(Msg::MessageLane::Unreliable);
  pw.Add(Msg::InputCommand{});   // 0x0100

  const Msg::InspectedPacket p =
      Msg::InspectPacket(pw.Bytes().data(), pw.Bytes().size(), Msg::GlobalRegistry());
  ASSERT_TRUE(p.ok);
  EXPECT_TRUE(p.kind == Msg::PacketKind::Unreliable);
  EXPECT_TRUE(p.lane == Msg::MessageLane::Unreliable);
  ASSERT_EQ(p.records.size(), 1u);
  EXPECT_TRUE(p.records[0].id == Msg::InputCommand::Id);
  EXPECT_EQ(p.records[0].name, "InputCommand");
}

TEST(PacketInspect, DecodesReliableNrlbDatagram)
{
  Msg::MessageEndpoint ep;
  ep.Send(Msg::EntityDeath{ 1, 2 });   // Gameplay lane
  ep.Send(Msg::Chat{ 3, "gg" });       // Gameplay lane

  bool sawDeath = false;
  bool sawChat = false;
  for (const std::vector<uint8_t>& dg : ep.WriteDatagrams())
  {
    const Msg::InspectedPacket p = Msg::InspectPacket(dg.data(), dg.size(), Msg::GlobalRegistry());
    ASSERT_TRUE(p.ok);
    EXPECT_TRUE(p.kind == Msg::PacketKind::Reliable);
    for (const Msg::InspectedRecord& r : p.records)
    {
      if (r.id == Msg::EntityDeath::Id) { sawDeath = true; EXPECT_EQ(r.name, "EntityDeath"); }
      if (r.id == Msg::Chat::Id)        { sawChat = true;  EXPECT_EQ(r.name, "Chat"); }
    }
  }
  EXPECT_TRUE(sawDeath);
  EXPECT_TRUE(sawChat);
}

TEST(PacketInspect, UnknownIdReportsPlaceholderName)
{
  // A wire packet carrying an unregistered id still inspects (named <unknown>).
  Net::DataWriter w;
  w.WriteU32(Msg::MESSAGE_MAGIC);
  w.WriteU16(Msg::PROTOCOL_VERSION);
  w.WriteU8(static_cast<uint8_t>(Msg::MessageLane::Gameplay));
  w.WriteU16(0x7777);   // unknown id
  w.WriteU16(2);        // payload length
  w.WriteU8(1);
  w.WriteU8(2);

  const Msg::InspectedPacket p = Msg::InspectPacket(w.Bytes().data(), w.Bytes().size(), Msg::GlobalRegistry());
  ASSERT_TRUE(p.ok);
  ASSERT_EQ(p.records.size(), 1u);
  EXPECT_TRUE(p.records[0].id == static_cast<Msg::MessageId>(0x7777));
  EXPECT_EQ(p.records[0].name, "<unknown>");
  EXPECT_EQ(p.records[0].length, 2u);
}

TEST(PacketInspect, ForeignPacketRejectedSafely)
{
  std::vector<uint8_t> foreign = { 0xDE, 0xAD, 0xBE, 0xEF };
  const Msg::InspectedPacket p = Msg::InspectPacket(foreign.data(), foreign.size(), Msg::GlobalRegistry());
  EXPECT_FALSE(p.ok);
  EXPECT_EQ(Msg::FormatPacket(p), "<malformed or foreign packet>\n");
}
