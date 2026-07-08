#include <gtest/gtest.h>

#include <vector>

#include "DataWriter.h"
#include "Messages/Serialize.h"
#include "Messages/Registry.h"
#include "Messages/Defs/Travel.h"
#include "Messages/Defs/GalaxyChunks.h"

using namespace Neuron;

// --- Travel messages -----------------------------------------------------------

TEST(Travel, RequestRoundTripsAndMatchesGoldenBytes)
{
  Msg::TravelRequest in;
  in.kind = Msg::TravelKind::Hyperspace;
  in.systemId = 42;
  in.poiId = 7;

  const std::vector<uint8_t> payload = Msg::Encode(in);

  // Golden layout: kind u8, systemId u32 (LE), poiId u32 (LE). poiId was added for
  // the scene.md targeted-POI jump; it is always on the wire (ignored by the other
  // kinds), so the golden carries it too.
  Net::DataWriter golden;
  golden.WriteU8(1);
  golden.WriteU32(42);
  golden.WriteU32(7);
  EXPECT_EQ(payload, golden.Bytes());

  Msg::TravelRequest out;
  ASSERT_TRUE(Msg::Decode(payload, out));
  EXPECT_TRUE(out.kind == Msg::TravelKind::Hyperspace);
  EXPECT_EQ(out.systemId, 42u);
  EXPECT_EQ(out.poiId, 7u);
}

TEST(Travel, ResponseRoundTripsAndMatchesGoldenBytes)
{
  Msg::TravelResponse in;
  in.kind = Msg::TravelKind::InSystemJump;
  in.status = Msg::TravelStatus::MassLocked;

  const std::vector<uint8_t> payload = Msg::Encode(in);

  // Golden layout: kind u8, status u8.
  Net::DataWriter golden;
  golden.WriteU8(2);
  golden.WriteU8(6);
  EXPECT_EQ(payload, golden.Bytes());

  Msg::TravelResponse out;
  ASSERT_TRUE(Msg::Decode(payload, out));
  EXPECT_TRUE(out.kind == Msg::TravelKind::InSystemJump);
  EXPECT_TRUE(out.status == Msg::TravelStatus::MassLocked);
}

TEST(Travel, MessagesAreRegisteredInTheGameBand)
{
  const Msg::MessageInfo* req = Msg::GlobalRegistry().Find(Msg::TravelRequest::Id);
  const Msg::MessageInfo* resp = Msg::GlobalRegistry().Find(Msg::TravelResponse::Id);
  ASSERT_NE(req, nullptr);
  ASSERT_NE(resp, nullptr);
  EXPECT_TRUE(req->dir == Msg::Direction::ClientToServer);
  EXPECT_TRUE(resp->dir == Msg::Direction::ServerToClient);
  EXPECT_TRUE(Msg::GlobalRegistry().DuplicateIds().empty());
  EXPECT_TRUE(Msg::GlobalRegistry().ScopeIdConsistent());
}

// --- Galaxy chunk messages (nested-record codec) ---------------------------------

TEST(Travel, GalaxyChunkRequestMatchesGoldenBytes)
{
  Msg::GalaxyChunkRequest in;
  in.baseIndex = 64;
  in.count = 16;

  const std::vector<uint8_t> payload = Msg::Encode(in);

  // Golden layout: baseIndex u32, count u16 (LE).
  Net::DataWriter golden;
  golden.WriteU32(64);
  golden.WriteU16(16);
  EXPECT_EQ(payload, golden.Bytes());
}

TEST(Travel, GalaxyChunkNestedRecordsRoundTrip)
{
  Msg::GalaxyChunk in;
  in.total = 256;
  in.baseIndex = 32;
  Msg::GalaxySystemEntry e;
  e.id = 33;
  e.x = -5'000'000;
  e.y = 123;
  e.z = 99'999'999;
  e.name = "Diso";
  e.government = 7;
  e.economy = 2;
  e.techLevel = 11;
  e.population = 40;
  e.productivity = 22000;
  in.systems.push_back(e);
  e.id = 34;
  e.name = "Leesti";
  in.systems.push_back(e);

  const std::vector<uint8_t> payload = Msg::Encode(in);

  Msg::GalaxyChunk out;
  ASSERT_TRUE(Msg::Decode(payload, out));
  EXPECT_EQ(out.total, 256u);
  EXPECT_EQ(out.baseIndex, 32u);
  ASSERT_EQ(out.systems.size(), 2u);
  EXPECT_EQ(out.systems[0].id, 33u);
  EXPECT_TRUE(out.systems[0].name == "Diso");
  EXPECT_EQ(out.systems[0].x, -5'000'000);
  EXPECT_EQ(out.systems[1].id, 34u);
  EXPECT_TRUE(out.systems[1].name == "Leesti");
  EXPECT_EQ(out.systems[1].productivity, 22000);
}

TEST(Travel, TruncatedGalaxyChunkFailsDecodeSafely)
{
  Msg::GalaxyChunk in;
  in.total = 8;
  in.baseIndex = 0;
  Msg::GalaxySystemEntry e;
  e.id = 1;
  e.name = "Orerve";
  in.systems.push_back(e);

  std::vector<uint8_t> payload = Msg::Encode(in);
  payload.resize(payload.size() - 3);   // chop mid-record

  Msg::GalaxyChunk out;
  EXPECT_FALSE(Msg::Decode(payload, out));
}
