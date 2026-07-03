#include <gtest/gtest.h>

#include <cstring>
#include <string>
#include <vector>

#include "Messages/Defs/GalaxyChunks.h"
#include "Messages/MessageEndpoint.h"
#include "Messages/Reliable.h"
#include "GalaxyGen.h"
#include "ServerSessions.h"

using namespace Neuron;

namespace
{
  Net::GalaxySystemInfo MakeSys(uint32_t _id, int64_t _x, int64_t _y, int64_t _z, const char* _name)
  {
    Net::GalaxySystemInfo s;
    s.id = _id;
    s.x = _x; s.y = _y; s.z = _z;
    for (std::size_t i = 0; _name[i] != '\0' && i < Net::GALAXY_NAME_MAX - 1; ++i)
      s.name[i] = _name[i];
    s.government = 3;
    s.economy = 5;
    s.techLevel = 9;
    s.population = 42;
    s.productivity = 1234;
    return s;
  }
}

// --- Wire entry <-> manifest struct ------------------------------------------

TEST(Manifest, WireEntryConversionRoundTrips)
{
  const Net::GalaxySystemInfo in = MakeSys(7, 100, -200, 300, "Tibedied");

  const Msg::GalaxySystemEntry e = Msg::ToWireEntry(in);
  EXPECT_TRUE(e.name == "Tibedied");

  const Net::GalaxySystemInfo out = Msg::FromWireEntry(e);
  EXPECT_EQ(out.id, in.id);
  EXPECT_EQ(out.x, in.x);
  EXPECT_EQ(out.y, in.y);
  EXPECT_EQ(out.z, in.z);
  EXPECT_TRUE(std::strcmp(out.name, in.name) == 0);
  EXPECT_EQ(out.government, in.government);
  EXPECT_EQ(out.economy, in.economy);
  EXPECT_EQ(out.techLevel, in.techLevel);
  EXPECT_EQ(out.population, in.population);
  EXPECT_EQ(out.productivity, in.productivity);
}

TEST(Manifest, OverlongWireNameIsTruncatedSafely)
{
  Msg::GalaxySystemEntry e;
  e.name = "AVeryLongSystemNameIndeed";   // longer than GALAXY_NAME_MAX

  const Net::GalaxySystemInfo out = Msg::FromWireEntry(e);
  EXPECT_EQ(std::strlen(out.name), Net::GALAXY_NAME_MAX - 1);   // clamped + terminated
}

// --- GalaxyChunk through the generic codec ------------------------------------

TEST(Manifest, ChunkRoundTripsThroughTheCodec)
{
  Msg::GalaxyChunk in;
  in.total = 99;
  in.baseIndex = 4;
  for (uint32_t i = 0; i < 3; ++i)
    in.systems.push_back(Msg::ToWireEntry(MakeSys(4 + i, i * 10, -static_cast<int64_t>(i), i * 2, "Lave")));

  const std::vector<uint8_t> bytes = Msg::Encode(in);

  Msg::GalaxyChunk out;
  EXPECT_TRUE(Msg::Decode(bytes, out));
  EXPECT_EQ(out.total, 99u);
  EXPECT_EQ(out.baseIndex, 4u);
  ASSERT_EQ(out.systems.size(), 3u);
  EXPECT_EQ(out.systems[2].id, 6u);
  EXPECT_TRUE(out.systems[2].name == "Lave");
}

TEST(Manifest, AFullChunkFitsTheSafeUdpPayload)
{
  Msg::GalaxyChunk chunk;
  chunk.total = 256;
  chunk.baseIndex = 0;
  for (uint16_t i = 0; i < Msg::GALAXY_CHUNK_MAX_SYSTEMS; ++i)
    chunk.systems.push_back(Msg::ToWireEntry(MakeSys(i, 1'000'000, -2'000'000, 3'000'000, "Zaonceatxe")));

  // Leave generous room for the reliable framing around the payload.
  EXPECT_LT(Msg::Encode(chunk).size(), static_cast<std::size_t>(Net::SAFE_UDP_PAYLOAD - 64));
}

