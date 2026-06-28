#include <gtest/gtest.h>

#include <cstring>
#include <vector>

#include "GalaxyManifest.h"
#include "ReliableChannel.h"
#include "GalaxyGen.h"

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

TEST(Manifest, ChunkRoundTrips)
{
  std::vector<Net::GalaxySystemInfo> in;
  in.push_back(MakeSys(0, 10, -20, 30, "LAVE"));
  in.push_back(MakeSys(7, 9'000'000'000LL, -1, 5, "DISO"));

  std::vector<uint8_t> bytes = Net::EncodeManifestChunk(99, 4, in.data(), static_cast<uint16_t>(in.size()));

  Net::ReliableMessage m;
  m.type = static_cast<uint16_t>(Net::EventType::GalaxyManifest);
  m.payload = bytes;

  uint32_t total = 0, base = 0;
  std::vector<Net::GalaxySystemInfo> out;
  EXPECT_TRUE(Net::DecodeManifestChunk(m, total, base, out));
  EXPECT_TRUE(total == 99);
  EXPECT_TRUE(base == 4);
  EXPECT_TRUE(out.size() == 2);

  EXPECT_TRUE(out[0].id == 0);
  EXPECT_TRUE((out[0].x == 10 && out[0].y == -20 && out[0].z == 30));
  EXPECT_TRUE(std::strcmp(out[0].name, "LAVE") == 0);
  EXPECT_TRUE(out[0].government == 3);
  EXPECT_TRUE(out[0].economy == 5);
  EXPECT_TRUE(out[0].techLevel == 9);
  EXPECT_TRUE(out[0].population == 42);
  EXPECT_TRUE(out[0].productivity == 1234);

  EXPECT_TRUE(out[1].id == 7);
  EXPECT_TRUE(out[1].x == 9'000'000'000LL);   // a value well beyond 32 bits survives
  EXPECT_TRUE(std::strcmp(out[1].name, "DISO") == 0);
}

TEST(Manifest, WrongTypeRejected)
{
  Net::ReliableMessage m;
  m.type = static_cast<uint16_t>(Net::EventType::Chat);
  uint32_t total = 0, base = 0;
  std::vector<Net::GalaxySystemInfo> out;
  EXPECT_TRUE(!Net::DecodeManifestChunk(m, total, base, out));
}

TEST(Manifest, ChunkFitsTheSafeUdpPayload)
{
  EXPECT_TRUE(Net::MANIFEST_SYSTEMS_PER_CHUNK >= 1);
  const std::size_t maxPacket =
      Net::MANIFEST_CHUNK_HEADER + Net::MANIFEST_SYSTEMS_PER_CHUNK * Net::MANIFEST_ENTRY_SIZE;
  EXPECT_TRUE(maxPacket <= Net::SAFE_UDP_PAYLOAD);
}

TEST(Manifest, StreamsThroughReliableChannelInOrder)
{
  // Build a manifest larger than one chunk so the chunking path is exercised.
  std::vector<Net::GalaxySystemInfo> systems;
  const uint32_t count = static_cast<uint32_t>(Net::MANIFEST_SYSTEMS_PER_CHUNK * 2 + 3);
  for (uint32_t i = 0; i < count; ++i)
    systems.push_back(MakeSys(i, static_cast<int64_t>(i) * 100, 0, 0, "SYS"));

  Net::ReliableChannel server;
  Net::ReliableChannel client;
  Net::SendManifest(server, systems);

  // Pump packets server->client until everything is acked (bounded loop).
  std::vector<Net::GalaxySystemInfo> received;
  for (int round = 0; round < 50 && server.PendingOutgoing() > 0; ++round)
  {
    std::vector<uint8_t> pkt = server.WritePacket();
    client.ReadPacket(pkt.data(), pkt.size());

    Net::ReliableMessage m;
    while (client.Receive(m))
    {
      uint32_t total = 0, base = 0;
      EXPECT_TRUE(Net::DecodeManifestChunk(m, total, base, received));
      EXPECT_TRUE(total == count);
    }
    std::vector<uint8_t> ack = client.WritePacket();
    server.ReadPacket(ack.data(), ack.size());
  }

  EXPECT_TRUE(received.size() == count);
  for (uint32_t i = 0; i < count; ++i)
  {
    EXPECT_TRUE(received[i].id == i);
    EXPECT_TRUE(received[i].x == static_cast<int64_t>(i) * 100);
  }
}

TEST(Manifest, BuiltFromGeneratedGalaxy)
{
  GameLogic::GalaxyConfig cfg;
  cfg.planetCount = 8;
  const std::vector<GameLogic::GalaxySystem> systems = GameLogic::GenerateGalaxy(cfg);
  const std::vector<Net::GalaxySystemInfo> manifest = GameLogic::BuildManifest(systems);

  EXPECT_TRUE(manifest.size() == systems.size());
  for (std::size_t i = 0; i < systems.size(); ++i)
  {
    EXPECT_TRUE(manifest[i].id == systems[i].id);
    EXPECT_TRUE(manifest[i].x == systems[i].planetPos.x);
    EXPECT_TRUE(manifest[i].y == systems[i].planetPos.y);
    EXPECT_TRUE(manifest[i].z == systems[i].planetPos.z);
    EXPECT_TRUE(manifest[i].economy == static_cast<uint8_t>(systems[i].planet.economy));
    EXPECT_TRUE(manifest[i].techLevel == static_cast<uint8_t>(systems[i].planet.techLevel));
    // Names are short (<= the wire limit), so they carry across intact.
    EXPECT_TRUE(systems[i].name == std::string(manifest[i].name));
  }
}
