#include "TestFramework.h"

#include "ECS.h"

using namespace Neuron::ECS;

namespace
{
  struct Position { int x; int y; };
  struct Velocity { int dx; int dy; };
}

TEST(Ecs_CreateAndValidate)
{
  Registry r;
  CHECK(r.AliveCount() == 0);

  EntityId a = r.Create();
  EntityId b = r.Create();
  CHECK(r.IsValid(a));
  CHECK(r.IsValid(b));
  CHECK(a != b);
  CHECK(r.AliveCount() == 2);

  EntityId none{};
  CHECK(!r.IsValid(none));
}

TEST(Ecs_DestroyInvalidatesHandle)
{
  Registry r;
  EntityId a = r.Create();
  CHECK(r.IsValid(a));

  r.Destroy(a);
  CHECK(!r.IsValid(a));
  CHECK(r.AliveCount() == 0);

  // Destroying an already-dead handle is a no-op.
  r.Destroy(a);
  CHECK(r.AliveCount() == 0);
}

TEST(Ecs_SlotRecycleBumpsGeneration)
{
  Registry r;
  EntityId a = r.Create();
  const uint32_t index = a.index;
  r.Destroy(a);

  EntityId b = r.Create();          // should reuse the freed slot
  CHECK(b.index == index);
  CHECK(b.generation != a.generation);
  CHECK(r.IsValid(b));
  CHECK(!r.IsValid(a));             // stale handle to the recycled slot
}

TEST(Ecs_AddGetHasRemove)
{
  Registry r;
  EntityId e = r.Create();

  CHECK(!r.Has<Position>(e));
  CHECK(r.TryGet<Position>(e) == nullptr);

  r.Add<Position>(e, Position{ 3, 4 });
  CHECK(r.Has<Position>(e));
  CHECK(r.Get<Position>(e).x == 3);
  CHECK(r.Get<Position>(e).y == 4);

  // Re-adding overwrites in place.
  r.Add<Position>(e, Position{ 7, 8 });
  CHECK(r.Get<Position>(e).x == 7);

  // Mutation through the reference is visible.
  r.Get<Position>(e).x = 9;
  CHECK(r.Get<Position>(e).x == 9);

  r.Remove<Position>(e);
  CHECK(!r.Has<Position>(e));
  CHECK(r.TryGet<Position>(e) == nullptr);
}

TEST(Ecs_MultipleComponentTypesAreIndependent)
{
  Registry r;
  EntityId e = r.Create();

  r.Add<Position>(e, Position{ 1, 2 });
  r.Add<Velocity>(e, Velocity{ 5, 6 });

  CHECK(r.Has<Position>(e));
  CHECK(r.Has<Velocity>(e));
  CHECK(r.Get<Velocity>(e).dx == 5);

  r.Remove<Position>(e);
  CHECK(!r.Has<Position>(e));
  CHECK(r.Has<Velocity>(e));        // removing one type leaves the other
}

TEST(Ecs_DestroyRemovesAllComponents)
{
  Registry r;
  EntityId e = r.Create();
  r.Add<Position>(e, Position{ 1, 1 });
  r.Add<Velocity>(e, Velocity{ 1, 1 });

  r.Destroy(e);
  CHECK(!r.Has<Position>(e));
  CHECK(!r.Has<Velocity>(e));

  // A new entity reusing the slot must not inherit stale components.
  EntityId e2 = r.Create();
  CHECK(!r.Has<Position>(e2));
  CHECK(!r.Has<Velocity>(e2));
}

TEST(Ecs_EachIteratesMatchingEntities)
{
  Registry r;
  EntityId a = r.Create();
  EntityId b = r.Create();
  EntityId c = r.Create();

  r.Add<Position>(a, Position{ 10, 0 });
  r.Add<Position>(b, Position{ 20, 0 });
  r.Add<Position>(c, Position{ 30, 0 });
  r.Add<Velocity>(b, Velocity{ 1, 0 });   // only b has velocity

  int positionCount = 0;
  int positionSum = 0;
  r.Each<Position>([&](EntityId, Position& p)
  {
    ++positionCount;
    positionSum += p.x;
  });
  CHECK(positionCount == 3);
  CHECK(positionSum == 60);

  int velocityCount = 0;
  r.Each<Velocity>([&](EntityId id, Velocity&)
  {
    ++velocityCount;
    CHECK(id == b);
  });
  CHECK(velocityCount == 1);
}

TEST(Ecs_EachTwoComponentsIntersects)
{
  Registry r;
  EntityId a = r.Create();   // Position only
  EntityId b = r.Create();   // Position + Velocity
  EntityId c = r.Create();   // Velocity only
  r.Add<Position>(a, Position{ 1, 1 });
  r.Add<Position>(b, Position{ 2, 2 });
  r.Add<Velocity>(b, Velocity{ 9, 0 });
  r.Add<Velocity>(c, Velocity{ 8, 0 });

  int matched = 0;
  r.Each<Position, Velocity>([&](EntityId id, Position& p, Velocity& v)
  {
    ++matched;
    CHECK(id == b);          // only b has both
    CHECK(p.x == 2);
    CHECK(v.dx == 9);
  });
  CHECK(matched == 1);
}

TEST(Ecs_RemoveMiddleKeepsOthersIntact)
{
  // Exercises the sparse-set swap-and-pop path.
  Registry r;
  EntityId a = r.Create();
  EntityId b = r.Create();
  EntityId c = r.Create();
  r.Add<Position>(a, Position{ 1, 0 });
  r.Add<Position>(b, Position{ 2, 0 });
  r.Add<Position>(c, Position{ 3, 0 });

  r.Remove<Position>(b);            // remove the middle element

  CHECK(r.Has<Position>(a));
  CHECK(!r.Has<Position>(b));
  CHECK(r.Has<Position>(c));
  CHECK(r.Get<Position>(a).x == 1);
  CHECK(r.Get<Position>(c).x == 3);

  int count = 0;
  r.Each<Position>([&](EntityId, Position&) { ++count; });
  CHECK(count == 2);
}
