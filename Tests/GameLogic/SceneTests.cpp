// SceneTests - per-system scene templates, deterministic placement, materialization
// and regeneration (scene.md; GameLogic/SceneTemplates.h, SceneSystem.h).

#include <gtest/gtest.h>

#include <vector>

#include "GalaxyGen.h"
#include "SceneTemplates.h"
#include "SceneSystem.h"
#include "HyperspaceSystem.h"
#include "CabinHeatSystem.h"   // SUN_SYSTEM_OFFSET

using namespace Neuron;
using namespace Neuron::GameLogic;

namespace
{
  int64_t DistSq(const Math::Vector3i64& a, const Math::Vector3i64& b)
  {
    const int64_t dx = a.x - b.x, dy = a.y - b.y, dz = a.z - b.z;
    return dx * dx + dy * dy + dz * dz;
  }
}

// Every (economy, government, tech) triple matches exactly one template - the
// Default floor guarantees a match, priority breaks ties deterministically.
TEST(Scene, TemplateMatchIsTotal)
{
  for (int e = 0; e <= 7; ++e)
    for (int g = 0; g <= 7; ++g)
      for (int t = 0; t <= 15; ++t)
      {
        PlanetData pl; pl.economy = e; pl.government = g; pl.techLevel = t;
        const SceneTemplate& tm = MatchTemplate(pl);   // never throws / null
        EXPECT_GE(tm.poiCount, 1);
      }
}

// Placement honours the band + separation anti-crowding rules for every system in
// the default galaxy, and is byte-identical across runs (determinism).
TEST(Scene, PlacementInvariants)
{
  const GalaxyConfig cfg{};
  const auto systems = GenerateGalaxy(cfg);
  const int64_t bandMinSq = POI_BAND_MIN * POI_BAND_MIN;
  const int64_t bandMaxSq = POI_BAND_MAX * POI_BAND_MAX;
  const int64_t sepSq = POI_MIN_SEPARATION * POI_MIN_SEPARATION;

  int total = 0;
  for (const auto& s : systems)
  {
    const Math::Vector3i64 sun{ s.planetPos.x, s.planetPos.y + SUN_SYSTEM_OFFSET, s.planetPos.z };
    const std::vector<Math::Vector3i64> avoid{ s.stationPos, sun };
    const auto pois = PlaceScene(cfg.seed, s.id, s.planetPos, sun, avoid, MatchTemplate(s.planet));
    const auto again = PlaceScene(cfg.seed, s.id, s.planetPos, sun, avoid, MatchTemplate(s.planet));
    ASSERT_EQ(pois.size(), again.size());

    for (size_t i = 0; i < pois.size(); ++i)
    {
      ++total;
      EXPECT_EQ(pois[i].position.x, again[i].position.x);
      EXPECT_EQ(pois[i].seed, again[i].seed);
      const int64_t dP = DistSq(pois[i].position, s.planetPos);
      const int64_t dS = DistSq(pois[i].position, sun);
      EXPECT_TRUE((dP >= bandMinSq && dP <= bandMaxSq) || (dS >= bandMinSq && dS <= bandMaxSq));
      for (size_t j = 0; j < pois.size(); ++j)
        if (j != i)
          EXPECT_GE(DistSq(pois[i].position, pois[j].position), sepSq);
      for (const auto& a : avoid)
        EXPECT_GE(DistSq(pois[i].position, a), sepSq);
    }
  }
  EXPECT_GT(total, 0);
}

