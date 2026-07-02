#include <gtest/gtest.h>

#include <cmath>

#include "GameLogic.h"

using namespace Neuron;
using namespace Neuron::GameLogic;

namespace
{
  // An AI-flown NPC as SpawnDirector builds one: intent-flight + tactics.
  ECS::EntityId SpawnNpc(ECS::Registry& _w, Math::Vector3i64 _pos,
                         int _bravery = 127, int _missiles = 0, int _energy = 80)
  {
    const ECS::EntityId e = _w.Create();
    _w.Add<WorldTransform>(e, WorldTransform{ _pos });
    _w.Add<Flight>(e, Flight{});
    _w.Add<FlightIntent>(e, FlightIntent{});
    _w.Add<FlightCaps>(e, NpcFlightCaps());
    _w.Add<Combatant>(e, Combatant{ Team::Pirate, _energy, 3, 5000, true });
    _w.Add<AiPilot>(e, AiPilot{ _bravery, _missiles, /*maxEnergy*/ 80 });
    return e;
  }

  // A defenceless enemy for the NPC to hunt (a player-team target).
  ECS::EntityId SpawnPrey(ECS::Registry& _w, Math::Vector3i64 _pos)
  {
    const ECS::EntityId e = _w.Create();
    _w.Add<WorldTransform>(e, WorldTransform{ _pos });
    _w.Add<Combatant>(e, Combatant{ Team::Player, 1000000, 0, 1, false });
    return e;
  }

  // Advance the world exactly as the server does: tactics decide intents, the
  // shared input/flight systems integrate them.
  void RunSim(ECS::Registry& _w, uint32_t& _tick, uint32_t& _rng, int _ticks)
  {
    for (int i = 0; i < _ticks; ++i)
    {
      std::ignore = StepAi(_w, _tick, _rng);
      StepFlightInput(_w);
      StepFlight(_w);
      ++_tick;
    }
  }

  // Unit vector from the NPC's position to the target's.
  Math::Vector3d ToTarget(ECS::Registry& _w, ECS::EntityId _npc, ECS::EntityId _target)
  {
    const Math::Vector3i64 a = _w.Get<WorldTransform>(_npc).position;
    const Math::Vector3i64 b = _w.Get<WorldTransform>(_target).position;
    const double dx = static_cast<double>(b.x - a.x);
    const double dy = static_cast<double>(b.y - a.y);
    const double dz = static_cast<double>(b.z - a.z);
    const double len = std::sqrt(dx * dx + dy * dy + dz * dz);
    return { dx / len, dy / len, dz / len };
  }

  double DistTo(ECS::Registry& _w, ECS::EntityId _npc, ECS::EntityId _target)
  {
    const Math::Vector3i64 a = _w.Get<WorldTransform>(_npc).position;
    const Math::Vector3i64 b = _w.Get<WorldTransform>(_target).position;
    const double dx = static_cast<double>(b.x - a.x);
    const double dy = static_cast<double>(b.y - a.y);
    const double dz = static_cast<double>(b.z - a.z);
    return std::sqrt(dx * dx + dy * dy + dz * dz);
  }

  // Run the hunt and report how well the nose ever locked on and how close the
  // NPC ever got (best-over-run: near the prey the AI rightly breaks off).
  struct HuntResult
  {
    double bestAlign = -1.0;
    double bestDist = 0.0;
  };

  HuntResult Hunt(Math::Vector3i64 _preyPos, int _ticks = 900)
  {
    ECS::Registry w;
    const ECS::EntityId npc = SpawnNpc(w, { 0, 0, 0 });
    const ECS::EntityId prey = SpawnPrey(w, _preyPos);

    uint32_t tick = 0;
    uint32_t rng = 0x5EEDu;
    HuntResult r;
    r.bestDist = DistTo(w, npc, prey);
    for (int i = 0; i < _ticks; ++i)
    {
      RunSim(w, tick, rng, 1);
      const Flight& f = w.Get<Flight>(npc);
      const Math::Vector3d want = ToTarget(w, npc, prey);
      const double align = Math::Dot(want, f.nose);
      if (align > r.bestAlign) r.bestAlign = align;
      const double d = DistTo(w, npc, prey);
      if (d < r.bestDist) r.bestDist = d;
    }
    return r;
  }
}

