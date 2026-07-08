#include <gtest/gtest.h>

#include <algorithm>
#include <cstdint>
#include <vector>

#include "GameLogic.h"

using namespace Neuron;

// Golden-run determinism (ARCHITECTURE.md Section 8): "same seed, same world =>
// bit-identical positions". The whole replay / reconciliation strategy rests on the
// simulation being bit-reproducible, so this drives the full GameLogic::Tick
// pipeline (launch-cruise -> flight-intent -> flight integrate -> motion) over a
// large seed-derived scene and asserts two independent runs land on bit-identical
// state. It is a cross-system integration test: a regression that made the tick
// order-dependent, left a field uninitialised, or let the compiler contract floats
// (losing /fp:strict) would diverge here even while the per-system unit tests pass.

namespace
{
  // A deterministic scene built purely from `_seed`: same seed => identical world
  // (identical entities, in identical creation order, with identical components).
  void BuildScene(ECS::Registry& _world, uint32_t _seed)
  {
    uint32_t rng = _seed;
    auto next = [&rng]() { rng = rng * 1664525u + 1013904223u; return rng; };
    auto axis = [&next]() { return static_cast<double>(next() & 0xFFFFu) / 65535.0 * 2.0 - 1.0; }; // [-1,1]
    auto throttle = [&next]() { return static_cast<double>(next() & 0xFFFFu) / 65535.0; };          // [0,1]
    auto coord = [&next]() { return static_cast<int64_t>(next() % 20001u) - 10000; };                // [-10000,10000]

    // Steered ships: exercise StepFlightInput (intent -> controls) + StepFlight
    // (the float orientation/position integrator, the part most exposed to
    // contraction/ordering nondeterminism).
    for (int i = 0; i < 48; ++i)
    {
      const ECS::EntityId e = _world.Create();
      _world.Add<GameLogic::WorldTransform>(e, GameLogic::WorldTransform{ { coord(), coord(), coord() } });
      _world.Add<GameLogic::Flight>(e, GameLogic::Flight{});
      _world.Add<GameLogic::FlightIntent>(e, GameLogic::FlightIntent{ axis(), axis(), throttle() });
      if (next() & 1u)   // ~half get a custom envelope so caps probing varies
        _world.Add<GameLogic::FlightCaps>(e, GameLogic::FlightCaps{ 0.05 + throttle() * 0.1,
                                                                    0.05 + throttle() * 0.1,
                                                                    20.0 + throttle() * 80.0 });
    }

    // Plain velocity movers: exercise StepMotion.
    for (int i = 0; i < 16; ++i)
    {
      const ECS::EntityId e = _world.Create();
      _world.Add<GameLogic::WorldTransform>(e, GameLogic::WorldTransform{ { coord(), coord(), coord() } });
      _world.Add<GameLogic::Velocity>(e, GameLogic::Velocity{ { static_cast<int64_t>(next() % 21u) - 10,
                                                                static_cast<int64_t>(next() % 21u) - 10,
                                                                static_cast<int64_t>(next() % 21u) - 10 } });
    }

    // Launching hulls: exercise StepLaunchCruise (the fly-out + component removal).
    for (int i = 0; i < 4; ++i)
    {
      const Math::Vector3i64 origin{ coord(), coord(), coord() };
      const ECS::EntityId e = _world.Create();
      _world.Add<GameLogic::WorldTransform>(e, GameLogic::WorldTransform{ origin });
      _world.Add<GameLogic::Flight>(e, GameLogic::Flight{});
      _world.Add<GameLogic::FlightIntent>(e, GameLogic::FlightIntent{});
      _world.Add<GameLogic::LaunchCruise>(e, GameLogic::LaunchCruise{ origin, GameLogic::LAUNCH_OFFSET, 0.35 });
    }
  }

  struct Sample
  {
    uint32_t id;
    Math::Vector3i64 pos;
    GameLogic::Flight flight;
  };

