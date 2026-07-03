#include <gtest/gtest.h>

#include <cstdint>
#include <string>
#include <tuple>

#include "OnChangeCache.h"

using namespace Neuron::Server;

TEST(OnChangeCache, FirstValueForAKeyIsAChange)
{
  OnChangeCache<uint64_t, int> cache;
  EXPECT_TRUE(cache.Changed(1, 42));
  EXPECT_EQ(cache.Size(), 1u);
}

TEST(OnChangeCache, IdenticalResendsAreSuppressed)
{
  OnChangeCache<uint64_t, int> cache;
  EXPECT_TRUE(cache.Changed(1, 42));
  EXPECT_FALSE(cache.Changed(1, 42));   // same value: don't send
  EXPECT_FALSE(cache.Changed(1, 42));
  EXPECT_TRUE(cache.Changed(1, 43));    // it moved: send
  EXPECT_FALSE(cache.Changed(1, 43));
}

TEST(OnChangeCache, KeysAreIndependent)
{
  OnChangeCache<uint64_t, int> cache;
  EXPECT_TRUE(cache.Changed(1, 7));
  EXPECT_TRUE(cache.Changed(2, 7));     // same value, different client: still new
  EXPECT_FALSE(cache.Changed(1, 7));
}

TEST(OnChangeCache, PruneDropsDepartedKeys)
{
  OnChangeCache<uint64_t, int> cache;
  std::ignore = cache.Changed(1, 1);
  std::ignore = cache.Changed(2, 2);
  std::ignore = cache.Changed(3, 3);

  cache.Prune([](uint64_t _key) { return _key == 2; });   // only client 2 remains

  EXPECT_EQ(cache.Size(), 1u);
  EXPECT_FALSE(cache.Changed(2, 2));   // survivor kept its memory
  EXPECT_TRUE(cache.Changed(1, 1));    // departed key re-registers as new
}

namespace
{
  // A catalog-message-shaped value: no operator==, compared via Fields() like
  // the real PlayerStatus.
  struct FieldsValue
  {
    int a = 0;
    std::string b;
    auto Fields() const { return std::tie(a, b); }
  };
  struct FieldsEq
  {
    bool operator()(const FieldsValue& _x, const FieldsValue& _y) const
    {
      return _x.Fields() == _y.Fields();
    }
  };
}

TEST(OnChangeCache, PluggableEqualityComparesFieldsTuples)
{
  OnChangeCache<uint64_t, FieldsValue, FieldsEq> cache;
  EXPECT_TRUE(cache.Changed(1, FieldsValue{ 1, "x" }));
  EXPECT_FALSE(cache.Changed(1, FieldsValue{ 1, "x" }));   // tuple-equal: suppressed
  EXPECT_TRUE(cache.Changed(1, FieldsValue{ 1, "y" }));    // one field moved: sent
}
