#include <gtest/gtest.h>

#include "Replication.h"
#include "SnapshotInterpolator.h"

using namespace Neuron;

namespace
{
  Net::WorldSnapshot OneEntity(uint32_t _tick, uint32_t _id, int64_t _x, int64_t _y, int64_t _z)
  {
    Net::WorldSnapshot s;
    s.tick = _tick;
    Net::EntitySnapshot e;
    e.id = _id;
    e.x = _x;
    e.y = _y;
    e.z = _z;
    e.noseZ = 1.0f;
    e.roofY = 1.0f;
    s.entities.push_back(e);
    return s;
  }
}

TEST(Interp, LerpsPositionBetweenTwoTicks)
{
  Net::SnapshotInterpolator interp;
  interp.Ingest(OneEntity(1, 7, 0, 0, 0));
  interp.Ingest(OneEntity(2, 7, 100, -40, 200));

  Net::EntitySnapshot s;
  EXPECT_TRUE(interp.Sample(7, 0.0, s));        // alpha 0 = the previous tick
  EXPECT_TRUE((s.x == 0 && s.y == 0 && s.z == 0));

  EXPECT_TRUE(interp.Sample(7, 0.5, s));        // midpoint
  EXPECT_TRUE((s.x == 50 && s.y == -20 && s.z == 100));

  EXPECT_TRUE(interp.Sample(7, 1.0, s));        // alpha 1 = the current tick
  EXPECT_TRUE((s.x == 100 && s.y == -40 && s.z == 200));
}

TEST(Interp, AlphaIsClampedToUnitRange)
{
  Net::SnapshotInterpolator interp;
  interp.Ingest(OneEntity(1, 1, 0, 0, 0));
  interp.Ingest(OneEntity(2, 1, 100, 0, 0));

  Net::EntitySnapshot s;
  EXPECT_TRUE(interp.Sample(1, 5.0, s));    // > 1 clamps to the current tick
  EXPECT_TRUE(s.x == 100);
  EXPECT_TRUE(interp.Sample(1, -3.0, s));   // < 0 clamps to the previous tick
  EXPECT_TRUE(s.x == 0);
}

TEST(Interp, SingleSnapshotReturnsItUnblended)
{
  Net::SnapshotInterpolator interp;
  interp.Ingest(OneEntity(4, 9, 33, 0, 0));

  Net::EntitySnapshot s;
  EXPECT_TRUE(interp.Sample(9, 0.5, s));    // no previous yet -> just the one we have
  EXPECT_TRUE(s.x == 33);
  EXPECT_TRUE(interp.Count() == 1);
}

TEST(Interp, StaleTickDoesNotAdvanceState)
{
  Net::SnapshotInterpolator interp;
  interp.Ingest(OneEntity(5, 2, 500, 0, 0));
  interp.Ingest(OneEntity(3, 2, 999, 0, 0));   // older -> ignored

  Net::EntitySnapshot s;
  EXPECT_TRUE(interp.Sample(2, 1.0, s));
  EXPECT_TRUE(s.x == 500);
}

TEST(Interp, FreshestOrientationIsUsedAcrossTheBlend)
{
  Net::SnapshotInterpolator interp;
  Net::WorldSnapshot a = OneEntity(1, 1, 0, 0, 0);   // nose +z
  interp.Ingest(a);
  Net::WorldSnapshot b = OneEntity(2, 1, 100, 0, 0);
  b.entities[0].noseZ = 0.0f;
  b.entities[0].noseX = 1.0f;                        // now facing +x
  interp.Ingest(b);

  Net::EntitySnapshot s;
  EXPECT_TRUE(interp.Sample(1, 0.25, s));
  EXPECT_TRUE(s.noseX == 1.0f);   // orientation comes from the freshest state...
  EXPECT_TRUE(s.noseZ == 0.0f);
  EXPECT_TRUE(s.x == 25);         // ...while position is blended
}

TEST(Interp, ApplyDecodesWireBytes)
{
  Net::DataWriter w;
  Net::WriteSnapshot(w, OneEntity(8, 3, 70, 0, 0));

  Net::SnapshotInterpolator interp;
  EXPECT_TRUE(interp.Apply(w.Data(), w.Size()));
  EXPECT_TRUE(interp.Count() == 1);
  EXPECT_TRUE(interp.LatestTick() == 8);

  Net::EntitySnapshot s;
  EXPECT_TRUE(interp.Sample(3, 0.0, s));
  EXPECT_TRUE(s.x == 70);
}

TEST(Interp, EvictsStaleEntities)
{
  Net::SnapshotInterpolator interp;
  interp.Ingest(OneEntity(1, 1, 0, 0, 0));
  interp.Ingest(OneEntity(10, 2, 0, 0, 0));   // latest tick now 10
  EXPECT_TRUE(interp.Count() == 2);

  interp.EvictStale(5);                         // entity 1 last at tick 1 -> gone
  EXPECT_TRUE(interp.Count() == 1);
  Net::EntitySnapshot s;
  EXPECT_TRUE(!interp.Sample(1, 0.0, s));
  EXPECT_TRUE(interp.Sample(2, 0.0, s));
}

TEST(Interp, LearnsViewerIdFromSnapshots)
{
  Net::SnapshotInterpolator interp;
  EXPECT_TRUE(interp.ViewerId() == 0xFFFFFFFFu);   // unknown until a snapshot arrives

  Net::WorldSnapshot s = OneEntity(1, 5, 0, 0, 0);
  s.viewerId = 5;                            // server tells us we are entity 5
  interp.Ingest(s);
  EXPECT_TRUE(interp.ViewerId() == 5);
}

TEST(Interp, LocalOffsetRebasesToFloatingOrigin)
{
  Net::EntitySnapshot e;
  e.x = 1000;
  e.y = 2000;
  e.z = 3000;

  Math::Vector3d local = Net::LocalOffset(e, Math::Vector3i64{ 900, 1800, 2700 });
  EXPECT_TRUE(local.x == 100.0);
  EXPECT_TRUE(local.y == 200.0);
  EXPECT_TRUE(local.z == 300.0);
}
