#include <gtest/gtest.h>

#include "ECS.h"
#include "OwnershipIndex.h"

using namespace Neuron::ECS;

namespace
{
  [[nodiscard]] bool Contains(const std::vector<EntityId>& _v, EntityId _e)
  {
    for (const EntityId x : _v)
      if (x == _e)
        return true;
    return false;
  }
}

TEST(Ownership, AddAndQueryPerOwner)
{
  Registry world;
  const EntityId a = world.Create();
  const EntityId b = world.Create();
  const EntityId c = world.Create();

  OwnershipIndex idx;
  idx.Add(1, a);
  idx.Add(1, b);
  idx.Add(2, c);

  EXPECT_EQ(idx.OwnedCount(1), 2u);
  EXPECT_EQ(idx.OwnedCount(2), 1u);
  EXPECT_EQ(idx.OwnerCount(), 2u);
  EXPECT_TRUE(Contains(idx.Owned(1), a));
  EXPECT_TRUE(Contains(idx.Owned(1), b));
  EXPECT_FALSE(Contains(idx.Owned(1), c));   // never another owner's units
  EXPECT_TRUE(idx.Owned(99).empty());        // unknown owner -> empty, no insert
  EXPECT_EQ(idx.OwnerCount(), 2u);
}

TEST(Ownership, AddIsIdempotentAndOwnerZeroIsIgnored)
{
  Registry world;
  const EntityId a = world.Create();

  OwnershipIndex idx;
  idx.Add(1, a);
  idx.Add(1, a);              // re-grant: no double-count
  EXPECT_EQ(idx.OwnedCount(1), 1u);

  idx.Add(0, a);              // 0 = "unowned" sentinel, never indexed
  EXPECT_EQ(idx.OwnedCount(0), 0u);
  EXPECT_EQ(idx.OwnerCount(), 1u);
}

TEST(Ownership, RemoveDropsOneEntityAndEmptyOwners)
{
  Registry world;
  const EntityId a = world.Create();
  const EntityId b = world.Create();

  OwnershipIndex idx;
  idx.Add(7, a);
  idx.Add(7, b);

  idx.Remove(7, a);
  EXPECT_EQ(idx.OwnedCount(7), 1u);
  EXPECT_TRUE(Contains(idx.Owned(7), b));   // the other unit survives

  idx.Remove(7, b);
  EXPECT_EQ(idx.OwnedCount(7), 0u);
  EXPECT_EQ(idx.OwnerCount(), 0u);          // last unit gone -> owner key dropped

  idx.Remove(7, a);                          // removing from a gone owner: no-op
  EXPECT_EQ(idx.OwnerCount(), 0u);
}

TEST(Ownership, StaleGenerationNeverMatchesTheRecycledSlot)
{
  Registry world;
  const EntityId original = world.Create();
  world.Destroy(original);
  const EntityId recycled = world.Create();   // same slot, bumped generation
  ASSERT_EQ(recycled.index, original.index);
  ASSERT_NE(recycled.generation, original.generation);

  OwnershipIndex idx;
  idx.Add(1, recycled);

  // A stale pre-recycle handle must not remove (or match) the new occupant.
  idx.Remove(1, original);
  EXPECT_EQ(idx.OwnedCount(1), 1u);
  EXPECT_TRUE(Contains(idx.Owned(1), recycled));
  EXPECT_FALSE(Contains(idx.Owned(1), original));

  // And the full-handle entries let callers validate against the registry: the
  // recycled handle is live, the original is not.
  EXPECT_TRUE(world.IsValid(recycled));
  EXPECT_FALSE(world.IsValid(original));
}

TEST(Ownership, ForgetDropsEveryEntryForAnOwner)
{
  Registry world;
  const EntityId a = world.Create();
  const EntityId b = world.Create();
  const EntityId c = world.Create();

  OwnershipIndex idx;
  idx.Add(3, a);
  idx.Add(3, b);
  idx.Add(4, c);

  idx.Forget(3);
  EXPECT_EQ(idx.OwnedCount(3), 0u);
  EXPECT_EQ(idx.OwnedCount(4), 1u);   // other owners untouched
  EXPECT_EQ(idx.OwnerCount(), 1u);
}
