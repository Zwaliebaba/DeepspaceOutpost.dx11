#include <gtest/gtest.h>

#include "GameLogic.h"

using namespace Neuron;
using namespace Neuron::GameLogic;

namespace
{
  // A minimal orderable unit owned by `_playerId`, at `_pos`, facing +z (the default
  // Flight basis). Returns its id.
  ECS::EntityId MakeUnit(ECS::Registry& _w, uint32_t _playerId, const Math::Vector3i64& _pos)
  {
    const ECS::EntityId e = _w.Create();
    _w.Add<Owner>(e, Owner{ _playerId });
    _w.Add<WorldTransform>(e, WorldTransform{ _pos });
    _w.Add<Flight>(e, Flight{});
    _w.Add<FlightIntent>(e, FlightIntent{});
    _w.Add<FlightCaps>(e, FlightCaps{});
    _w.Add<DockState>(e, DockState{});
    return e;
  }

  double Dist(const Math::Vector3i64& _a, const Math::Vector3i64& _b)
  {
    const double dx = static_cast<double>(_b.x - _a.x);
    const double dy = static_cast<double>(_b.y - _a.y);
    const double dz = static_cast<double>(_b.z - _a.z);
    return std::sqrt(dx * dx + dy * dy + dz * dz);
  }

  Msg::UnitOrder Move(uint32_t _unit, int64_t _x, int64_t _y, int64_t _z)
  {
    Msg::UnitOrder o;
    o.unitId = _unit;
    o.order = Msg::OrderKind::Move;
    o.targetX = _x; o.targetY = _y; o.targetZ = _z;
    return o;
  }
}

// --- ClampToChebyshev (the Move anti-fling gate) --------------------------------

TEST(OrderSystem, ClampToChebyshevPassesNearPointsAndClampsFarOnes)
{
  const Math::Vector3i64 from{ 1000, -2000, 500 };
  // Inside the box on every axis: unchanged.
  EXPECT_TRUE(ClampToChebyshev(from, Math::Vector3i64{ 1500, -2500, 900 }, 1000)
              == Math::Vector3i64{ 1500, -2500, 900 });
  // Beyond on +x, -y and +z: each axis clamped to the box face, others untouched.
  const Math::Vector3i64 c = ClampToChebyshev(from, Math::Vector3i64{ 9'999'999, -9'999'999, 800 }, 1000);
  EXPECT_EQ(c.x, from.x + 1000);
  EXPECT_EQ(c.y, from.y - 1000);
  EXPECT_EQ(c.z, 800);
}

// --- PlanUnitOrder: the ownership / legality / target matrix ---------------------

TEST(OrderSystem, RejectsUnitYouDoNotOwn)
{
  ECS::Registry w;
  const ECS::EntityId mine = MakeUnit(w, /*player*/ 7, { 0, 0, 0 });
  const ECS::EntityId theirs = MakeUnit(w, /*player*/ 9, { 0, 0, 0 });

  // Player 7 ordering player 9's unit -> NotYours.
  EXPECT_TRUE(PlanUnitOrder(w, 7, Move(theirs.index, 100, 0, 0), 1'000'000).status
              == Msg::OrderStatus::NotYours);
  // Player 0 (no identity) -> NotYours even for a real unit.
  EXPECT_TRUE(PlanUnitOrder(w, 0, Move(mine.index, 100, 0, 0), 1'000'000).status
              == Msg::OrderStatus::NotYours);
  // A spoofed / dead unit index -> NotYours (no owner).
  EXPECT_TRUE(PlanUnitOrder(w, 7, Move(9999, 100, 0, 0), 1'000'000).status
              == Msg::OrderStatus::NotYours);
  // Own unit -> Accepted.
  EXPECT_TRUE(PlanUnitOrder(w, 7, Move(mine.index, 100, 0, 0), 1'000'000).status
              == Msg::OrderStatus::Accepted);
}

TEST(OrderSystem, MovePlanClampsTheDestination)
{
  ECS::Registry w;
  const ECS::EntityId u = MakeUnit(w, 1, { 0, 0, 0 });
  const OrderPlan plan = PlanUnitOrder(w, 1, Move(u.index, 5'000'000, 0, 0), 1'000'000);
  ASSERT_TRUE(plan.status == Msg::OrderStatus::Accepted);
  EXPECT_EQ(plan.order.targetPos.x, 1'000'000);   // clamped to the reach
}

