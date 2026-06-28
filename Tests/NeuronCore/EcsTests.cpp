#include <gtest/gtest.h>

#include "ECS.h"

using namespace Neuron::ECS;

namespace
{
  struct Position { int x; int y; };
  struct Velocity { int dx; int dy; };
}

TEST(Ecs, CreateAndValidate)
{
  Registry r;
  EXPECT_TRUE(r.AliveCount() == 0);

  EntityId a = r.Create();
  EntityId b = r.Create();
  EXPECT_TRUE(r.IsValid(a));
  EXPECT_TRUE(r.IsValid(b));
  EXPECT_TRUE(a != b);
  EXPECT_TRUE(r.AliveCount() == 2);

  EntityId none{};
  EXPECT_TRUE(!r.IsValid(none));
}

TEST(Ecs, DestroyInvalidatesHandle)
{
  Registry r;
  EntityId a = r.Create();
  EXPECT_TRUE(r.IsValid(a));

  r.Destroy(a);
  EXPECT_TRUE(!r.IsValid(a));
  EXPECT_TRUE(r.AliveCount() == 0);

  // Destroying an already-dead handle is a no-op.
  r.Destroy(a);
  EXPECT_TRUE(r.AliveCount() == 0);
}

TEST(Ecs, SlotRecycleBumpsGeneration)
{
  Registry r;
  EntityId a = r.Create();
  const uint32_t index = a.index;
  r.Destroy(a);

  EntityId b = r.Create();          // should reuse the freed slot
  EXPECT_TRUE(b.index == index);
  EXPECT_TRUE(b.generation != a.generation);
  EXPECT_TRUE(r.IsValid(b));
  EXPECT_TRUE(!r.IsValid(a));             // stale handle to the recycled slot
}

TEST(Ecs, AddGetHasRemove)
{
  Registry r;
  EntityId e = r.Create();

  EXPECT_TRUE(!r.Has<Position>(e));
  EXPECT_TRUE(r.TryGet<Position>(e) == nullptr);

  r.Add<Position>(e, Position{ 3, 4 });
  EXPECT_TRUE(r.Has<Position>(e));
  EXPECT_TRUE(r.Get<Position>(e).x == 3);
  EXPECT_TRUE(r.Get<Position>(e).y == 4);

  // Re-adding overwrites in place.
  r.Add<Position>(e, Position{ 7, 8 });
  EXPECT_TRUE(r.Get<Position>(e).x == 7);

  // Mutation through the reference is visible.
  r.Get<Position>(e).x = 9;
  EXPECT_TRUE(r.Get<Position>(e).x == 9);

  r.Remove<Position>(e);
  EXPECT_TRUE(!r.Has<Position>(e));
  EXPECT_TRUE(r.TryGet<Position>(e) == nullptr);
}

TEST(Ecs, MultipleComponentTypesAreIndependent)
{
  Registry r;
  EntityId e = r.Create();

  r.Add<Position>(e, Position{ 1, 2 });
  r.Add<Velocity>(e, Velocity{ 5, 6 });

  EXPECT_TRUE(r.Has<Position>(e));
  EXPECT_TRUE(r.Has<Velocity>(e));
  EXPECT_TRUE(r.Get<Velocity>(e).dx == 5);

  r.Remove<Position>(e);
  EXPECT_TRUE(!r.Has<Position>(e));
  EXPECT_TRUE(r.Has<Velocity>(e));        // removing one type leaves the other
}

TEST(Ecs, DestroyRemovesAllComponents)
{
  Registry r;
  EntityId e = r.Create();
  r.Add<Position>(e, Position{ 1, 1 });
  r.Add<Velocity>(e, Velocity{ 1, 1 });

  r.Destroy(e);
  EXPECT_TRUE(!r.Has<Position>(e));
  EXPECT_TRUE(!r.Has<Velocity>(e));

  // A new entity reusing the slot must not inherit stale components.
  EntityId e2 = r.Create();
  EXPECT_TRUE(!r.Has<Position>(e2));
  EXPECT_TRUE(!r.Has<Velocity>(e2));
}

TEST(Ecs, EachIteratesMatchingEntities)
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
  EXPECT_TRUE(positionCount == 3);
  EXPECT_TRUE(positionSum == 60);

  int velocityCount = 0;
  r.Each<Velocity>([&](EntityId id, Velocity&)
  {
    ++velocityCount;
    EXPECT_TRUE(id == b);
  });
  EXPECT_TRUE(velocityCount == 1);
}

TEST(Ecs, ComponentReferencesStableWithoutAddRemove)
{
  // The local_objects[] proxy relies on this: as long as no component of a type
  // is added/removed, references (and pointers like &local_objects[un]) stay
  // valid across arbitrarily many Get calls and mutations.
  Registry r;
  EntityId ids[20];
  for (int i = 0; i < 20; ++i)
  {
    ids[i] = r.Create();
    r.Add<Position>(ids[i], Position{ i, 0 });
  }

  Position* p5 = &r.Get<Position>(ids[5]);
  Position* p17 = &r.Get<Position>(ids[17]);

  // Lots of reads/mutations - no add/remove, so nothing reallocates.
  for (int k = 0; k < 200; ++k)
    r.Get<Position>(ids[k % 20]).y = k;

  EXPECT_TRUE(&r.Get<Position>(ids[5]) == p5);     // same address
  EXPECT_TRUE(&r.Get<Position>(ids[17]) == p17);
  EXPECT_TRUE(p5->x == 5);
  EXPECT_TRUE(p17->x == 17);

  // Mutating through the cached pointer is visible through the registry.
  p5->x = 999;
  EXPECT_TRUE(r.Get<Position>(ids[5]).x == 999);
}

TEST(Ecs, EachTwoComponentsIntersects)
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
    EXPECT_TRUE(id == b);          // only b has both
    EXPECT_TRUE(p.x == 2);
    EXPECT_TRUE(v.dx == 9);
  });
  EXPECT_TRUE(matched == 1);
}

TEST(Ecs, RemoveMiddleKeepsOthersIntact)
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

  EXPECT_TRUE(r.Has<Position>(a));
  EXPECT_TRUE(!r.Has<Position>(b));
  EXPECT_TRUE(r.Has<Position>(c));
  EXPECT_TRUE(r.Get<Position>(a).x == 1);
  EXPECT_TRUE(r.Get<Position>(c).x == 3);

  int count = 0;
  r.Each<Position>([&](EntityId, Position&) { ++count; });
  EXPECT_TRUE(count == 2);
}

TEST(Ecs, LiveEntityResolvesIndexToTheCurrentHandle)
{
  Registry r;
  EntityId a = r.Create();
  EntityId b = r.Create();

  // A live index resolves back to the exact handle (index + generation).
  EXPECT_TRUE(r.LiveEntity(a.index) == a);
  EXPECT_TRUE(r.LiveEntity(b.index) == b);

  // A dead slot resolves to an invalid handle...
  r.Destroy(b);
  EXPECT_TRUE(!r.IsValid(r.LiveEntity(b.index)));

  // ...and once that slot is recycled, LiveEntity tracks the NEW generation, so the
  // stale handle no longer matches.
  EntityId c = r.Create();              // reuses b's index, bumped generation
  EXPECT_TRUE(c.index == b.index);
  EXPECT_TRUE(r.LiveEntity(c.index) == c);
  EXPECT_TRUE(r.LiveEntity(c.index) != b);

  // An out-of-range index is invalid.
  EXPECT_TRUE(!r.IsValid(r.LiveEntity(9999)));
}
