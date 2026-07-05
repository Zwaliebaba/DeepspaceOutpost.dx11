#include <gtest/gtest.h>

#include <vector>

#include "DataWriter.h"
#include "Messages/Serialize.h"
#include "Messages/Registry.h"
#include "Messages/Defs/UnitOrder.h"

using namespace Neuron;

TEST(UnitOrder, RequestRoundTripsAndMatchesGoldenBytes)
{
  Msg::UnitOrder in;
  in.unitId = 42;
  in.order = Msg::OrderKind::Move;
  in.target = 0xFFFFFFFFu;
  in.targetX = -5'000'000;
  in.targetY = 123;
  in.targetZ = 9'000'000'000ll;   // beyond int32: the field is a real int64

  const std::vector<uint8_t> payload = Msg::Encode(in);

  // Golden layout: unitId u32, order u8, target u32, targetX/Y/Z i64 (LE).
  Net::DataWriter golden;
  golden.WriteU32(42);
  golden.WriteU8(static_cast<uint8_t>(Msg::OrderKind::Move));
  golden.WriteU32(0xFFFFFFFFu);
  golden.WriteI64(-5'000'000);
  golden.WriteI64(123);
  golden.WriteI64(9'000'000'000ll);
  EXPECT_EQ(payload, golden.Bytes());

  Msg::UnitOrder out;
  ASSERT_TRUE(Msg::Decode(payload, out));
  EXPECT_EQ(out.unitId, 42u);
  EXPECT_TRUE(out.order == Msg::OrderKind::Move);
  EXPECT_EQ(out.target, 0xFFFFFFFFu);
  EXPECT_EQ(out.targetX, -5'000'000);
  EXPECT_EQ(out.targetY, 123);
  EXPECT_EQ(out.targetZ, 9'000'000'000ll);
}

TEST(UnitOrder, AckRoundTripsAndMatchesGoldenBytes)
{
  Msg::UnitOrderAck in;
  in.unitId = 7;
  in.order = Msg::OrderKind::Attack;
  in.status = Msg::OrderStatus::NotYours;

  const std::vector<uint8_t> payload = Msg::Encode(in);

  Net::DataWriter golden;
  golden.WriteU32(7);
  golden.WriteU8(static_cast<uint8_t>(Msg::OrderKind::Attack));
  golden.WriteU8(static_cast<uint8_t>(Msg::OrderStatus::NotYours));
  EXPECT_EQ(payload, golden.Bytes());

  Msg::UnitOrderAck out;
  ASSERT_TRUE(Msg::Decode(payload, out));
  EXPECT_EQ(out.unitId, 7u);
  EXPECT_TRUE(out.order == Msg::OrderKind::Attack);
  EXPECT_TRUE(out.status == Msg::OrderStatus::NotYours);
}

TEST(UnitOrder, AbilityRequestRoundTripsAndMatchesGoldenBytes)
{
  Msg::AbilityRequest in;
  in.kind = Msg::AbilityKind::FireMissile;
  in.target = 99;

  const std::vector<uint8_t> payload = Msg::Encode(in);

  Net::DataWriter golden;
  golden.WriteU8(static_cast<uint8_t>(Msg::AbilityKind::FireMissile));
  golden.WriteU32(99);
  EXPECT_EQ(payload, golden.Bytes());

  Msg::AbilityRequest out;
  ASSERT_TRUE(Msg::Decode(payload, out));
  EXPECT_TRUE(out.kind == Msg::AbilityKind::FireMissile);
  EXPECT_EQ(out.target, 99u);
}

TEST(UnitOrder, TruncatedRequestFailsDecodeSafely)
{
  Msg::UnitOrder in;
  in.unitId = 1;
  in.order = Msg::OrderKind::Approach;
  std::vector<uint8_t> payload = Msg::Encode(in);
  payload.resize(payload.size() - 4);   // chop into the last int64

  Msg::UnitOrder out;
  EXPECT_FALSE(Msg::Decode(payload, out));
}

TEST(UnitOrder, MessagesAreRegisteredInTheGameBandWithCorrectDirection)
{
  const Msg::MessageInfo* ord = Msg::GlobalRegistry().Find(Msg::UnitOrder::Id);
  const Msg::MessageInfo* ack = Msg::GlobalRegistry().Find(Msg::UnitOrderAck::Id);
  const Msg::MessageInfo* abl = Msg::GlobalRegistry().Find(Msg::AbilityRequest::Id);
  ASSERT_NE(ord, nullptr);
  ASSERT_NE(ack, nullptr);
  ASSERT_NE(abl, nullptr);
  EXPECT_TRUE(ord->dir == Msg::Direction::ClientToServer);
  EXPECT_TRUE(ack->dir == Msg::Direction::ServerToClient);
  EXPECT_TRUE(abl->dir == Msg::Direction::ClientToServer);
  // All three ride the reliable Gameplay lane (a dropped command must not vanish).
  EXPECT_TRUE(ord->lane == Msg::MessageLane::Gameplay);
  EXPECT_TRUE(ack->lane == Msg::MessageLane::Gameplay);
  EXPECT_TRUE(abl->lane == Msg::MessageLane::Gameplay);
  // Governance holds with the new ids in the catalog.
  EXPECT_TRUE(Msg::GlobalRegistry().DuplicateIds().empty());
  EXPECT_TRUE(Msg::GlobalRegistry().ScopeIdConsistent());
  EXPECT_TRUE(Msg::GlobalRegistry().WireHaveDirection());
}
