// MiningTests - the Mine order's validation and beam cycle: pool conservation,
// hold-full stop, gear/target gating (scene.md 3.7; GameLogic/MiningSystem.h).

#include <gtest/gtest.h>

#include "SceneSystem.h"
#include "MiningSystem.h"
#include "GameLogic.h"

using namespace Neuron;
using namespace Neuron::GameLogic;

namespace
{
  ECS::EntityId MakeMiner(ECS::Registry& w, Math::Vector3i64 pos, int cap, bool laser = true)
  {
    const auto e = w.Create();
    w.Add<WorldTransform>(e, WorldTransform{ pos });
    w.Add<Flight>(e, Flight{});
    w.Add<FlightIntent>(e, FlightIntent{});
    w.Add<FlightCaps>(e, FlightCaps{});
    CargoHold h; h.capacity = cap; w.Add<CargoHold>(e, h);
    Equipment eq; eq.miningLaser = laser; w.Add<Equipment>(e, eq);
    w.Add<Owner>(e, Owner{ 1 });
    return e;
  }
  int HoldTotal(ECS::Registry& w, ECS::EntityId e)
  {
    int t = 0; auto* h = w.TryGet<CargoHold>(e);
    for (int i = 0; i < COMMODITY_COUNT; ++i) t += h->units[i];
    return t;
  }
}

// PlanUnitOrder gates Mine: needs a laser, a valid rock/belt target, and hold room.
TEST(Mining, OrderValidation)
{
  ECS::Registry w; SceneIndex idx;
  const auto belt = MaterializeScenePoi(w, idx, 1, 0, PoiKind::AsteroidBelt, { 0, 0, 0 }, 8000, 5, 12, 400, 400, 0, 0);

  // No laser -> NoGear.
  const auto noGear = MakeMiner(w, { 0, 0, 0 }, 100, /*laser*/ false);
  Msg::UnitOrder req; req.unitId = noGear.index; req.order = Msg::OrderKind::Mine; req.target = belt.index;
  EXPECT_EQ(PlanUnitOrder(w, 1, req, 1'000'000).status, Msg::OrderStatus::NoGear);

  // Laser + belt target -> Accepted.
  const auto miner = MakeMiner(w, { 0, 0, 0 }, 100, true);
  req.unitId = miner.index;
  EXPECT_EQ(PlanUnitOrder(w, 1, req, 1'000'000).status, Msg::OrderStatus::Accepted);

  // Non-minable target -> BadTarget.
  const auto rock = w.Create();
  w.Add<WorldTransform>(rock, WorldTransform{ { 0, 0, 0 } });
  req.target = w.Create().index;   // an empty entity, no OreBody/ScenePoi
  EXPECT_EQ(PlanUnitOrder(w, 1, req, 1'000'000).status, Msg::OrderStatus::BadTarget);

  // Full hold -> HoldFull.
  const auto full = MakeMiner(w, { 0, 0, 0 }, 5, true);
  w.Get<CargoHold>(full).units[0] = 5;   // exactly at capacity
  req.unitId = full.index; req.target = belt.index;
  EXPECT_EQ(PlanUnitOrder(w, 1, req, 1'000'000).status, Msg::OrderStatus::HoldFull);
}

