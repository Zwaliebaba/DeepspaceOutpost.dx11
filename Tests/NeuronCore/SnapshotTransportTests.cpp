#include <gtest/gtest.h>

#include <vector>

#include "Replication.h"
#include "SnapshotPacketizer.h"
#include "SnapshotReceiver.h"

using namespace Neuron;

namespace
{
  Net::WorldSnapshot MakeSnapshot(uint32_t _tick, int _count)
  {
    Net::WorldSnapshot s;
    s.tick = _tick;
    for (int i = 0; i < _count; ++i)
    {
      Net::EntitySnapshot e;
      e.id = static_cast<uint32_t>(i);
      e.x = i * 10;
      e.y = -i;
      e.z = i * 2;
      e.noseZ = 1.0f;
      e.roofY = 1.0f;
      e.speed = static_cast<float>(i);
      s.entities.push_back(e);
    }
    return s;
  }
}

TEST(Transport, EntitiesPerDatagramFitsTheMtu)
{
  // (1200 - 12 header) / 58 per-entity = 20 whole entities.
  EXPECT_TRUE(Net::EntitiesPerDatagram(1200) == 20);
  // Never returns zero, even for a payload smaller than one entity/header.
  EXPECT_TRUE(Net::EntitiesPerDatagram(10) == 1);
  EXPECT_TRUE(Net::EntitiesPerDatagram(Net::SNAPSHOT_HEADER_SIZE + Net::SNAPSHOT_ENTITY_SIZE) == 1);
}

TEST(Transport, DatagramsStayWithinMtuAndHoldWholeEntities)
{
  std::vector<std::vector<uint8_t>> packets = Net::PacketizeSnapshot(MakeSnapshot(1, 50), 1200);

  // 50 entities at 21 per datagram -> 3 datagrams (21 + 21 + 8).
  EXPECT_TRUE(packets.size() == 3);

  std::size_t totalEntities = 0;
  for (const std::vector<uint8_t>& p : packets)
  {
    EXPECT_TRUE(p.size() <= 1200);

    // Every datagram is independently decodable and an exact multiple of the
    // entity size past the header (no entity split across packets).
    Net::DataReader r(p.data(), p.size());
    Net::WorldSnapshot part;
    EXPECT_TRUE(Net::ReadSnapshot(r, part));
    EXPECT_TRUE((p.size() - Net::SNAPSHOT_HEADER_SIZE) % Net::SNAPSHOT_ENTITY_SIZE == 0);
    totalEntities += part.entities.size();
  }
  EXPECT_TRUE(totalEntities == 50);
}

TEST(Transport, ReceiverReassemblesAllEntities)
{
  Net::WorldSnapshot snap = MakeSnapshot(9, 50);
  Net::SnapshotReceiver rx;
  for (const std::vector<uint8_t>& p : Net::PacketizeSnapshot(snap, 1200))
    EXPECT_TRUE(rx.Apply(p.data(), p.size()));

  EXPECT_TRUE(rx.Count() == 50);
  EXPECT_TRUE(rx.LatestTick() == 9);
  for (int i = 0; i < 50; ++i)
  {
    const Net::EntitySnapshot* e = rx.Get(static_cast<uint32_t>(i));
    EXPECT_TRUE(e != nullptr);
    EXPECT_TRUE(e->x == i * 10);
    EXPECT_TRUE(e->speed == static_cast<float>(i));
  }
}

TEST(Transport, StaleTickIsDroppedFreshTickWins)
{
  Net::SnapshotReceiver rx;

  auto applyOne = [&](uint32_t tick, uint32_t id, int64_t x)
  {
    Net::WorldSnapshot s;
    s.tick = tick;
    Net::EntitySnapshot e;
    e.id = id;
    e.x = x;
    s.entities.push_back(e);
    Net::DataWriter w;
    Net::WriteSnapshot(w, s);
    return rx.Apply(w.Data(), w.Size());
  };

  EXPECT_TRUE(applyOne(5, 1, 100));
  EXPECT_TRUE(rx.Get(1)->x == 100);
  EXPECT_TRUE(rx.TickOf(1) == 5);

  // A late datagram from an OLDER tick must not overwrite the fresher value.
  EXPECT_TRUE(applyOne(3, 1, 999));
  EXPECT_TRUE(rx.Get(1)->x == 100);
  EXPECT_TRUE(rx.TickOf(1) == 5);

  // A newer tick wins.
  EXPECT_TRUE(applyOne(7, 1, 200));
  EXPECT_TRUE(rx.Get(1)->x == 200);
  EXPECT_TRUE(rx.TickOf(1) == 7);
}

TEST(Transport, OutOfOrderDatagramsOfSameTickAllApply)
{
  std::vector<std::vector<uint8_t>> packets = Net::PacketizeSnapshot(MakeSnapshot(4, 50), 1200);

  // Deliver them back-to-front: same tick, so every entity still lands.
  Net::SnapshotReceiver rx;
  for (auto it = packets.rbegin(); it != packets.rend(); ++it)
    EXPECT_TRUE(rx.Apply(it->data(), it->size()));

  EXPECT_TRUE(rx.Count() == 50);
  EXPECT_TRUE(rx.Get(49) != nullptr);
  EXPECT_TRUE(rx.Get(0) != nullptr);
}

TEST(Transport, StaleEntitiesAreEvicted)
{
  Net::SnapshotReceiver rx;

  auto applyOne = [&](uint32_t tick, uint32_t id)
  {
    Net::WorldSnapshot s;
    s.tick = tick;
    Net::EntitySnapshot e;
    e.id = id;
    s.entities.push_back(e);
    Net::DataWriter w;
    Net::WriteSnapshot(w, s);
    rx.Apply(w.Data(), w.Size());
  };

  applyOne(1, 1);    // entity 1 last seen at tick 1
  applyOne(10, 2);   // entity 2 fresh; latest tick now 10
  EXPECT_TRUE(rx.Count() == 2);

  rx.EvictStale(/*maxAge*/ 5);   // 10 - 1 = 9 > 5 -> entity 1 forgotten
  EXPECT_TRUE(rx.Count() == 1);
  EXPECT_TRUE(rx.Get(1) == nullptr);
  EXPECT_TRUE(rx.Get(2) != nullptr);
}

TEST(Transport, EmptySnapshotStillSendsTickKeepAlive)
{
  std::vector<std::vector<uint8_t>> packets = Net::PacketizeSnapshot(MakeSnapshot(77, 0), 1200);
  EXPECT_TRUE(packets.size() == 1);

  Net::SnapshotReceiver rx;
  EXPECT_TRUE(rx.Apply(packets[0].data(), packets[0].size()));
  EXPECT_TRUE(rx.Count() == 0);
  EXPECT_TRUE(rx.LatestTick() == 77);
}
