#include "TestFramework.h"

#include <algorithm>

#include "GameLogic.h"

using namespace Neuron;

namespace
{
  bool Contains(const Net::WorldSnapshot& _s, uint32_t _id)
  {
    for (const auto& e : _s.entities)
      if (e.id == _id)
        return true;
    return false;
  }
}

TEST(Aoi_OnlyIncludesEntitiesNearTheViewer)
{
  ECS::Registry world;

  // Cell size 100. Place entities at increasing distance along x.
  ECS::EntityId here = world.Create();    // cell 0
  world.Add<GameLogic::WorldTransform>(here, GameLogic::WorldTransform{ { 50, 0, 0 } });
  ECS::EntityId adjacent = world.Create(); // cell 1 (within radius 1)
  world.Add<GameLogic::WorldTransform>(adjacent, GameLogic::WorldTransform{ { 150, 0, 0 } });
  ECS::EntityId far = world.Create();      // cell 5 (out of range)
  world.Add<GameLogic::WorldTransform>(far, GameLogic::WorldTransform{ { 500, 0, 0 } });

  GameLogic::AreaOfInterest aoi(100);
  aoi.Rebuild(world);

  // Viewer at origin (cell 0), radius 1 cell -> sees cells -1..1.
  Net::WorldSnapshot snap = aoi.SnapshotFor(world, 1, Math::Vector3i64{ 0, 0, 0 }, 1);

  CHECK(Contains(snap, here.index));
  CHECK(Contains(snap, adjacent.index));
  CHECK(!Contains(snap, far.index));
  CHECK(snap.entities.size() == 2);
  CHECK(snap.tick == 1);
}

TEST(Aoi_RadiusZeroIsJustTheViewerCell)
{
  ECS::Registry world;
  ECS::EntityId a = world.Create();
  world.Add<GameLogic::WorldTransform>(a, GameLogic::WorldTransform{ { 10, 10, 10 } });   // cell 0
  ECS::EntityId b = world.Create();
  world.Add<GameLogic::WorldTransform>(b, GameLogic::WorldTransform{ { 110, 0, 0 } });    // cell 1

  GameLogic::AreaOfInterest aoi(100);
  aoi.Rebuild(world);

  Net::WorldSnapshot snap = aoi.SnapshotFor(world, 2, Math::Vector3i64{ 0, 0, 0 }, 0);
  CHECK(Contains(snap, a.index));
  CHECK(!Contains(snap, b.index));
  CHECK(snap.entities.size() == 1);
}

TEST(Aoi_CarriesComponentDataForVisibleEntities)
{
  ECS::Registry world;
  ECS::EntityId ship = world.Create();
  world.Add<GameLogic::WorldTransform>(ship, GameLogic::WorldTransform{ { 5, 0, 0 } });
  GameLogic::Flight f;
  f.speed = 7.0;
  world.Add<GameLogic::Flight>(ship, f);

  GameLogic::AreaOfInterest aoi(100);
  aoi.Rebuild(world);
  Net::WorldSnapshot snap = aoi.SnapshotFor(world, 3, Math::Vector3i64{ 0, 0, 0 }, 1);

  CHECK(snap.entities.size() == 1);
  CHECK(snap.entities[0].x == 5);
  CHECK(snap.entities[0].speed == 7.0f);
}

TEST(Aoi_RebuildReflectsMovement)
{
  ECS::Registry world;
  ECS::EntityId mover = world.Create();
  world.Add<GameLogic::WorldTransform>(mover, GameLogic::WorldTransform{ { 50, 0, 0 } });

  GameLogic::AreaOfInterest aoi(100);
  aoi.Rebuild(world);
  CHECK(Contains(aoi.SnapshotFor(world, 1, Math::Vector3i64{ 0, 0, 0 }, 0), mover.index));

  // Move the entity far away and rebuild: it leaves the viewer's cell.
  world.Get<GameLogic::WorldTransform>(mover).position = Math::Vector3i64{ 900, 0, 0 };
  aoi.Rebuild(world);
  CHECK(!Contains(aoi.SnapshotFor(world, 2, Math::Vector3i64{ 0, 0, 0 }, 0), mover.index));
}

TEST(Despawn_ReportsEntitiesThatVanished)
{
  GameLogic::DespawnTracker tracker;

  // First tick: nothing was tracked before, so nothing despawned.
  std::vector<uint32_t> gone = tracker.Update({ 1, 2, 3 });
  CHECK(gone.empty());
  CHECK(tracker.TrackedCount() == 3);

  // Entity 2 disappears.
  gone = tracker.Update({ 1, 3 });
  CHECK(gone.size() == 1);
  CHECK(gone[0] == 2);

  // Stable set: nothing new despawns.
  gone = tracker.Update({ 1, 3 });
  CHECK(gone.empty());

  // Everything leaves.
  gone = tracker.Update({});
  CHECK(gone.size() == 2);
  std::sort(gone.begin(), gone.end());
  CHECK(gone[0] == 1);
  CHECK(gone[1] == 3);
  CHECK(tracker.TrackedCount() == 0);
}

TEST(Despawn_NewEntitiesAreNotDespawns)
{
  GameLogic::DespawnTracker tracker;
  tracker.Update({ 1 });
  // 2 appears, 1 stays -> no despawn.
  std::vector<uint32_t> gone = tracker.Update({ 1, 2 });
  CHECK(gone.empty());
  CHECK(tracker.TrackedCount() == 2);
}
