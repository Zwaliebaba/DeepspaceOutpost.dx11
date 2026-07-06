#include <gtest/gtest.h>

#include "input/Selection.h"

using namespace Neuron::Input;

namespace
{
  // An ownership predicate that treats a single id as "mine" (the pre-F1 rule,
  // id == LocalPlayer). A stateless lambda in the tests below.
  auto OnlyOwn(uint32_t _mine)
  {
    return [_mine](uint32_t _id) { return _id == _mine; };
  }
}

TEST(Selection, StartsEmpty)
{
  Selection s;
  EXPECT_TRUE(s.Empty());
  EXPECT_EQ(s.Count(), 0u);
  EXPECT_EQ(s.Primary(), SELECTION_NONE);
}

TEST(Selection, SetHoldsOneAndBecomesPrimary)
{
  Selection s;
  s.Set(7);
  EXPECT_FALSE(s.Empty());
  EXPECT_EQ(s.Count(), 1u);
  EXPECT_EQ(s.Primary(), 7u);
  EXPECT_TRUE(s.Contains(7));
}

TEST(Selection, SetNoneClears)
{
  Selection s;
  s.Set(7);
  s.Set(SELECTION_NONE);
  EXPECT_TRUE(s.Empty());
  EXPECT_EQ(s.Primary(), SELECTION_NONE);
}

TEST(Selection, SetReplacesTheWholeSet)
{
  Selection s;
  s.Add(1);
  s.Add(2);
  s.Set(9);   // a plain click replaces the band
  EXPECT_EQ(s.Count(), 1u);
  EXPECT_EQ(s.Primary(), 9u);
  EXPECT_FALSE(s.Contains(1));
}

TEST(Selection, AddDedupesAndKeepsPrimaryStable)
{
  Selection s;
  s.Add(3);
  s.Add(5);
  s.Add(3);   // duplicate ignored
  EXPECT_EQ(s.Count(), 2u);
  EXPECT_EQ(s.Primary(), 3u);   // first added stays primary
}

TEST(Selection, AddNoneIgnored)
{
  Selection s;
  s.Add(SELECTION_NONE);
  EXPECT_TRUE(s.Empty());
}

TEST(Selection, AddRespectsCapacity)
{
  Selection s;
  for (uint32_t i = 0; i < MAX_SELECTED + 5; ++i)
    s.Add(i);
  EXPECT_EQ(s.Count(), MAX_SELECTED);
  EXPECT_TRUE(s.Contains(0));
  EXPECT_FALSE(s.Contains(MAX_SELECTED));   // the overflow was dropped
}

TEST(Selection, ContainsOwnFiltersByPredicate)
{
  Selection s;
  s.Set(42);
  EXPECT_TRUE(s.ContainsOwn(OnlyOwn(42)));
  EXPECT_FALSE(s.ContainsOwn(OnlyOwn(1)));   // an enemy selection is not commandable

  s.Add(1);   // a mixed set still counts as owning a unit
  EXPECT_TRUE(s.ContainsOwn(OnlyOwn(42)));
}

TEST(Selection, RemoveDropsAndCompacts)
{
  Selection s;
  s.Add(1);
  s.Add(2);
  s.Add(3);
  s.Remove(2);
  EXPECT_EQ(s.Count(), 2u);
  EXPECT_FALSE(s.Contains(2));
  EXPECT_EQ(s.Primary(), 1u);   // primary unchanged when a non-primary is removed
}

TEST(Selection, RemovePrimaryPromotesNext)
{
  Selection s;
  s.Add(1);
  s.Add(2);
  s.Remove(1);   // the primary dies/despawns
  EXPECT_EQ(s.Count(), 1u);
  EXPECT_EQ(s.Primary(), 2u);
}
