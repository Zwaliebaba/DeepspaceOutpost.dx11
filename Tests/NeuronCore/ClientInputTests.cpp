#include <gtest/gtest.h>

#include <vector>

#include "DataWriter.h"
#include "DataReader.h"
#include "ClientInput.h"            // Net::ClientInput == Msg::InputCommand
#include "ReliableChannel.h"
#include "Messages/Serialize.h"
#include "Messages/Framing.h"
#include "Messages/Registry.h"
#include "Messages/Reliable.h"
#include "Messages/Defs/CoreEvents.h"

using namespace Neuron;

TEST(Input, RoundTripsOverUnreliableLane)
{
  Net::ClientInput in;
  in.sequence = 7;
  in.rollAxis = -1.0f;
  in.pitchAxis = 0.5f;
  in.throttle = 0.25f;
  in.fire = true;
  in.fireMissile = true;
  in.missileTarget = 4242;

  // The input rides the unified 'NMSG' unreliable lane as one record.
  Msg::PacketWriter pw(Msg::MessageLane::Unreliable);
  pw.Add(in);

  Msg::PacketHeader hdr;
  std::vector<Msg::Record> recs;
  ASSERT_TRUE(Msg::ReadPacket(pw.Bytes().data(), pw.Bytes().size(), hdr, recs));
  EXPECT_TRUE(hdr.lane == Msg::MessageLane::Unreliable);
  ASSERT_EQ(recs.size(), 1u);
  EXPECT_TRUE(recs[0].id == Net::ClientInput::Id);

  Net::ClientInput out;
  ASSERT_TRUE(Msg::DecodeRecord(recs[0], out));
  EXPECT_TRUE(out.sequence == 7);
  EXPECT_TRUE(out.rollAxis == -1.0f);
  EXPECT_TRUE(out.pitchAxis == 0.5f);
  EXPECT_TRUE(out.throttle == 0.25f);
  EXPECT_TRUE(out.fire == true);
  EXPECT_TRUE(out.fireMissile == true);
  EXPECT_TRUE(out.missileTarget == 4242u);
}

// Byte-parity: the InputCommand payload must be byte-identical to the legacy
// 'NCMD' field layout (sequence u32, three f32, two u8, target u32 - all LE), so
// folding onto the new framing did not change the wire format of the fields.
TEST(Input, PayloadMatchesLegacyByteLayout)
{
  Net::ClientInput in;
  in.sequence = 7;
  in.rollAxis = -1.0f;
  in.pitchAxis = 0.5f;
  in.throttle = 0.25f;
  in.fire = true;
  in.fireMissile = true;
  in.missileTarget = 4242;

  const std::vector<uint8_t> payload = Msg::Encode(in);

  Net::DataWriter golden;
  golden.WriteU32(7);
  golden.WriteF32(-1.0f);
  golden.WriteF32(0.5f);
  golden.WriteF32(0.25f);
  golden.WriteU8(1);
  golden.WriteU8(1);
  golden.WriteU32(4242);
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
  const Msg::MessageInfo* info = Msg::GlobalRegistry().Find(Net::ClientInput::Id);
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
