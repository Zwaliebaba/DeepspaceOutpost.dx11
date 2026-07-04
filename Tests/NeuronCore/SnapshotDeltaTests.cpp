#include <gtest/gtest.h>

#include <vector>

#include "Replication.h"
#include "SnapshotDelta.h"

using namespace Neuron;

namespace
{
  // An axis-aligned, integer-valued entity - lossless through quantization, so raw
  // equality holds after a wire round trip.
  Net::EntitySnapshot Ent(uint32_t _id, int64_t _x, float _speed = 0.0f, int16_t _type = 0)
  {
    Net::EntitySnapshot e;
    e.id = _id;
    e.x = _x; e.y = 0; e.z = 0;
    e.noseZ = 1.0f; e.roofY = 1.0f;   // default frame
    e.speed = _speed;
    e.type = _type;
    return e;
  }

  bool SameEntities(const Net::WorldSnapshot& _a, const Net::WorldSnapshot& _b)
  {
    if (_a.entities.size() != _b.entities.size())
      return false;
    // Order-independent: every A entity has a wire-equal match in B.
    for (const Net::EntitySnapshot& ea : _a.entities)
    {
      bool found = false;
      for (const Net::EntitySnapshot& eb : _b.entities)
        if (ea.id == eb.id) { found = Net::SameOnWire(ea, eb); break; }
      if (!found)
        return false;
    }
    return true;
  }
}

TEST(SnapshotDelta, IdenticalSnapshotsProduceAnEmptyDelta)
{
  Net::WorldSnapshot s;
  s.tick = 5;
  s.entities = { Ent(1, 100), Ent(2, 200), Ent(3, 300) };

  const Net::DeltaSnapshot d = Net::SnapshotDiff(s, s);
  EXPECT_TRUE(d.changed.empty());
  EXPECT_TRUE(d.removed.empty());
  EXPECT_EQ(d.baselineTick, 5u);
}

TEST(SnapshotDelta, DetectsNewChangedAndRemovedEntities)
{
  Net::WorldSnapshot base;
  base.tick = 10;
  base.entities = { Ent(1, 100), Ent(2, 200), Ent(3, 300) };

  Net::WorldSnapshot cur;
  cur.tick = 11;
  cur.entities = { Ent(1, 100), Ent(2, 250), Ent(4, 400) };   // 1 same, 2 moved, 3 gone, 4 new

  const Net::DeltaSnapshot d = Net::SnapshotDiff(base, cur);

  // changed = {2 (moved), 4 (new)}
  ASSERT_EQ(d.changed.size(), 2u);
  bool has2 = false, has4 = false;
  for (const auto& e : d.changed) { has2 |= (e.id == 2); has4 |= (e.id == 4); }
  EXPECT_TRUE(has2);
  EXPECT_TRUE(has4);
  // removed = {3}
  ASSERT_EQ(d.removed.size(), 1u);
  EXPECT_EQ(d.removed[0], 3u);
}

TEST(SnapshotDelta, ASubQuantumOrientationWobbleIsNotAChange)
{
  Net::WorldSnapshot base;
  base.entities = { Ent(1, 100) };
  Net::WorldSnapshot cur = base;
  // Nudge the nose by far less than one 1/32767 quantization step: same on the wire.
  cur.entities[0].noseX += 1.0e-6f;

  const Net::DeltaSnapshot d = Net::SnapshotDiff(base, cur);
  EXPECT_TRUE(d.changed.empty());
  EXPECT_TRUE(d.removed.empty());
}

TEST(SnapshotDelta, ApplyReconstructsTheCurrentSnapshot)
{
  Net::WorldSnapshot base;
  base.tick = 10;
  base.entities = { Ent(1, 100), Ent(2, 200), Ent(3, 300) };

  Net::WorldSnapshot cur;
  cur.tick = 11;
  cur.entities = { Ent(1, 100), Ent(2, 250), Ent(4, 400) };

  const Net::DeltaSnapshot d = Net::SnapshotDiff(base, cur);
  const Net::WorldSnapshot rebuilt = Net::ApplyDelta(base, d);

  EXPECT_EQ(rebuilt.tick, 11u);
  EXPECT_TRUE(SameEntities(rebuilt, cur));
}

