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
  in.playerId = 7;   // the C identity key: player, not hull
  in.entityId = 42;
  in.name = "Trader Jane";
  in.wantedLevel = 3;

  PlayerInfo out;
  ASSERT_TRUE(Decode(Encode(in), out));
  EXPECT_EQ(out.playerId, 7u);
  EXPECT_EQ(out.entityId, 42u);
  EXPECT_EQ(out.name, "Trader Jane");
  EXPECT_EQ(out.wantedLevel, 3);
}

TEST(PlayerSession, HelloAckRoundTripsIdentityAndToken)
{
  HelloAck in;
  in.sessionToken = 0xA1B2C3D4E5F60708ull;
  in.playerId = 12;
  in.entityId = 99;
  in.protocolVersion = PROTOCOL_VERSION;

  HelloAck out;
  ASSERT_TRUE(Decode(Encode(in), out));
  EXPECT_TRUE(in.Fields() == out.Fields());   // every field preserved
  EXPECT_EQ(out.playerId, 12u);
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
  in.laserTemp = 90;
  in.cabinTemp = 175;   // G4

  PlayerStatus out;
  ASSERT_TRUE(Decode(Encode(in), out));
  EXPECT_TRUE(in.Fields() == out.Fields());   // every field preserved
  EXPECT_EQ(out.cabinTemp, 175);
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