// Materializing a belt creates its pool + an initial rock field capped at
// BELT_MAX_ROCKS, all inside the belt radius.
TEST(Scene, MaterializeBelt)
{
  ECS::Registry w; SceneIndex idx;
  const Math::Vector3i64 anchor{ 1'000'000, 0, 0 };
  const auto belt = MaterializeScenePoi(w, idx, 7, 3, PoiKind::AsteroidBelt, anchor,
                                        12000, 12345, 12, 40, 40, 0, 0);
  const PoiResources* pool = w.TryGet<PoiResources>(belt);
  ASSERT_NE(pool, nullptr);
  EXPECT_EQ(pool->units, 40);
  EXPECT_EQ(pool->baseline, 40);

  int rocks = 0, within = 0;
  w.Each<WorldTransform, OreBody>([&](ECS::EntityId, WorldTransform& t, OreBody& ob)
  {
    ++rocks;
    EXPECT_EQ(ob.beltAnchor, belt.index);
    const int64_t ax = std::llabs(t.position.x - anchor.x);
    const int64_t ay = std::llabs(t.position.y - anchor.y);
    const int64_t az = std::llabs(t.position.z - anchor.z);
    if (ax <= 12000 && ay <= 12000 && az <= 12000) ++within;
  });
  EXPECT_GT(rocks, 0);
  EXPECT_LE(rocks, BELT_MAX_ROCKS);
  EXPECT_EQ(rocks, within);            // every rock lands inside the belt
  EXPECT_EQ(idx.Anchor(7).index, belt.index);
}

// A drained pool drifts back toward baseline and never overshoots it, repopulating
// rocks without leaking past the cap.
TEST(Scene, RegenRecoversPool)
{
  ECS::Registry w; SceneIndex idx;
  const Math::Vector3i64 anchor{ 0, 0, 0 };
  const auto belt = MaterializeScenePoi(w, idx, 1, 0, PoiKind::AsteroidBelt, anchor,
                                        12000, 99, 12, 40, 40, 0, 0);
  w.Get<PoiResources>(belt).units = 0;
  std::vector<ECS::EntityId> rm;
  w.Each<OreBody>([&](ECS::EntityId e, OreBody&) { rm.push_back(e); });
  for (auto e : rm) w.Destroy(e);

  for (uint32_t t = 1; t <= static_cast<uint32_t>(SCENE_REGEN_INTERVAL) * 20; ++t)
    StepSceneRegen(w, t);

  const int units = w.Get<PoiResources>(belt).units;
  EXPECT_GT(units, 0);
  EXPECT_LE(units, 40);
  int rocks = 0; w.Each<OreBody>([&](ECS::EntityId, OreBody&) { ++rocks; });
  EXPECT_GT(rocks, 0);
  EXPECT_LE(rocks, BELT_MAX_ROCKS);    // no rock leak
}

// Targeted jump: unknown/out-of-system POI, mass-lock gating, and a clean arrival
// placed off the anchor within the arrival offset.
TEST(Scene, TargetedJump)
{
  ECS::Registry w; SceneIndex idx;
  const Math::Vector3i64 anchor{ 5'000'000, 0, 0 };
  const auto poi = MaterializeScenePoi(w, idx, 42, 0, PoiKind::NavBeacon, anchor, 4000, 7, 0, 0, 0, 0, 0);

  const auto p = w.Create();
  w.Add<WorldTransform>(p, WorldTransform{ { 0, 0, 0 } });
  w.Add<Combatant>(p, Combatant{ Team::Player, 255, 1, 6000, false });
  EXPECT_EQ(JumpToPoi(w, p, poi, 10).status, Msg::TravelStatus::UnknownPoi);

  w.Get<WorldTransform>(p).position = anchor + Math::Vector3i64{ 50'000, 0, 0 };
  const auto tackler = w.Create();
  w.Add<WorldTransform>(tackler, WorldTransform{ w.Get<WorldTransform>(p).position + Math::Vector3i64{ 1000, 0, 0 } });
  w.Add<Combatant>(tackler, Combatant{ Team::Pirate, 80, 3, 5000, true });
  EXPECT_EQ(JumpToPoi(w, p, poi, 11).status, Msg::TravelStatus::MassLocked);

  w.Destroy(tackler);
  const auto ok = JumpToPoi(w, p, poi, 12);
  EXPECT_EQ(ok.status, Msg::TravelStatus::Jumped);
  const auto arr = w.Get<WorldTransform>(p).position;
  EXPECT_LE(std::llabs(arr.x - anchor.x), POI_ARRIVAL_OFFSET);
  EXPECT_LE(std::llabs(arr.y - anchor.y), POI_ARRIVAL_OFFSET);
  EXPECT_LE(std::llabs(arr.z - anchor.z), POI_ARRIVAL_OFFSET);
  EXPECT_FALSE(arr.x == anchor.x && arr.y == anchor.y && arr.z == anchor.z);
}
