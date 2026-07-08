#include <gtest/gtest.h>

#include <vector>

#include "DataWriter.h"
#include "DataReader.h"
#include "Messages/Defs/InputCommand.h"
#include "ReliableChannel.h"
#include "Messages/Serialize.h"
#include "Messages/Framing.h"
#include "Messages/Registry.h"
#include "Messages/Reliable.h"
#include "Messages/Defs/CoreEvents.h"

using namespace Neuron;

TEST(Input, RoundTripsOverUnreliableLane)
{
  Msg::InputCommand in;
  in.sequence = 7;
  in.ackSnapshotTick = 0xABCD;

  // The heartbeat rides the unified 'NMSG' unreliable lane as one record.
  Msg::PacketWriter pw(Msg::MessageLane::Unreliable);
  pw.Add(in);

  Msg::PacketHeader hdr;
  std::vector<Msg::Record> recs;
  ASSERT_TRUE(Msg::ReadPacket(pw.Bytes().data(), pw.Bytes().size(), hdr, recs));
  EXPECT_TRUE(hdr.lane == Msg::MessageLane::Unreliable);
  ASSERT_EQ(recs.size(), 1u);
  EXPECT_TRUE(recs[0].id == Msg::InputCommand::Id);

  Msg::InputCommand out;
  ASSERT_TRUE(Msg::DecodeRecord(recs[0], out));
  EXPECT_TRUE(out.sequence == 7);
  EXPECT_TRUE(out.ackSnapshotTick == 0xABCDu);
}

// Golden bytes: the protocol-v4 heartbeat is exactly {sequence u32, ack u32},
// little-endian - 8 bytes, nothing else. (The legacy 'NCMD' axes/flags layout
// retired with the v3->v4 re-cut: movement is a UnitOrder, abilities ride the
// reliable AbilityRequest, and PROTOCOL_VERSION gates out old clients.)
TEST(Input, PayloadMatchesTheHeartbeatByteLayout)
{
  Msg::InputCommand in;
  in.sequence = 7;
  in.ackSnapshotTick = 0xABCD;

  const std::vector<uint8_t> payload = Msg::Encode(in);

  Net::DataWriter golden;
  golden.WriteU32(7);       // sequence
  golden.WriteU32(0xABCD);  // ackSnapshotTick (E2b)
  EXPECT_EQ(payload, golden.Bytes());
}

TEST(Input, ForeignMagicRejected)
{
  std::vector<uint8_t> foreign = { 0xDE, 0xAD, 0xBE, 0xEF, 0x01, 0x00, 0x03 };
  Msg::PacketHeader hdr;
  std::vector<Msg::Record> recs;
  EXPECT_FALSE(Msg::ReadPacket(foreign.data(), foreign.size(), hdr, recs));
}

TEST(Input, InputCommandIsRegisteredInTheGlobalCatalog)
{
  const Msg::MessageInfo* info = Msg::GlobalRegistry().Find(Msg::InputCommand::Id);
  ASSERT_NE(info, nullptr);
  EXPECT_TRUE(info->scope == Msg::MessageScope::Wire);
  EXPECT_TRUE(info->dir == Msg::Direction::ClientToServer);
  EXPECT_TRUE(Msg::GlobalRegistry().ScopeIdConsistent());
  EXPECT_TRUE(Msg::GlobalRegistry().DuplicateIds().empty());
}

TEST(Input, AssignPlayerHandshakeRoundTrips)
{
  Net::ReliableMessage m{ Msg::Raw(Msg::AssignPlayer::Id), Msg::Encode(Msg::AssignPlayer{ 123 }) };
  Msg::AssignPlayer assign;
  EXPECT_TRUE(Msg::TryDecode(m, assign));
  EXPECT_TRUE(assign.entityId == 123);

  // A non-assign message is not mistaken for one.
  Net::ReliableMessage despawn{ Msg::Raw(Msg::EntityDespawn::Id), Msg::Encode(Msg::EntityDespawn{ 5 }) };
  EXPECT_TRUE(!Msg::TryDecode(despawn, assign));
}

TEST(Input, AssignPlayerDeliversOverTheReliableChannel)
{
  Net::ReliableChannel server;
  Net::ReliableChannel client;

  Msg::SendReliable(server, Msg::AssignPlayer{ 42 });
  std::vector<uint8_t> pkt = server.WritePacket();
  EXPECT_TRUE(client.ReadPacket(pkt.data(), pkt.size()));

  Net::ReliableMessage m;
  EXPECT_TRUE(client.Receive(m));
  Msg::AssignPlayer assign;
  EXPECT_TRUE(Msg::TryDecode(m, assign));
  EXPECT_TRUE(assign.entityId == 42);
}