// --- The pull protocol through ServerSessions ---------------------------------

TEST(Manifest, ChunkRequestIsAnsweredInOrderWithTotal)
{
  // A session with a 40-system manifest; the "client" end of its lanes.
  GameLogic::Session session;
  GameLogic::ServerSessions sessions;
  std::vector<Net::GalaxySystemInfo> manifest;
  for (uint32_t i = 0; i < 40; ++i)
    manifest.push_back(MakeSys(i, i * 1000, 0, 0, "Sys"));
  sessions.SetManifest(manifest);

  // Ask for the first 64 (more than exist): everything comes back, in order,
  // split into <=16-entry chunks, each carrying the total.
  sessions.SendGalaxyChunks(session, /*base*/ 0, /*count*/ 64);

  Msg::MessageEndpoint client;
  for (const std::vector<uint8_t>& dg : session.events.WriteDatagrams())
    client.OnDatagram(dg.data(), dg.size());

  std::vector<Net::GalaxySystemInfo> received;
  Net::ReliableMessage m;
  while (client.Receive(m))
  {
    Msg::GalaxyChunk chunk;
    ASSERT_TRUE(Msg::TryDecode(m, chunk));
    EXPECT_EQ(chunk.total, 40u);
    EXPECT_EQ(chunk.baseIndex, received.size());
    EXPECT_LE(chunk.systems.size(), static_cast<std::size_t>(Msg::GALAXY_CHUNK_MAX_SYSTEMS));
    for (const Msg::GalaxySystemEntry& e : chunk.systems)
      received.push_back(Msg::FromWireEntry(e));
  }

  ASSERT_EQ(received.size(), 40u);
  for (uint32_t i = 0; i < 40; ++i)
    EXPECT_EQ(received[i].id, i);
}

TEST(Manifest, OutOfRangeRequestStillTeachesTheTotal)
{
  GameLogic::Session session;
  GameLogic::ServerSessions sessions;
  sessions.SetManifest({ MakeSys(0, 0, 0, 0, "Only") });

  sessions.SendGalaxyChunks(session, /*base*/ 5, /*count*/ 16);

  Msg::MessageEndpoint client;
  for (const std::vector<uint8_t>& dg : session.events.WriteDatagrams())
    client.OnDatagram(dg.data(), dg.size());

  Net::ReliableMessage m;
  ASSERT_TRUE(client.Receive(m));
  Msg::GalaxyChunk chunk;
  ASSERT_TRUE(Msg::TryDecode(m, chunk));
  EXPECT_EQ(chunk.total, 1u);           // the client learns the size...
  EXPECT_TRUE(chunk.systems.empty());   // ...but gets no out-of-range entries
}

// --- Built from the generated galaxy ------------------------------------------

TEST(Manifest, BuiltFromGeneratedGalaxy)
{
  GameLogic::GalaxyConfig cfg;
  cfg.planetCount = 16;   // small but representative
  const std::vector<GameLogic::GalaxySystem> systems = GameLogic::GenerateGalaxy(cfg);
  const std::vector<Net::GalaxySystemInfo> manifest = GameLogic::BuildManifest(systems);

  EXPECT_TRUE(manifest.size() == systems.size());
  for (std::size_t i = 0; i < manifest.size(); ++i)
  {
    EXPECT_TRUE(manifest[i].id == systems[i].id);
    EXPECT_TRUE(manifest[i].x == systems[i].planetPos.x);
    EXPECT_TRUE(manifest[i].y == systems[i].planetPos.y);
    EXPECT_TRUE(manifest[i].z == systems[i].planetPos.z);
    EXPECT_TRUE(manifest[i].economy == static_cast<uint8_t>(systems[i].planet.economy));
    EXPECT_TRUE(manifest[i].techLevel == static_cast<uint8_t>(systems[i].planet.techLevel));
    EXPECT_TRUE(systems[i].name == std::string(manifest[i].name));
  }
}