TEST(OrderSystem, DockedUnitRefusesFlightOrdersButNotStop)
{
  ECS::Registry w;
  const ECS::EntityId u = MakeUnit(w, 1, { 0, 0, 0 });
  w.Get<DockState>(u).docked = true;

  EXPECT_TRUE(PlanUnitOrder(w, 1, Move(u.index, 100, 0, 0), 1'000'000).status
              == Msg::OrderStatus::Docked);

  Msg::UnitOrder stop;
  stop.unitId = u.index;
  stop.order = Msg::OrderKind::Stop;
  EXPECT_TRUE(PlanUnitOrder(w, 1, stop, 1'000'000).status == Msg::OrderStatus::Accepted);
}

TEST(OrderSystem, ReservedOrderKindsAreIllegal)
{
  ECS::Registry w;
  const ECS::EntityId u = MakeUnit(w, 1, { 0, 0, 0 });
  Msg::UnitOrder patrol;
  patrol.unitId = u.index;
  patrol.order = Msg::OrderKind::Patrol;   // reserved (F-track)
  EXPECT_TRUE(PlanUnitOrder(w, 1, patrol, 1'000'000).status == Msg::OrderStatus::Illegal);
}

TEST(OrderSystem, TargetTypeGatesPerOrderKind)
{
  ECS::Registry w;
  const ECS::EntityId u = MakeUnit(w, 1, { 0, 0, 0 });

  // A plain entity with only a transform (no station/combatant/canister markers).
  const ECS::EntityId plain = w.Create();
  w.Add<WorldTransform>(plain, WorldTransform{ { 5000, 0, 0 } });

  const ECS::EntityId station = w.Create();
  w.Add<WorldTransform>(station, WorldTransform{ { 5000, 0, 0 } });
  w.Add<ServerStation>(station, ServerStation{});

  const ECS::EntityId foe = w.Create();
  w.Add<WorldTransform>(foe, WorldTransform{ { 5000, 0, 0 } });
  w.Add<Combatant>(foe, Combatant{});

  const ECS::EntityId can = w.Create();
  w.Add<WorldTransform>(can, WorldTransform{ { 5000, 0, 0 } });
  w.Add<LootItem>(can, LootItem{});

  auto order = [&](Msg::OrderKind _k, ECS::EntityId _t)
  {
    Msg::UnitOrder o; o.unitId = u.index; o.order = _k; o.target = _t.index;
    return PlanUnitOrder(w, 1, o, 1'000'000).status;
  };

  // Right type accepts; wrong type is BadTarget.
  EXPECT_TRUE(order(Msg::OrderKind::Dock, station)  == Msg::OrderStatus::Accepted);
  EXPECT_TRUE(order(Msg::OrderKind::Dock, plain)    == Msg::OrderStatus::BadTarget);
  EXPECT_TRUE(order(Msg::OrderKind::Attack, foe)    == Msg::OrderStatus::Accepted);
  EXPECT_TRUE(order(Msg::OrderKind::Attack, plain)  == Msg::OrderStatus::BadTarget);
  EXPECT_TRUE(order(Msg::OrderKind::Collect, can)   == Msg::OrderStatus::Accepted);
  EXPECT_TRUE(order(Msg::OrderKind::Collect, foe)   == Msg::OrderStatus::BadTarget);
  EXPECT_TRUE(order(Msg::OrderKind::Approach, plain)== Msg::OrderStatus::Accepted);
  // Targeting yourself is never legal.
  EXPECT_TRUE(order(Msg::OrderKind::Approach, u)    == Msg::OrderStatus::BadTarget);
  // A dead target index -> BadTarget.
  Msg::UnitOrder dead; dead.unitId = u.index; dead.order = Msg::OrderKind::Approach; dead.target = 4242;
  EXPECT_TRUE(PlanUnitOrder(w, 1, dead, 1'000'000).status == Msg::OrderStatus::BadTarget);
}

// --- StepOrders: order -> intent execution ---------------------------------------

TEST(OrderSystem, StopZeroesTheIntent)
{
  ECS::Registry w;
  const ECS::EntityId u = MakeUnit(w, 1, { 0, 0, 0 });
  w.Get<FlightIntent>(u) = FlightIntent{ 0.9, -0.4, 0.7 };   // stale motion
  w.Add<ActiveOrder>(u, ActiveOrder{ Msg::OrderKind::Stop });

  StepOrders(w);
  const FlightIntent& fi = w.Get<FlightIntent>(u);
  EXPECT_EQ(fi.rollAxis, 0.0);
  EXPECT_EQ(fi.pitchAxis, 0.0);
  EXPECT_EQ(fi.throttle, 0.0);
}