// --- Stage 1: pursue -------------------------------------------------------

TEST(AiSystem, HuntsATargetAhead)
{
  const HuntResult r = Hunt({ 0, 0, 9000 });
  EXPECT_GT(r.bestAlign, 0.9);
  EXPECT_LT(r.bestDist, 4000.0);   // it closed most of the 9000
}

TEST(AiSystem, TurnsOntoATargetAbove)
{
  const HuntResult r = Hunt({ 0, 9000, 0 });
  EXPECT_GT(r.bestAlign, 0.9);
  EXPECT_LT(r.bestDist, 5000.0);
}

TEST(AiSystem, TurnsOntoATargetAbeam)
{
  const HuntResult r = Hunt({ 9000, 0, 0 });
  EXPECT_GT(r.bestAlign, 0.9);
  EXPECT_LT(r.bestDist, 5000.0);
}

TEST(AiSystem, HardPitchesOntoATargetAstern)
{
  const HuntResult r = Hunt({ 0, 0, -9000 });
  EXPECT_GT(r.bestAlign, 0.9);
  EXPECT_LT(r.bestDist, 5000.0);
}

TEST(AiSystem, ConvergesOnADiagonalTarget)
{
  const HuntResult r = Hunt({ 6000, -6000, 3000 });
  EXPECT_GT(r.bestAlign, 0.9);
  EXPECT_LT(r.bestDist, 5000.0);
}

// --- Stage 1: throttle & break-off ----------------------------------------

TEST(AiSystem, BrakesOnAnAlignedAttackRun)
{
  // Aligned (nose +z, target dead ahead), inside the 8192 fire distance but
  // outside the close box: the ported rule bleeds speed so the pass doesn't
  // overshoot (legacy acceleration = -1).
  ECS::Registry w;
  const ECS::EntityId npc = SpawnNpc(w, { 0, 0, 0 });
  SpawnPrey(w, { 0, 0, 3000 });

  const double before = w.Get<AiPilot>(npc).throttle;
  uint32_t rng = 1u;
  // Pick the tick that passes this ship's scheduling gate.
  std::ignore = StepAi(w, /*tick*/ npc.index & 7u, rng);

  EXPECT_LT(w.Get<AiPilot>(npc).throttle, before);
  EXPECT_EQ(w.Get<FlightIntent>(npc).throttle, w.Get<AiPilot>(npc).throttle);
}

TEST(AiSystem, BreaksOffInsideTheCloseBox)
{
  // Right on top of the prey (|lz| < 768): the legacy anti-ram rule slams the
  // throttle open (accel +3) and pitches away rather than pressing in.
  ECS::Registry w;
  const ECS::EntityId npc = SpawnNpc(w, { 0, 0, 0 });
  SpawnPrey(w, { 0, 0, 600 });

  const double before = w.Get<AiPilot>(npc).throttle;
  uint32_t rng = 1u;
  std::ignore = StepAi(w, npc.index & 7u, rng);

  EXPECT_GT(w.Get<AiPilot>(npc).throttle, before);   // full-throttle peel-away
}

TEST(AiSystem, ThrottlesUpOnADistantChase)
{
  // Beyond the fire distance, target well ahead: accel +3 every think.
  ECS::Registry w;
  const ECS::EntityId npc = SpawnNpc(w, { 0, 0, 0 });
  const ECS::EntityId prey = SpawnPrey(w, { 0, 0, 15000 });

  uint32_t tick = 0;
  uint32_t rng = 7u;
  RunSim(w, tick, rng, 200);

  EXPECT_GT(w.Get<Flight>(npc).speed, 50.0);         // it is genuinely moving
  EXPECT_LT(DistTo(w, npc, prey), 15000.0);          // and closing
}

// --- Stage 2: self-preservation --------------------------------------------

