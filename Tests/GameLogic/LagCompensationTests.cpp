#include <gtest/gtest.h>

#include "GameLogic.h"   // TransformHistory, ResolvePlayerFire, components

using namespace Neuron;
using namespace Neuron::GameLogic;

namespace
{
  // A shooter: nose +z (Flight default), fires only on command.
  ECS::EntityId SpawnShooter(ECS::Registry& _w, int _laser = 20)
  {
    const ECS::EntityId e = _w.Create();
    _w.Add<WorldTransform>(e, WorldTransform{ { 0, 0, 0 } });
    _w.Add<Flight>(e, Flight{});
    _w.Add<Combatant>(e, Combatant{ Team::Player, 255, _laser, 6000, false });
    return e;
  }

  ECS::EntityId SpawnTarget(ECS::Registry& _w, int64_t _x, int64_t _y, int64_t _z, int _energy = 100)
  {
    const ECS::EntityId e = _w.Create();
    _w.Add<WorldTransform>(e, WorldTransform{ { _x, _y, _z } });
    _w.Add<Combatant>(e, Combatant{ Team::Pirate, _energy, 0, 1, false });
    return e;
  }
}

// --- the ms->ticks rewind conversion (pure arithmetic) ------------------------

TEST(LagComp, TicksBackIncludesTheInterpolationDelayEvenAtZeroRtt)
{
  // Even a 0-RTT (LAN) client renders ~one snapshot interval in the past, so the
  // rewind is at least one tick.
  EXPECT_EQ(LagCompTicks(0), 1u);
}

TEST(LagComp, TicksBackScalesWithHalfTheRoundTrip)
{
  // 200 ms RTT -> 100 ms one-way + ~33 ms interp = ~133 ms = 4 ticks at 30 Hz.
  EXPECT_EQ(LagCompTicks(200), 4u);
}

TEST(LagComp, TicksBackIsClampedToTheHistoryWindow)
{
  // A wildly over-reported RTT can never rewind past the ring - the bound that
  // caps how far a misreporting client can reach into the past.
  EXPECT_EQ(LagCompTicks(100000), LAGCOMP_HISTORY_TICKS - 1);
}

// --- the ring itself ----------------------------------------------------------

TEST(TransformHistoryRing, SamplesThePositionFromTheRequestedTickBack)
{
  ECS::Registry w;
  const ECS::EntityId e = SpawnTarget(w, 0, 0, 0);
  TransformHistory hist;

  for (int i = 0; i <= 5; ++i)
  {
    w.Get<WorldTransform>(e).position = { 100 * i, 0, 0 };
    hist.Capture(w);
  }

  Math::Vector3i64 pos;
  Math::Vector3d nose;
  ASSERT_TRUE(hist.Sample(e, /*ticksBack*/ 0, pos, nose));
  EXPECT_EQ(pos.x, 500);   // newest = the last captured position
  ASSERT_TRUE(hist.Sample(e, /*ticksBack*/ 5, pos, nose));
  EXPECT_EQ(pos.x, 0);     // five ticks back = the first captured position
}

TEST(TransformHistoryRing, ClampsToTheOldestAvailableWhenAskedTooFarBack)
{
  ECS::Registry w;
  const ECS::EntityId e = SpawnTarget(w, 0, 0, 0);
  TransformHistory hist;

  w.Get<WorldTransform>(e).position = { 10, 0, 0 };
  hist.Capture(w);
  w.Get<WorldTransform>(e).position = { 20, 0, 0 };
  hist.Capture(w);

  Math::Vector3i64 pos;
  Math::Vector3d nose;
  ASSERT_TRUE(hist.Sample(e, /*ticksBack*/ 99, pos, nose));   // only 2 samples exist
  EXPECT_EQ(pos.x, 10);   // clamped to the oldest we have
}

TEST(TransformHistoryRing, UnknownEntityHasNoHistory)
{
  ECS::Registry w;
  const ECS::EntityId e = SpawnTarget(w, 0, 0, 0);
  TransformHistory hist;   // never captured

  Math::Vector3i64 pos;
  Math::Vector3d nose;
  EXPECT_FALSE(hist.Sample(e, 0, pos, nose));
}

TEST(TransformHistoryRing, ARecycledIndexRejectsTheEarlierTenantsSample)
{
  ECS::Registry w;
  TransformHistory hist;

  const ECS::EntityId e1 = SpawnTarget(w, 111, 0, 0);
  hist.Capture(w);                 // slot holds e1's generation
  const uint32_t idx = e1.index;
  w.Destroy(e1);

  const ECS::EntityId e2 = w.Create();   // recycles the index with a bumped generation
  ASSERT_EQ(e2.index, idx);
  ASSERT_NE(e2.generation, e1.generation);
  w.Add<WorldTransform>(e2, WorldTransform{ { 222, 0, 0 } });
  w.Add<Combatant>(e2, Combatant{ Team::Pirate, 100, 0, 1, false });
  hist.Capture(w);                 // newest slot now holds e2's generation

  Math::Vector3i64 pos;
  Math::Vector3d nose;
  // e2's own newest sample resolves; the older slot (e1's generation) does not.
  ASSERT_TRUE(hist.Sample(e2, 0, pos, nose));
  EXPECT_EQ(pos.x, 222);
  EXPECT_FALSE(hist.Sample(e2, 1, pos, nose));   // that slot is a different entity's past
}

// --- the acceptance test: hit the target where the shooter SAW it -------------

TEST(LagComp, ALaterallyMovingTargetIsHittableAtItsRenderedPosition)
{
  ECS::Registry w;
  const ECS::EntityId shooter = SpawnShooter(w, /*laser*/ 20);
  const ECS::EntityId target = SpawnTarget(w, 0, 0, 4000, /*energy*/ 100);
  TransformHistory hist;

  // The target slides across the shooter's aim: straight ahead five ticks ago,
  // well off-axis now. Capture each tick's transform.
  for (int i = 0; i <= 5; ++i)
  {
    w.Get<WorldTransform>(target).position = { 600 * i, 0, 4000 };
    hist.Capture(w);
  }
  // Live position is (3000,0,4000): dot with the +z nose = 4000/5000 = 0.8 < 0.9.

  // Without compensation the shot misses - the target has left the cone.
  const FireOutcome live = ResolvePlayerFire(w, shooter, 6000, 0.9);
  EXPECT_FALSE(live.hit);
  EXPECT_EQ(w.Get<Combatant>(target).energy, 100);   // untouched

  // Rewound five ticks (to x=0, straight ahead), the same shot connects, and the
  // damage lands on the LIVE target.
  const FireOutcome comp = ResolvePlayerFire(w, shooter, 6000, 0.9, &hist, /*ticksBack*/ 5);
  EXPECT_TRUE(comp.hit);
  EXPECT_TRUE(comp.target == target);
  EXPECT_EQ(w.Get<Combatant>(target).energy, 80);    // 100 - 20, applied to the live hull
}

TEST(LagComp, ZeroTicksBackIsTheLivePositionSoAnOffAxisTargetStillMisses)
{
  ECS::Registry w;
  const ECS::EntityId shooter = SpawnShooter(w);
  const ECS::EntityId target = SpawnTarget(w, 3000, 0, 4000);
  TransformHistory hist;
  hist.Capture(w);

  // ticksBack 0 samples the newest (== live) transform, so passing history changes
  // nothing when there is no latency to compensate for.
  const FireOutcome o = ResolvePlayerFire(w, shooter, 6000, 0.9, &hist, /*ticksBack*/ 0);
  EXPECT_FALSE(o.hit);
}