TEST(OrderSystem, DockedUnitFliesNowhereEvenWithAStaleOrder)
{
  ECS::Registry w;
  const ECS::EntityId u = MakeUnit(w, 1, { 0, 0, 0 });
  w.Get<DockState>(u).docked = true;
  w.Add<ActiveOrder>(u, ActiveOrder{ Msg::OrderKind::Move, ECS::INVALID_INDEX, { 900'000, 0, 0 } });

  StepOrders(w);
  EXPECT_EQ(w.Get<FlightIntent>(u).throttle, 0.0);
}

TEST(OrderSystem, MoveDrivesThrottleWhenFarAndStopsOnArrival)
{
  // Target straight ahead (+z, the default nose) so this exercises the order's
  // throttle-ease + arrival-stop directly, without depending on turn convergence
  // (the shared SteerToward turning is covered by the AI tests).
  ECS::Registry w;
  const ECS::EntityId u = MakeUnit(w, 1, { 0, 0, 0 });
  ActiveOrder o{ Msg::OrderKind::Move };
  o.targetPos = { 0, 0, 30000 };
  w.Add<ActiveOrder>(u, o);

  StepOrders(w);
  EXPECT_GT(w.Get<FlightIntent>(u).throttle, 0.0);   // far: it wants to move

  // Drive the full flight pipeline until the order reports complete.
  bool arrived = false;
  for (int t = 0; t < 5000 && !arrived; ++t)
  {
    StepOrders(w);
    Tick(w);   // StepFlightInput (intent->controls) + StepFlight (integrate)
    arrived = w.Get<ActiveOrder>(u).complete;
  }
  ASSERT_TRUE(arrived);
  EXPECT_LE(Dist(w.Get<WorldTransform>(u).position, { 0, 0, 30000 }),
            static_cast<double>(ORDER_ARRIVE_RADIUS));
  EXPECT_EQ(w.Get<FlightIntent>(u).throttle, 0.0);   // holding station
}

TEST(OrderSystem, ApproachHoldsWhenTheTargetIsGone)
{
  ECS::Registry w;
  const ECS::EntityId u = MakeUnit(w, 1, { 0, 0, 0 });
  const ECS::EntityId tgt = w.Create();
  w.Add<WorldTransform>(tgt, WorldTransform{ { 50000, 0, 0 } });

  ActiveOrder o{ Msg::OrderKind::Approach };
  o.target = tgt.index;
  w.Add<ActiveOrder>(u, o);
  w.Get<FlightIntent>(u).throttle = 0.5;

  w.Destroy(tgt);   // target vanishes
  StepOrders(w);
  EXPECT_TRUE(w.Get<ActiveOrder>(u).complete);
  EXPECT_EQ(w.Get<FlightIntent>(u).throttle, 0.0);
}

TEST(OrderSystem, AttackFiresOnlyWhenAlignedAndInRange)
{
  auto attackWith = [](const Math::Vector3i64& _targetPos) -> bool
  {
    ECS::Registry w;
    const ECS::EntityId u = MakeUnit(w, 1, { 0, 0, 0 });   // nose = +z
    w.Add<Combatant>(u, Combatant{ Team::Player, 100, 10, 6000, false });

    const ECS::EntityId foe = w.Create();
    w.Add<WorldTransform>(foe, WorldTransform{ _targetPos });
    w.Add<Combatant>(foe, Combatant{ Team::Pirate });

    ActiveOrder o{ Msg::OrderKind::Attack };
    o.target = foe.index;
    w.Add<ActiveOrder>(u, o);

    const std::vector<ECS::EntityId> fire = StepOrders(w);
    // The pilot always adopts the ordered prey as its focus.
    EXPECT_EQ(w.Get<Combatant>(u).focus, foe.index);
    for (const ECS::EntityId f : fire)
      if (f.index == u.index)
        return true;
    return false;
  };

  EXPECT_TRUE(attackWith({ 0, 0, 5000 }));    // dead ahead, in range -> fire
  EXPECT_FALSE(attackWith({ 0, 0, 9000 }));   // dead ahead but out of range -> no fire
  EXPECT_FALSE(attackWith({ 5000, 0, 0 }));   // in range but 90deg off the nose -> no fire
}

TEST(OrderSystem, IsDeterministic)
{
  auto build = [](ECS::Registry& _w)
  {
    const ECS::EntityId u = MakeUnit(_w, 1, { 100, 200, 300 });
    ActiveOrder o{ Msg::OrderKind::Move };
    o.targetPos = { 40000, -12000, 7000 };
    _w.Add<ActiveOrder>(u, o);
    return u;
  };

  ECS::Registry a, b;
  const ECS::EntityId ua = build(a);
  const ECS::EntityId ub = build(b);
  for (int t = 0; t < 200; ++t) { StepOrders(a); Tick(a); StepOrders(b); Tick(b); }

  EXPECT_TRUE(a.Get<WorldTransform>(ua).position == b.Get<WorldTransform>(ub).position);
  EXPECT_EQ(a.Get<FlightIntent>(ua).throttle, b.Get<FlightIntent>(ub).throttle);
}
