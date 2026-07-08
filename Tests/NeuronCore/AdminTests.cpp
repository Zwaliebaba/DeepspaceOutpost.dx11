#include <gtest/gtest.h>

#include <string>

#include "Messages/Defs/Admin.h"   // the ServerManager management-channel schema
#include "Messages/Framing.h"      // Neuron::Msg::PROTOCOL_VERSION
#include "Messages/Registry.h"     // MessageRegistry governance checks
#include "Messages/Serialize.h"    // Encode / Decode, MAX_STRING_LEN

using namespace Neuron::Msg;

TEST(Admin, HelloRoundTrips)
{
  AdminHello in;
  in.protocolVersion = PROTOCOL_VERSION;
  in.adminKey = "s3cr3t-key";

  AdminHello out;
  ASSERT_TRUE(Decode(Encode(in), out));
  EXPECT_EQ(out.protocolVersion, PROTOCOL_VERSION);
  EXPECT_EQ(out.adminKey, "s3cr3t-key");
}

TEST(Admin, HelloAckRoundTripsEveryField)
{
  AdminHelloAck in;
  in.adminToken = 0x0102030405060708ull;
  in.protocolVersion = PROTOCOL_VERSION;
  in.gameLogicVersion = 42;
  in.tickRateMs = 33;
  in.serverTick = 99999;
  in.gamePort = 40000;

  AdminHelloAck out;
  ASSERT_TRUE(Decode(Encode(in), out));
  EXPECT_TRUE(in.Fields() == out.Fields());
  EXPECT_EQ(out.adminToken, 0x0102030405060708ull);
  EXPECT_EQ(out.gamePort, 40000);
}

TEST(Admin, HelloRejectRoundTrips)
{
  AdminHelloReject in;
  in.reason = static_cast<uint8_t>(AdminRejectReason::BadKey);

  AdminHelloReject out;
  ASSERT_TRUE(Decode(Encode(in), out));
  EXPECT_EQ(out.reason, static_cast<uint8_t>(AdminRejectReason::BadKey));
}

TEST(Admin, PlayerInfoRoundTripsEveryField)
{
  AdminPlayerInfo in;
  in.playerId = 7;
  in.entityId = 42;
  in.name = "Cmdr JAMESON";
  in.address = 0x7F000001;   // 127.0.0.1 in host order
  in.port = 51234;
  in.rttMs = 48;
  in.score = 1500;
  in.state = static_cast<uint8_t>(AdminPlayerState::Live);
  in.connectedTick = 12345;

  AdminPlayerInfo out;
  ASSERT_TRUE(Decode(Encode(in), out));
  EXPECT_TRUE(in.Fields() == out.Fields());
  EXPECT_EQ(out.name, "Cmdr JAMESON");
  EXPECT_EQ(out.address, 0x7F000001u);
}

TEST(Admin, PlayerGoneRoundTrips)
{
  AdminPlayerGone in;
  in.playerId = 9;
  in.reason = static_cast<uint8_t>(AdminGoneReason::Disconnected);

  AdminPlayerGone out;
  ASSERT_TRUE(Decode(Encode(in), out));
  EXPECT_EQ(out.playerId, 9u);
  EXPECT_EQ(out.reason, static_cast<uint8_t>(AdminGoneReason::Disconnected));
}

TEST(Admin, EventRoundTrips)
{
  AdminEvent in;
  in.sequence = 256;
  in.tick = 4000;
  in.kind = static_cast<uint8_t>(AdminEventKind::Kill);
  in.subject = 12;
  in.text = "Cmdr JAMESON destroyed a pirate";

  AdminEvent out;
  ASSERT_TRUE(Decode(Encode(in), out));
  EXPECT_TRUE(in.Fields() == out.Fields());
  EXPECT_EQ(out.text, "Cmdr JAMESON destroyed a pirate");
}

TEST(Admin, HealthRoundTripsFloatsAndCounters)
{
  AdminHealth in;
  in.tick = 9000;
  in.uptimeSeconds = 300;
  in.avgTickMs = 12.5f;
  in.maxTickMs = 31.75f;
  in.overruns = 2;
  in.entities = 128;
  in.sessions = 7;
  in.bytesPerSecond = 65536;
  in.droppedEntities = 3;

  AdminHealth out;
  ASSERT_TRUE(Decode(Encode(in), out));
  EXPECT_TRUE(in.Fields() == out.Fields());
  EXPECT_FLOAT_EQ(out.avgTickMs, 12.5f);
  EXPECT_FLOAT_EQ(out.maxTickMs, 31.75f);
  EXPECT_EQ(out.bytesPerSecond, 65536u);
}

TEST(Admin, LongAdminKeyIsCappedByTheStringLeaf)
{
  AdminHello in;
  in.adminKey = std::string(MAX_STRING_LEN + 100, 'K');

  AdminHello out;
  ASSERT_TRUE(Decode(Encode(in), out));
  EXPECT_EQ(out.adminKey.size(), MAX_STRING_LEN);
}

// The admin messages live in the wire-visible debug/tooling id range (0x0F00+), so
// they must satisfy the same catalog governance every wire message does: unique ids,
// wire-scope/id consistency, and a declared direction.
TEST(Admin, MessagesSatisfyCatalogGovernance)
{
  MessageRegistry reg;
  reg.Add<AdminHello>("AdminHello");
  reg.Add<AdminHelloAck>("AdminHelloAck");
  reg.Add<AdminHelloReject>("AdminHelloReject");
  reg.Add<AdminPlayerInfo>("AdminPlayerInfo");
  reg.Add<AdminPlayerGone>("AdminPlayerGone");
  reg.Add<AdminEvent>("AdminEvent");
  reg.Add<AdminHealth>("AdminHealth");

  EXPECT_TRUE(reg.DuplicateIds().empty());
  EXPECT_TRUE(reg.ScopeIdConsistent());
  EXPECT_TRUE(reg.WireHaveDirection());
}