// The crown-jewel conservation invariant: total ore mined into the hold equals the
// pool drained, exactly, across rock respawns; a dry belt yields nothing.
TEST(Mining, PoolConservation)
{
  ECS::Registry w; SceneIndex idx;
  const Math::Vector3i64 anchor{ 1'000'000, 0, 0 };
  const auto belt = MaterializeScenePoi(w, idx, 7, 3, PoiKind::AsteroidBelt, anchor, 12000, 12345, 12, 40, 40, 0, 0);
  const auto miner = MakeMiner(w, anchor, /*cap*/ 1000);
  w.Add<ActiveOrder>(miner, ActiveOrder{ Msg::OrderKind::Mine, belt.index, {}, false });

  long fromEvents = 0;
  for (uint32_t tick = 1; tick < 200000; ++tick)
  {
    if (auto* ms = w.TryGet<MiningState>(miner); ms && ms->rock != ECS::INVALID_INDEX)
      if (auto* rt = w.TryGet<WorldTransform>(w.LiveEntity(ms->rock)))
        w.Get<WorldTransform>(miner).position = rt->position;   // stay parked (isolate extraction)
    for (auto& e : StepMining(w, tick)) fromEvents += e.units;
    if (w.Get<ActiveOrder>(miner).complete) break;
  }
  EXPECT_EQ(HoldTotal(w, miner), 40);
  EXPECT_EQ(fromEvents, 40);
  EXPECT_EQ(w.TryGet<PoiResources>(belt)->units, 0);
  EXPECT_TRUE(w.Get<ActiveOrder>(miner).complete);

  // Dry belt: a fresh Mine yields nothing (no phantom ore from leftover rocks).
  w.Remove<ActiveOrder>(miner);
  w.Add<ActiveOrder>(miner, ActiveOrder{ Msg::OrderKind::Mine, belt.index, {}, false });
  long extra = 0;
  for (uint32_t t = 1; t < 2000; ++t)
  {
    if (auto* ms = w.TryGet<MiningState>(miner); ms && ms->rock != ECS::INVALID_INDEX)
      if (auto* rt = w.TryGet<WorldTransform>(w.LiveEntity(ms->rock)))
        w.Get<WorldTransform>(miner).position = rt->position;
    for (auto& e : StepMining(w, t)) extra += e.units;
    if (w.Get<ActiveOrder>(miner).complete) break;
  }
  EXPECT_EQ(extra, 0);
  EXPECT_EQ(HoldTotal(w, miner), 40);
}

// Mining stops exactly at hold capacity, draining the pool by only what was taken.
TEST(Mining, StopsWhenHoldFull)
{
  ECS::Registry w; SceneIndex idx;
  const Math::Vector3i64 anchor{ 0, 0, 0 };
  const auto belt = MaterializeScenePoi(w, idx, 1, 0, PoiKind::AsteroidBelt, anchor, 8000, 999, 12, 4000, 4000, 0, 0);
  const auto miner = MakeMiner(w, anchor, /*cap*/ 8);
  w.Add<ActiveOrder>(miner, ActiveOrder{ Msg::OrderKind::Mine, belt.index, {}, false });

  for (uint32_t tick = 1; tick < 200000; ++tick)
  {
    if (auto* ms = w.TryGet<MiningState>(miner); ms && ms->rock != ECS::INVALID_INDEX)
      if (auto* rt = w.TryGet<WorldTransform>(w.LiveEntity(ms->rock)))
        w.Get<WorldTransform>(miner).position = rt->position;
    (void)StepMining(w, tick);
    if (w.Get<ActiveOrder>(miner).complete) break;
  }
  EXPECT_EQ(HoldTotal(w, miner), 8);
  EXPECT_EQ(w.TryGet<PoiResources>(belt)->units, 4000 - 8);
}

// End-to-end through the real integrator: the miner flies to the belt, cuts rocks,
// retargets, and mines the pool out - the steering half of StepMining.
TEST(Mining, FullFlightEndToEnd)
{
  ECS::Registry w; SceneIndex idx;
  const Math::Vector3i64 anchor{ 2'000'000, 0, 0 };
  const auto belt = MaterializeScenePoi(w, idx, 5, 1, PoiKind::AsteroidBelt, anchor, 10000, 777, 12, 60, 60, 0, 0);
  const auto m = MakeMiner(w, anchor + Math::Vector3i64{ 8000, 4000, -3000 }, 1000);
  w.Add<ActiveOrder>(m, ActiveOrder{ Msg::OrderKind::Mine, belt.index, {}, false });

  const auto start = w.Get<WorldTransform>(m).position;
  bool flew = false; int cycles = 0;
  for (uint32_t tick = 1; tick < 500000; ++tick)
  {
    for (auto& e : StepMining(w, tick)) { (void)e; ++cycles; }
    GameLogic::Tick(w);
    if (w.Get<WorldTransform>(m).position.x != start.x) flew = true;
    if (w.Get<ActiveOrder>(m).complete) break;
  }
  EXPECT_TRUE(flew);
  EXPECT_GT(cycles, 0);
  EXPECT_EQ(HoldTotal(w, m), 60);
  EXPECT_EQ(w.TryGet<PoiResources>(belt)->units, 0);
  EXPECT_TRUE(w.Get<ActiveOrder>(m).complete);
}
