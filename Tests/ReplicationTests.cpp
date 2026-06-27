#include "TestFramework.h"

#include "DataWriter.h"
#include "DataReader.h"
#include "Replication.h"
#include "GameLogic.h"   // SnapshotBuilder + components

using namespace Neuron;

TEST(Wire_ScalarsRoundTripLittleEndian)
{
  Net::DataWriter w;
  w.WriteU8(0x12);
  w.WriteU16(0x3456);
  w.WriteU32(0x789ABCDEu);
  w.WriteI64(-1);
  w.WriteF32(1.5f);
  w.WriteF64(-2.25);

  // U16 0x3456 must be stored low byte first.
  CHECK(w.Bytes()[1] == 0x56);
  CHECK(w.Bytes()[2] == 0x34);

  Net::DataReader r(w.Data(), w.Size());
  CHECK(r.ReadU8() == 0x12);
  CHECK(r.ReadU16() == 0x3456);
  CHECK(r.ReadU32() == 0x789ABCDEu);
  CHECK(r.ReadI64() == -1);
  CHECK(r.ReadF32() == 1.5f);
  CHECK(r.ReadF64() == -2.25);
  CHECK(r.Ok());
  CHECK(r.Remaining() == 0);
}

TEST(Wire_ReadPastEndFailsCleanly)
{
  Net::DataWriter w;
  w.WriteU16(0xABCD);

  Net::DataReader r(w.Data(), w.Size());
  CHECK(r.ReadU16() == 0xABCD);
  CHECK(r.Ok());
  (void)r.ReadU32();      // only 0 bytes left -> overrun
  CHECK(!r.Ok());
}

TEST(Replication_SnapshotRoundTrips)
{
  Net::WorldSnapshot snap;
  snap.tick = 42;
  snap.entities.push_back(Net::EntitySnapshot{
    /*id*/ 3, /*x*/ 1000, /*y*/ -2000, /*z*/ 9000,
    /*nose*/ 0.0f, 0.0f, 1.0f, /*roof*/ 0.0f, 1.0f, 0.0f, /*speed*/ 12.5f });
  snap.entities.push_back(Net::EntitySnapshot{
    7, -5, 5, 0, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, -1.0f, 0.0f });

  Net::DataWriter w;
  Net::WriteSnapshot(w, snap);

  Net::WorldSnapshot out;
  Net::DataReader r(w.Data(), w.Size());
  CHECK(Net::ReadSnapshot(r, out));
  CHECK(out.tick == 42);
  CHECK(out.entities.size() == 2);
  CHECK((out.entities[0] == snap.entities[0]));
  CHECK((out.entities[1] == snap.entities[1]));
}

TEST(Replication_BadMagicIsRejected)
{
  Net::WorldSnapshot snap;
  snap.tick = 1;
  Net::DataWriter w;
  Net::WriteSnapshot(w, snap);

  std::vector<uint8_t> bytes = w.Bytes();
  bytes[0] ^= 0xFF;   // corrupt the magic

  Net::WorldSnapshot out;
  Net::DataReader r(bytes.data(), bytes.size());
  CHECK(!Net::ReadSnapshot(r, out));
}

TEST(Replication_TruncatedPacketIsRejected)
{
  Net::WorldSnapshot snap;
  snap.tick = 1;
  snap.entities.push_back(Net::EntitySnapshot{ 1, 10, 20, 30, 0, 0, 1, 0, 1, 0, 4.0f });
  Net::DataWriter w;
  Net::WriteSnapshot(w, snap);

  // Drop the tail of the single entity: header + count parse, the entity overruns.
  Net::WorldSnapshot out;
  Net::DataReader r(w.Data(), w.Size() - 8);
  CHECK(!Net::ReadSnapshot(r, out));
}

TEST(Replication_BuildSnapshotReadsAuthoritativeComponents)
{
  ECS::Registry world;

  ECS::EntityId a = world.Create();
  world.Add<GameLogic::WorldTransform>(a, GameLogic::WorldTransform{ { 100, 200, 300 } });
  GameLogic::Flight f;
  f.nose = Math::Vector3d{ 1.0, 0.0, 0.0 };
  f.speed = 9.0;
  world.Add<GameLogic::Flight>(a, f);

  ECS::EntityId b = world.Create();
  world.Add<GameLogic::WorldTransform>(b, GameLogic::WorldTransform{ { -7, 0, 0 } });
  // b has no Flight -> default facing, speed 0

  Net::WorldSnapshot snap = GameLogic::BuildWorldSnapshot(world, 5);
  CHECK(snap.tick == 5);
  CHECK(snap.entities.size() == 2);

  auto find = [&](uint32_t id) -> const Net::EntitySnapshot*
  {
    for (const auto& e : snap.entities)
      if (e.id == id)
        return &e;
    return nullptr;
  };

  const Net::EntitySnapshot* ea = find(a.index);
  const Net::EntitySnapshot* eb = find(b.index);
  CHECK(ea != nullptr);
  CHECK(eb != nullptr);

  CHECK(ea->x == 100);
  CHECK(ea->y == 200);
  CHECK(ea->z == 300);
  CHECK(ea->noseX == 1.0f);
  CHECK(ea->noseZ == 0.0f);
  CHECK(ea->speed == 9.0f);

  CHECK(eb->x == -7);
  CHECK(eb->noseZ == 1.0f);   // default forward
  CHECK(eb->speed == 0.0f);
}