TEST(SnapshotDelta, AStationaryEntityIsSuppressedEvenAsTheViewerReferenceMoves)
{
  // The reference origin tracks the moving viewer, so a stationary entity's OFFSET
  // differs between ticks - but its ABSOLUTE position doesn't, so it must not appear
  // in the delta, and Apply must still place it at the right absolute position.
  Net::WorldSnapshot base;
  base.tick = 1;
  base.refX = 1000;
  base.entities = { Ent(1, 5000) };   // absolute x = 5000 (offset 4000 from ref 1000)

  Net::WorldSnapshot cur;
  cur.tick = 2;
  cur.refX = 1100;                    // viewer moved +100
  cur.entities = { Ent(1, 5000) };    // still absolute 5000 (offset now 3900)

  const Net::DeltaSnapshot d = Net::SnapshotDiff(base, cur);
  EXPECT_TRUE(d.changed.empty());     // unchanged despite the different offset
  EXPECT_TRUE(d.removed.empty());

  const Net::WorldSnapshot rebuilt = Net::ApplyDelta(base, d);
  ASSERT_EQ(rebuilt.entities.size(), 1u);
  EXPECT_EQ(rebuilt.entities[0].x, 5000);   // absolute position preserved
  EXPECT_EQ(rebuilt.refX, 1100);            // and it carries the new reference
}

TEST(SnapshotDelta, WireRoundTripPreservesChangedAndRemoved)
{
  Net::DeltaSnapshot d;
  d.tick = 42;
  d.viewerId = 7;
  d.refX = 900'000'000'000;   // a far-from-origin reference (unbounded int64 world)
  d.baselineTick = 41;
  d.changed = { Ent(2, d.refX + 250, /*speed*/ 8.0f, /*type*/ 16), Ent(4, d.refX + 400) };
  d.removed = { 3, 9 };

  Net::DataWriter w;
  Net::WriteSnapshotDelta(w, d);

  Net::DataReader r(w.Bytes().data(), w.Size());
  Net::DeltaSnapshot got;
  ASSERT_TRUE(Net::ReadSnapshotDelta(r, got));

  EXPECT_EQ(got.tick, 42u);
  EXPECT_EQ(got.viewerId, 7u);
  EXPECT_EQ(got.baselineTick, 41u);
  EXPECT_EQ(got.refX, 900'000'000'000);
  ASSERT_EQ(got.changed.size(), 2u);
  EXPECT_EQ(got.changed[0].id, 2u);
  EXPECT_EQ(got.changed[0].x, d.refX + 250);   // exact absolute position across the wire
  EXPECT_FLOAT_EQ(got.changed[0].speed, 8.0f);
  EXPECT_EQ(got.changed[0].type, 16);
  ASSERT_EQ(got.removed.size(), 2u);
  EXPECT_EQ(got.removed[0], 3u);
  EXPECT_EQ(got.removed[1], 9u);
}

TEST(SnapshotDelta, FullDiffApplyRoundTripsThroughTheWire)
{
  Net::WorldSnapshot base;
  base.tick = 10;
  base.entities = { Ent(1, 100), Ent(2, 200), Ent(3, 300) };

  Net::WorldSnapshot cur;
  cur.tick = 11;
  cur.entities = { Ent(1, 100), Ent(2, 250), Ent(4, 400) };

  // Server side: diff, serialize.
  Net::DataWriter w;
  Net::WriteSnapshotDelta(w, Net::SnapshotDiff(base, cur));

  // Client side: decode, apply to its own copy of the baseline.
  Net::DataReader r(w.Bytes().data(), w.Size());
  Net::DeltaSnapshot got;
  ASSERT_TRUE(Net::ReadSnapshotDelta(r, got));
  const Net::WorldSnapshot rebuilt = Net::ApplyDelta(base, got);

  EXPECT_TRUE(SameEntities(rebuilt, cur));
}

TEST(SnapshotDelta, PeekVersionDistinguishesFullFromDelta)
{
  Net::WorldSnapshot full;
  full.entities = { Ent(1, 100) };
  Net::DataWriter fw;
  Net::WriteSnapshot(fw, full);
  EXPECT_EQ(Net::PeekSnapshotVersion(fw.Bytes().data(), fw.Size()), Net::SNAPSHOT_VERSION);

  Net::DeltaSnapshot d;
  d.changed = { Ent(1, 100) };
  Net::DataWriter dw;
  Net::WriteSnapshotDelta(dw, d);
  EXPECT_EQ(Net::PeekSnapshotVersion(dw.Bytes().data(), dw.Size()), Net::SNAPSHOT_DELTA_VERSION);
}
