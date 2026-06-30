#include <gtest/gtest.h>

#include <vector>

#include "Messages/Framing.h"
#include "ReliableChannel.h"
#include "MessageTestMessages.h"

using namespace Neuron::Msg;
using namespace MsgTest;

TEST(MessageWire, MultiRecordPacketRoundTrips)
{
  PacketWriter pw(MessageLane::Gameplay);
  Kitchen k; k.u32 = 0xCAFEBABE; k.text = "frame"; k.nums = { 9, 8, 7 };
  pw.Add(k);
  Pong pong; pong.value = 77;
  pw.Add(pong);

  PacketHeader hdr;
  std::vector<Record> recs;
  ASSERT_TRUE(ReadPacket(pw.Bytes().data(), pw.Bytes().size(), hdr, recs));
  EXPECT_TRUE(hdr.lane == MessageLane::Gameplay);
  ASSERT_EQ(recs.size(), 2u);

  Kitchen gotK;
  ASSERT_TRUE(DecodeRecord(recs[0], gotK));
  EXPECT_EQ(gotK.u32, 0xCAFEBABEu);
  EXPECT_EQ(gotK.text, "frame");
  ASSERT_EQ(gotK.nums.size(), 3u);

  Pong gotP;
  ASSERT_TRUE(DecodeRecord(recs[1], gotP));
  EXPECT_EQ(gotP.value, 77u);
}

TEST(MessageWire, LaneBytePreserved)
{
  for (MessageLane lane : { MessageLane::Control, MessageLane::Gameplay,
                            MessageLane::Bulk, MessageLane::Unreliable })
  {
    PacketWriter pw(lane);
    pw.Add(Pong{ 1 });
    PacketHeader hdr;
    std::vector<Record> recs;
    ASSERT_TRUE(ReadPacket(pw.Bytes().data(), pw.Bytes().size(), hdr, recs));
    EXPECT_TRUE(hdr.lane == lane);
  }
}

TEST(MessageWire, DecodeRecordRejectsIdMismatch)
{
  PacketWriter pw(MessageLane::Gameplay);
  pw.Add(Kitchen{});
  PacketHeader hdr;
  std::vector<Record> recs;
  ASSERT_TRUE(ReadPacket(pw.Bytes().data(), pw.Bytes().size(), hdr, recs));
  ASSERT_EQ(recs.size(), 1u);

  Pong wrong;
  EXPECT_FALSE(DecodeRecord(recs[0], wrong));   // a Kitchen record is not a Pong
}

TEST(MessageWire, ForeignOrTruncatedPacketsRejected)
{
  PacketWriter pw(MessageLane::Gameplay);
  pw.Add(Kitchen{});
  pw.Add(Pong{ 5 });

  // Truncate the last record mid-payload.
  std::vector<uint8_t> bytes = pw.Bytes();
  bytes.resize(bytes.size() - 1);
  PacketHeader hdr;
  std::vector<Record> recs;
  EXPECT_FALSE(ReadPacket(bytes.data(), bytes.size(), hdr, recs));

  // Foreign magic.
  std::vector<uint8_t> foreign = { 0xDE, 0xAD, 0xBE, 0xEF, 0x01, 0x00, 0x01 };
  EXPECT_FALSE(ReadPacket(foreign.data(), foreign.size(), hdr, recs));
}

TEST(MessageWire, EmptyPacketDecodesToZeroRecords)
{
  PacketWriter pw(MessageLane::Control);   // header only, no records
  PacketHeader hdr;
  std::vector<Record> recs;
  ASSERT_TRUE(ReadPacket(pw.Bytes().data(), pw.Bytes().size(), hdr, recs));
  EXPECT_TRUE(recs.empty());
  EXPECT_TRUE(hdr.lane == MessageLane::Control);
}

TEST(MessageWire, PeekMagicIdentifiesPackets)
{
  PacketWriter pw(MessageLane::Gameplay);
  pw.Add(Pong{ 1 });
  EXPECT_EQ(PeekMessageMagic(pw.Bytes().data(), pw.Bytes().size()), MESSAGE_MAGIC);
  EXPECT_NE(PeekMessageMagic(pw.Bytes().data(), pw.Bytes().size()), Neuron::Net::EVENT_MAGIC);
}

// A message record's payload is opaque bytes keyed by a u16 tag - exactly what the
// existing ReliableChannel already carries. This shows a catalog message riding the
// reliable transport unchanged (the full lane bridge lands in a later phase).
TEST(MessageWire, RecordRidesReliableChannelKeyedByMessageId)
{
  Neuron::Net::ReliableChannel client;
  Neuron::Net::ReliableChannel server;

  Pong sent; sent.value = 4242;
  client.Send(Raw(Pong::Id), Encode(sent));

  const std::vector<uint8_t> pkt = client.WritePacket();
  ASSERT_TRUE(server.ReadPacket(pkt.data(), pkt.size()));

  Neuron::Net::ReliableMessage m;
  ASSERT_TRUE(server.Receive(m));
  EXPECT_EQ(m.type, Raw(Pong::Id));

  Pong got;
  Record rec{ static_cast<MessageId>(m.type), m.payload };
  ASSERT_TRUE(DecodeRecord(rec, got));
  EXPECT_EQ(got.value, 4242u);
}