  // Capture every entity's authoritative state, in a stable (id-sorted) order so two
  // runs are compared element-for-element regardless of iteration order.
  std::vector<Sample> Snapshot(ECS::Registry& _world)
  {
    std::vector<Sample> out;
    _world.Each<GameLogic::WorldTransform>([&](ECS::EntityId _id, GameLogic::WorldTransform& _t)
    {
      Sample s;
      s.id = _id.index;
      s.pos = _t.position;
      const GameLogic::Flight* f = _world.TryGet<GameLogic::Flight>(_id);
      s.flight = (f != nullptr) ? *f : GameLogic::Flight{};
      out.push_back(s);
    });
    std::sort(out.begin(), out.end(), [](const Sample& _a, const Sample& _b) { return _a.id < _b.id; });
    return out;
  }

  // Bit-exact flight comparison (determinism means identical bits, so == is right).
  bool SameFlight(const GameLogic::Flight& _a, const GameLogic::Flight& _b)
  {
    return _a.side.x == _b.side.x && _a.side.y == _b.side.y && _a.side.z == _b.side.z
        && _a.roof.x == _b.roof.x && _a.roof.y == _b.roof.y && _a.roof.z == _b.roof.z
        && _a.nose.x == _b.nose.x && _a.nose.y == _b.nose.y && _a.nose.z == _b.nose.z
        && _a.roll == _b.roll && _a.pitch == _b.pitch && _a.speed == _b.speed
        && _a.carry.x == _b.carry.x && _a.carry.y == _b.carry.y && _a.carry.z == _b.carry.z;
  }
}

TEST(Determinism, TickPipelineIsBitIdenticalAcrossRuns)
{
  constexpr uint32_t seed = 0xABCDEF01u;
  constexpr int ticks = 200;

  ECS::Registry a, b;
  BuildScene(a, seed);
  BuildScene(b, seed);

  const std::vector<Sample> initial = Snapshot(a);   // to prove the sim actually moved

  for (int i = 0; i < ticks; ++i)
  {
    GameLogic::Tick(a);
    GameLogic::Tick(b);
  }

  const std::vector<Sample> sa = Snapshot(a);
  const std::vector<Sample> sb = Snapshot(b);

  ASSERT_EQ(sa.size(), sb.size());
  ASSERT_EQ(sa.size(), initial.size());

  std::size_t moved = 0;
  for (std::size_t i = 0; i < sa.size(); ++i)
  {
    EXPECT_EQ(sa[i].id, sb[i].id);
    EXPECT_TRUE(sa[i].pos == sb[i].pos) << "position diverged for entity " << sa[i].id;
    EXPECT_TRUE(SameFlight(sa[i].flight, sb[i].flight)) << "flight diverged for entity " << sa[i].id;
    if (!(sa[i].pos == initial[i].pos))
      ++moved;
  }

  // Guard against a trivial pass: the scene must not be static (throttled ships and
  // velocity movers advance), or the equality above would be meaningless.
  EXPECT_GT(moved, 0u);
}

TEST(Determinism, DifferentSeedsDiverge)
{
  constexpr int ticks = 200;

  ECS::Registry a, b;
  BuildScene(a, 0x11111111u);
  BuildScene(b, 0x22222222u);

  for (int i = 0; i < ticks; ++i)
  {
    GameLogic::Tick(a);
    GameLogic::Tick(b);
  }

  const std::vector<Sample> sa = Snapshot(a);
  const std::vector<Sample> sb = Snapshot(b);

  // Same entity shape (the scene structure is seed-independent)...
  ASSERT_EQ(sa.size(), sb.size());

  // ...but the seed-driven initial conditions must lead to a different world, so the
  // determinism test above is proving reproducibility, not comparing constants.
  bool anyDifferent = false;
  for (std::size_t i = 0; i < sa.size() && !anyDifferent; ++i)
    anyDifferent = !(sa[i].pos == sb[i].pos);
  EXPECT_TRUE(anyDifferent);
}
