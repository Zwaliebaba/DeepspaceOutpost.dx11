#include <gtest/gtest.h>

#include <vector>

#include "Replication.h"
#include "SnapshotBudget.h"

using namespace Neuron;

namespace
{
  Net::EntitySnapshot At(uint32_t _id, int64_t _x, int64_t _y = 0, int64_t _z = 0)
  {
    Net::EntitySnapshot e;
    e.id = _id;
    e.x = _x; e.y = _y; e.z = _z;
    return e;
  }
}

TEST(SnapshotBudget, EntityBudgetIsHeaderPlusWholeEntities)
{
  const std::size_t threeFit = Net::SNAPSHOT_HEADER_SIZE + 3 * Net::SNAPSHOT_ENTITY_SIZE;
  EXPECT_EQ(Net::SnapshotEntityBudget(threeFit), 3u);
  EXPECT_EQ(Net::SnapshotEntityBudget(Net::SNAPSHOT_HEADER_SIZE), 0u);   // no room for any entity
}

TEST(SnapshotBudget, UnderBudgetIsANoOpAndPreservesOrder)
{
  std::vector<Net::EntitySnapshot> es = { At(1, 10), At(2, 20), At(3, 30) };
  const std::vector<Net::EntitySnapshot> before = es;

  EXPECT_EQ(Net::TrimSnapshotToBudget(es, 0, 0, 0, 5), 0u);
  EXPECT_EQ(es, before);   // untouched (EntitySnapshot has a defaulted operator==)
}

TEST(SnapshotBudget, KeepsTheClosestAndDropsTheFarthest)
{
  // Viewer at the origin; entities at increasing distance.
  std::vector<Net::EntitySnapshot> es = { At(1, 1000), At(2, 100), At(3, 5000), At(4, 50) };

  const std::size_t dropped = Net::TrimSnapshotToBudget(es, 0, 0, 0, 2);

  EXPECT_EQ(dropped, 2u);
  ASSERT_EQ(es.size(), 2u);
  EXPECT_EQ(es[0].id, 4u);   // closest (dist 50), sorted first
  EXPECT_EQ(es[1].id, 2u);   // next (dist 100); id1 (1000) and id3 (5000) dropped
}

TEST(SnapshotBudget, EquidistantEntitiesBreakTiesByIdDeterministically)
{
  // All three sit 100 units from the viewer, so only the id tie-break decides who
  // survives - identically on every client.
  std::vector<Net::EntitySnapshot> es = { At(8, 100), At(3, -100), At(5, 0, 100) };

  const std::size_t dropped = Net::TrimSnapshotToBudget(es, 0, 0, 0, 2);

  EXPECT_EQ(dropped, 1u);
  ASSERT_EQ(es.size(), 2u);
  EXPECT_EQ(es[0].id, 3u);   // lowest ids kept
  EXPECT_EQ(es[1].id, 5u);   // id8 dropped
}

TEST(SnapshotBudget, DistanceUsesAllThreeAxes)
{
  // id1 is far on z only; id2 is nearer overall. With room for one, id2 survives.
  std::vector<Net::EntitySnapshot> es = { At(1, 0, 0, 9000), At(2, 100, 100, 100) };
  const std::size_t dropped = Net::TrimSnapshotToBudget(es, 0, 0, 0, 1);
  EXPECT_EQ(dropped, 1u);
  ASSERT_EQ(es.size(), 1u);
  EXPECT_EQ(es[0].id, 2u);
}