TEST(AiSystem, RegeneratesEnergyEachThink)
{
  ECS::Registry w;
  const ECS::EntityId npc = SpawnNpc(w, { 0, 0, 0 });
  w.Get<Combatant>(npc).energy = 50;

  uint32_t tick = 0;
  uint32_t rng = 1u;
  RunSim(w, tick, rng, 8);   // exactly one think in any 8-tick window

  EXPECT_EQ(w.Get<Combatant>(npc).energy, 51);
}

TEST(AiSystem, FleesAtCriticalEnergyAndEscapes)
{
  ECS::Registry w;
  const ECS::EntityId npc = SpawnNpc(w, { 0, 0, 0 }, /*bravery*/ 127, /*missiles*/ 0);
  const ECS::EntityId prey = SpawnPrey(w, { 0, 0, 4000 });

  uint32_t tick = 0;
  uint32_t rng = 0xF1EEu;

  // Keep it critically hurt until the ~10%-per-think eject roll lands.
  bool fled = false;
  for (int i = 0; i < 4000 && !fled; ++i)
  {
    w.Get<Combatant>(npc).energy = 5;   // < maxEnergy/8, held below the regen
    RunSim(w, tick, rng, 1);
    fled = w.Get<AiPilot>(npc).fleeing;
  }
  ASSERT_TRUE(fled);
  EXPECT_FALSE(w.Get<Combatant>(npc).autoEngage);   // it stopped fighting

  // It runs: nose ends up pointing away from the enemy, flat out - and once it
  // has shaken the engagement range entirely, it despawns.
  bool gone = false;
  for (int i = 0; i < 4000 && !gone; ++i)
  {
    RunSim(w, tick, rng, 1);
    gone = !w.IsValid(npc);
    if (!gone && i == 600)
    {
      const double align = Math::Dot(ToTarget(w, npc, prey), w.Get<Flight>(npc).nose);
      EXPECT_LT(align, 0.0);   // mid-flee: pointed away
    }
  }
  EXPECT_TRUE(gone);
}

TEST(AiSystem, LaunchesAPanicMissileWhenHurt)
{
  ECS::Registry w;
  // 31 missiles guarantees the legacy launch gate (missiles >= rand & 31).
  const ECS::EntityId npc = SpawnNpc(w, { 0, 0, 0 }, /*bravery*/ 127, /*missiles*/ 31);
  const ECS::EntityId prey = SpawnPrey(w, { 0, 0, 4000 });
  w.Get<Combatant>(npc).energy = 30;   // under half (40), over an eighth (10)

  uint32_t rng = 1u;
  const int launched = StepAi(w, npc.index & 7u, rng);

  EXPECT_EQ(launched, 1);
  EXPECT_EQ(w.Get<AiPilot>(npc).missiles, 30);

  int liveMissiles = 0;
  ECS::EntityId mid;
  w.Each<Missile>([&](ECS::EntityId _id, Missile&) { ++liveMissiles; mid = _id; });
  ASSERT_EQ(liveMissiles, 1);
  EXPECT_EQ(w.Get<Missile>(mid).owner, npc.index);
  EXPECT_TRUE(w.Get<Missile>(mid).target == prey);
}

// --- Scheduling, idling, determinism ---------------------------------------

TEST(AiSystem, SchedulingGateSkipsOffTicks)
{
  ECS::Registry w;
  const ECS::EntityId npc = SpawnNpc(w, { 0, 0, 0 });
  SpawnPrey(w, { 0, 9000, 0 });
  w.Get<FlightIntent>(npc).rollAxis = 0.5;   // sentinel

  uint32_t rng = 1u;
  std::ignore = StepAi(w, /*tick*/ (npc.index & 7u) ^ 1u, rng);   // gate fails

  EXPECT_EQ(w.Get<FlightIntent>(npc).rollAxis, 0.5);   // untouched
}

TEST(AiSystem, IdlesWithoutATarget)
{
  ECS::Registry w;
  const ECS::EntityId npc = SpawnNpc(w, { 0, 0, 0 });
  w.Get<FlightIntent>(npc).pitchAxis = 0.7;   // stale steering

  uint32_t rng = 1u;
  std::ignore = StepAi(w, npc.index & 7u, rng);

  EXPECT_EQ(w.Get<FlightIntent>(npc).pitchAxis, 0.0);   // levelled off
  EXPECT_EQ(w.Get<FlightIntent>(npc).rollAxis, 0.0);
  EXPECT_EQ(w.Get<FlightIntent>(npc).throttle, w.Get<AiPilot>(npc).throttle);
}

