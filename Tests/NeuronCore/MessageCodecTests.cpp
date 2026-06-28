#include <gtest/gtest.h>

#include <vector>

#include "Messages/Serialize.h"
#include "MessageTestMessages.h"

using namespace Neuron::Msg;
using namespace MsgTest;

TEST(MessageCodec, EveryFieldTypeRoundTrips)
{
  Kitchen k;
  k.u8 = 0xAB; k.u16 = 0xBEEF; k.u32 = 0xDEADBEEF; k.u64 = 0x0123456789ABCDEFull;
  k.i32 = -12345; k.i64 = -9000000000ll; k.f = 3.5f; k.d = -2.25; k.flag = true;
  k.fl = Flavour::Spicy; k.ent = NetEntityId{ 42, 7 }; k.opt = 999u;
  k.text = "hello world"; k.nums = { 1, 2, 3, 65535 };

  const std::vector<uint8_t> bytes = Encode(k);
  Kitchen out;
  ASSERT_TRUE(Decode(bytes, out));
  EXPECT_EQ(out.u8, k.u8);
  EXPECT_EQ(out.u16, k.u16);
  EXPECT_EQ(out.u32, k.u32);
  EXPECT_EQ(out.u64, k.u64);
  EXPECT_EQ(out.i32, k.i32);
  EXPECT_EQ(out.i64, k.i64);
  EXPECT_FLOAT_EQ(out.f, k.f);
  EXPECT_DOUBLE_EQ(out.d, k.d);
  EXPECT_EQ(out.flag, k.flag);
  EXPECT_TRUE(out.fl == Flavour::Spicy);
  EXPECT_TRUE(out.ent == (NetEntityId{ 42, 7 }));
  ASSERT_TRUE(out.opt.has_value());
  EXPECT_EQ(*out.opt, 999u);
  EXPECT_EQ(out.text, "hello world");
  ASSERT_EQ(out.nums.size(), 4u);
  EXPECT_EQ(out.nums[3], 65535);
}

TEST(MessageCodec, AbsentOptionalRoundTripsAbsent)
{
  Kitchen k;
  k.opt.reset();
  Kitchen out;
  out.opt = 5u;   // ensure decode actually clears it
  ASSERT_TRUE(Decode(Encode(k), out));
  EXPECT_FALSE(out.opt.has_value());
}

TEST(MessageCodec, EncodingIsDeterministicAndStable)
{
  // A fixed message must encode to a fixed byte vector (golden) so wire drift is
  // caught. Pong is a single u32 in little-endian.
  Pong p; p.value = 0x04030201;
  const std::vector<uint8_t> bytes = Encode(p);
  const std::vector<uint8_t> golden = { 0x01, 0x02, 0x03, 0x04 };
  EXPECT_EQ(bytes, golden);
}

TEST(MessageCodec, TruncatedBufferFails)
{
  Kitchen k;
  k.text = "abc";
  std::vector<uint8_t> bytes = Encode(k);
  bytes.resize(bytes.size() - 1);   // chop a byte off the tail
  Kitchen out;
  EXPECT_FALSE(Decode(bytes, out));
}

TEST(MessageCodec, OversizedStringLengthRejectedBeforeAllocation)
{
  // A length prefix far beyond MAX_STRING_LEN (and the buffer) must be refused
  // rather than driving a huge allocation.
  Neuron::Net::DataWriter w;
  w.WriteU16(60000);
  std::string s;
  Neuron::Net::DataReader r(w.Bytes().data(), w.Bytes().size());
  EXPECT_FALSE(ReadField(r, s));
}

TEST(MessageCodec, OversizedVectorCountRejected)
{
  Neuron::Net::DataWriter w;
  w.WriteU16(60000);   // claimed element count, over MAX_VECTOR_ELEMENTS
  std::vector<uint16_t> v;
  Neuron::Net::DataReader r(w.Bytes().data(), w.Bytes().size());
  EXPECT_FALSE(ReadField(r, v));
}
