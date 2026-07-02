#include <gtest/gtest.h>

#include <string>

#include <vector>

#include "Messages/Defs/PlayerSession.h"   // ClientHello / PlayerInfo / PlayerStatus / CargoManifest
#include "Messages/Framing.h"              // Neuron::Msg::PROTOCOL_VERSION
#include "Messages/Serialize.h"            // Encode / Decode, MAX_STRING_LEN

using namespace Neuron::Msg;

TEST(PlayerSession, ClientHelloRoundTrips)
{
  ClientHello in;
  in.protocolVersion = PROTOCOL_VERSION;
  in.commanderName = "JAMESON";

  ClientHello out;
  ASSERT_TRUE(Decode(Encode(in), out));
  EXPECT_EQ(out.protocolVersion, PROTOCOL_VERSION);
  EXPECT_EQ(out.commanderName, "JAMESON");
}

TEST(PlayerSession, PlayerInfoRoundTrips)
{
  PlayerInfo in;
  in.entityId = 42;
  in.name = "Trader Jane";
  in.wantedLevel = 3;

  PlayerInfo out;
  ASSERT_TRUE(Decode(Encode(in), out));
  EXPECT_EQ(out.entityId, 42u);
  EXPECT_EQ(out.name, "Trader Jane");
  EXPECT_EQ(out.wantedLevel, 3);
}

TEST(PlayerSession, PlayerStatusRoundTripsAllFields)
{
  PlayerStatus in;
  in.energy = 200;
  in.frontShield = 24;
  in.aftShield = 18;
  in.fuel = 70;
  in.credits = 123456;
  in.missiles = 4;
  in.cargoUsed = 15;
  in.wantedLevel = 8;
  in.score = 512;

  PlayerStatus out;
  ASSERT_TRUE(Decode(Encode(in), out));
  EXPECT_TRUE(in.Fields() == out.Fields());   // every field preserved
}

TEST(PlayerSession, CargoManifestRoundTripsTheWholeHold)
{
  CargoManifest in;
  in.units = { 0, 3, 0, 0, 0, 0, 0, 7, 0, 2, 0, 0, 5, 0, 0, 0, 0 };   // 17 commodities

  CargoManifest out;
  ASSERT_TRUE(Decode(Encode(in), out));
  EXPECT_EQ(out.units, in.units);
}

TEST(PlayerSession, LongCommanderNameIsCappedByTheStringLeaf)
{
  ClientHello in;
  in.commanderName = std::string(MAX_STRING_LEN + 100, 'A');   // over the leaf cap

  ClientHello out;
  ASSERT_TRUE(Decode(Encode(in), out));
  EXPECT_EQ(out.commanderName.size(), MAX_STRING_LEN);   // truncated to the wire cap, not rejected
}
