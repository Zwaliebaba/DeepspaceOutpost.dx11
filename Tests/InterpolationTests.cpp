#include "TestFramework.h"

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

TEST(Interp_LerpsPositionBetweenTwoTicks)
{
  Net::SnapshotInterpolator interp;
  interp.Ingest(OneEntity(1, 7, 0, 0, 0));
  interp.Ingest(OneEntity(2, 7, 100, -40, 200));

  Net::EntitySnapshot s;
  CHECK(interp.Sample(7, 0.0, s));        // alpha 0 = the previous tick
  CHECK((s.x == 0 && s.y == 0 && s.z == 0));

  CHECK(interp.Sample(7, 0.5, s));        // midpoint
  CHECK((s.x == 50 && s.y == -20 && s.z == 100));

  CHECK(interp.Sample(7, 1.0, s));        // alpha 1 = the current tick
  CHECK((s.x == 100 && s.y == -40 && s.z == 200));
}

TEST(Interp_AlphaIsClampedToUnitRange)
{
  Net::SnapshotInterpolator interp;
  interp.Ingest(OneEntity(1, 1, 0, 0, 0));
  interp.Ingest(OneEntity(2, 1, 100, 0, 0));

  Net::EntitySnapshot s;
  CHECK(interp.Sample(1, 5.0, s));    // > 1 clamps to the current tick
  CHECK(s.x == 100);
  CHECK(interp.Sample(1, -3.0, s));   // < 0 clamps to the previous tick
  CHECK(s.x == 0);
}

TEST(Interp_SingleSnapshotReturnsItUnblended)
{
  Net::SnapshotInterpolator interp;
  interp.Ingest(OneEntity(4, 9, 33, 0, 0));

  Net::EntitySnapshot s;
  CHECK(interp.Sample(9, 0.5, s));    // no previous yet -> just the one we have
  CHECK(s.x == 33);
  CHECK(interp.Count() == 1);
}

TEST(Interp_StaleTickDoesNotAdvanceState)
{
  Net::SnapshotInterpolator interp;
  interp.Ingest(OneEntity(5, 2, 500, 0, 0));
  interp.Ingest(OneEntity(3, 2, 999, 0, 0));   // older -> ignored

  Net::EntitySnapshot s;
  CHECK(interp.Sample(2, 1.0, s));
  CHECK(s.x == 500);
}

TEST(Interp_FreshestOrientationIsUsedAcrossTheBlend)
{
  Net::SnapshotInterpolator interp;
  Net::WorldSnapshot a = OneEntity(1, 1, 0, 0, 0);   // nose +z
  interp.Ingest(a);
  Net::WorldSnapshot b = OneEntity(2, 1, 100, 0, 0);
  b.entities[0].noseZ = 0.0f;
  b.entities[0].noseX = 1.0f;                        // now facing +x
  interp.Ingest(b);

  Net::EntitySnapshot s;
  CHECK(interp.Sample(1, 0.25, s));
  CHECK(s.noseX == 1.0f);   // orientation comes from the freshest state...
  CHECK(s.noseZ == 0.0f);
  CHECK(s.x == 25);         // ...while position is blended
}

TEST(Interp_ApplyDecodesWireBytes)
{
  Net::DataWriter w;
  Net::WriteSnapshot(w, OneEntity(8, 3, 70, 0, 0));

  Net::SnapshotInterpolator interp;
  CHECK(interp.Apply(w.Data(), w.Size()));
  CHECK(interp.Count() == 1);
  CHECK(interp.LatestTick() == 8);

  Net::EntitySnapshot s;
  CHECK(interp.Sample(3, 0.0, s));
  CHECK(s.x == 70);
}

TEST(Interp_EvictsStaleEntities)
{
  Net::SnapshotInterpolator interp;
  interp.Ingest(OneEntity(1, 1, 0, 0, 0));
  interp.Ingest(OneEntity(10, 2, 0, 0, 0));   // latest tick now 10
  CHECK(interp.Count() == 2);

  interp.EvictStale(5);                         // entity 1 last at tick 1 -> gone
  CHECK(interp.Count() == 1);
  Net::EntitySnapshot s;
  CHECK(!interp.Sample(1, 0.0, s));
  CHECK(interp.Sample(2, 0.0, s));
}

TEST(Interp_LocalOffsetRebasesToFloatingOrigin)
{
  Net::EntitySnapshot e;
  e.x = 1000;
  e.y = 2000;
  e.z = 3000;

  Math::Vector3d local = Net::LocalOffset(e, Math::Vector3i64{ 900, 1800, 2700 });
  CHECK(local.x == 100.0);
  CHECK(local.y == 200.0);
  CHECK(local.z == 300.0);
}
