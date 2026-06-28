#include <gtest/gtest.h>

#include "DataWriter.h"
#include "DataReader.h"
#include "Replication.h"
#include "GameLogic.h"   // SnapshotBuilder + components

using namespace Neuron;

TEST(Wire, ScalarsRoundTripLittleEndian)
{
  Net::DataWriter w;
  w.WriteU8(0x12);
  w.WriteU16(0x3456);
  w.WriteU32(0x789ABCDEu);
  w.WriteI64(-1);
  w.WriteF32(1.5f);
  w.WriteF64(-2.25);

  // U16 0x3456 must be stored low byte first.
  EXPECT_TRUE(w.Bytes()[1] == 0x56);
  EXPECT_TRUE(w.Bytes()[2] == 0x34);

  Net::DataReader r(w.Data(), w.Size());
  EXPECT_TRUE(r.ReadU8() == 0x12);
  EXPECT_TRUE(r.ReadU16() == 0x3456);
  EXPECT_TRUE(r.ReadU32() == 0x789ABCDEu);
  EXPECT_TRUE(r.ReadI64() == -1);
  EXPECT_TRUE(r.ReadF32() == 1.5f);
  EXPECT_TRUE(r.ReadF64() == -2.25);
  EXPECT_TRUE(r.Ok());
  EXPECT_TRUE(r.Remaining() == 0);
}

TEST(Wire, ReadPastEndFailsCleanly)
{
  Net::DataWriter w;
  w.WriteU16(0xABCD);

  Net::DataReader r(w.Data(), w.Size());
  EXPECT_TRUE(r.ReadU16() == 0xABCD);
  EXPECT_TRUE(r.Ok());
  (void)r.ReadU32();      // only 0 bytes left -> overrun
  EXPECT_TRUE(!r.Ok());
}

TEST(Replication, SnapshotRoundTrips)
{
  Net::WorldSnapshot snap;
  snap.tick = 42;
  snap.viewerId = 7;
  snap.entities.push_back(Net::EntitySnapshot{
    /*id*/ 3, /*x*/ 1000, /*y*/ -2000, /*z*/ 9000,
    /*nose*/ 0.0f, 0.0f, 1.0f, /*roof*/ 0.0f, 1.0f, 0.0f, /*speed*/ 12.5f, /*type*/ -1 });
  snap.entities.push_back(Net::EntitySnapshot{
    7, -5, 5, 0, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, -1.0f, 0.0f, /*type*/ 2 });

  Net::DataWriter w;
  Net::WriteSnapshot(w, snap);

  Net::WorldSnapshot out;
  Net::DataReader r(w.Data(), w.Size());
  EXPECT_TRUE(Net::ReadSnapshot(r, out));
  EXPECT_TRUE(out.tick == 42);
  EXPECT_TRUE(out.viewerId == 7);
  EXPECT_TRUE(out.entities.size() == 2);
  EXPECT_TRUE((out.entities[0] == snap.entities[0]));
  EXPECT_TRUE((out.entities[1] == snap.entities[1]));
}

TEST(Replication, BadMagicIsRejected)
{
  Net::WorldSnapshot snap;
  snap.tick = 1;
  Net::DataWriter w;
  Net::WriteSnapshot(w, snap);

  std::vector<uint8_t> bytes = w.Bytes();
  bytes[0] ^= 0xFF;   // corrupt the magic

  Net::WorldSnapshot out;
  Net::DataReader r(bytes.data(), bytes.size());
  EXPECT_TRUE(!Net::ReadSnapshot(r, out));
}

TEST(Replication, TruncatedPacketIsRejected)
{
  Net::WorldSnapshot snap;
  snap.tick = 1;
  snap.entities.push_back(Net::EntitySnapshot{ 1, 10, 20, 30, 0, 0, 1, 0, 1, 0, 4.0f });
  Net::DataWriter w;
  Net::WriteSnapshot(w, snap);

  // Drop the tail of the single entity: header + count parse, the entity overruns.
  Net::WorldSnapshot out;
  Net::DataReader r(w.Data(), w.Size() - 8);
  EXPECT_TRUE(!Net::ReadSnapshot(r, out));
}

TEST(Replication, BuildSnapshotReadsAuthoritativeComponents)
{
  ECS::Registry world;

  ECS::EntityId a = world.Create();
  world.Add<GameLogic::WorldTransform>(a, GameLogic::WorldTransform{ { 100, 200, 300 } });
  GameLogic::Flight f;
  f.nose = Math::Vector3d{ 1.0, 0.0, 0.0 };
  f.speed = 9.0;
  world.Add<GameLogic::Flight>(a, f);
  world.Add<GameLogic::NetType>(a, GameLogic::NetType{ GameLogic::ShipType::Coriolis });

  ECS::EntityId b = world.Create();
  world.Add<GameLogic::WorldTransform>(b, GameLogic::WorldTransform{ { -7, 0, 0 } });
  // b has no Flight -> default facing, speed 0

  Net::WorldSnapshot snap = GameLogic::BuildWorldSnapshot(world, 5);
  EXPECT_TRUE(snap.tick == 5);
  EXPECT_TRUE(snap.entities.size() == 2);

  auto find = [&](uint32_t id) -> const Net::EntitySnapshot*
  {
    for (const auto& e : snap.entities)
      if (e.id == id)
        return &e;
    return nullptr;
  };

  const Net::EntitySnapshot* ea = find(a.index);
  const Net::EntitySnapshot* eb = find(b.index);
  EXPECT_TRUE(ea != nullptr);
  EXPECT_TRUE(eb != nullptr);

  EXPECT_TRUE(ea->x == 100);
  EXPECT_TRUE(ea->y == 200);
  EXPECT_TRUE(ea->z == 300);
  EXPECT_TRUE(ea->noseX == 1.0f);
  EXPECT_TRUE(ea->noseZ == 0.0f);
  EXPECT_TRUE(ea->speed == 9.0f);
  EXPECT_TRUE(ea->type == 2);          // Coriolis

  EXPECT_TRUE(eb->x == -7);
  EXPECT_TRUE(eb->noseZ == 1.0f);   // default forward
  EXPECT_TRUE(eb->speed == 0.0f);
  EXPECT_TRUE(eb->type == 0);          // no NetType -> default
}