TEST(AiSystem, PoliceIgnoreCleanPlayersButHuntFugitives)
{
  ECS::Registry w;
  const ECS::EntityId cop = w.Create();
  w.Add<WorldTransform>(cop, WorldTransform{ { 0, 0, 0 } });
  w.Add<Flight>(cop, Flight{});
  w.Add<FlightIntent>(cop, FlightIntent{});
  w.Add<FlightCaps>(cop, NpcFlightCaps());
  w.Add<Combatant>(cop, Combatant{ Team::Police, 120, 4, 6000, true });
  w.Add<AiPilot>(cop, AiPilot{ 113, 1, 120 });

  const ECS::EntityId player = w.Create();
  w.Add<WorldTransform>(player, WorldTransform{ { 0, 9000, 0 } });
  w.Add<Combatant>(player, Combatant{ Team::Player, 255, 10, 6000, false });
  w.Add<PlayerTag>(player, PlayerTag{});
  w.Add<Wanted>(player, Wanted{ 0 });

  uint32_t rng = 1u;
  std::ignore = StepAi(w, cop.index & 7u, rng);
  EXPECT_EQ(w.Get<FlightIntent>(cop).pitchAxis, 0.0);   // clean player: no pursuit

  w.Get<Wanted>(player).level = 3;
  std::ignore = StepAi(w, cop.index & 7u, rng);
  EXPECT_NE(w.Get<FlightIntent>(cop).pitchAxis, 0.0);   // fugitive: turn onto them
}

TEST(AiSystem, SameSeedSameWorldIsDeterministic)
{
  auto run = []() -> Math::Vector3i64
  {
    ECS::Registry w;
    const ECS::EntityId npc = SpawnNpc(w, { 0, 0, 0 }, /*bravery*/ 100, /*missiles*/ 2);
    SpawnPrey(w, { 5000, -3000, 7000 });
    uint32_t tick = 0;
    uint32_t rng = 0xD5EEDu;
    RunSim(w, tick, rng, 500);
    return w.Get<WorldTransform>(npc).position;
  };

  const Math::Vector3i64 a = run();
  const Math::Vector3i64 b = run();
  EXPECT_EQ(a.x, b.x);
  EXPECT_EQ(a.y, b.y);
  EXPECT_EQ(a.z, b.z);
}

// --- Spawn wiring -----------------------------------------------------------

TEST(AiSystem, SpawnDirectorShipsFlyByIntent)
{
  ECS::Registry w;
  // A player to anchor the spawn.
  const ECS::EntityId player = w.Create();
  w.Add<WorldTransform>(player, WorldTransform{ { 0, 0, 0 } });
  w.Add<PlayerTag>(player, PlayerTag{});

  SpawnDirector dir(/*seed*/ 42u, /*interval*/ 1, /*maxNpcs*/ 8);
  const ECS::EntityId pirate = dir.Step(w, /*tick*/ 8);
  ASSERT_TRUE(w.IsValid(pirate));
  EXPECT_TRUE(w.Has<FlightIntent>(pirate));
  EXPECT_TRUE(w.Has<FlightCaps>(pirate));
  ASSERT_TRUE(w.Has<AiPilot>(pirate));
  EXPECT_GE(w.Get<AiPilot>(pirate).bravery, 64);
  EXPECT_LE(w.Get<AiPilot>(pirate).bravery, 127);
  EXPECT_EQ(w.Get<AiPilot>(pirate).missiles, 2);

  const std::vector<ECS::EntityId> police = dir.SpawnPolice(w, { 0, 0, 0 }, 1);
  ASSERT_EQ(police.size(), 1u);
  EXPECT_TRUE(w.Has<FlightIntent>(police[0]));
  ASSERT_TRUE(w.Has<AiPilot>(police[0]));
  EXPECT_EQ(w.Get<AiPilot>(police[0]).bravery, 113);
}
