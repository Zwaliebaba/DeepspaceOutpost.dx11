#include "TestFramework.h"

#include "SpatialGrid.h"

using namespace Neuron::Spatial;
using Neuron::Math::Vector3i64;

TEST(Grid_CellOfHandlesNegativesAtBoundaries)
{
  Grid g(100);
  CHECK(g.CellSize() == 100);

  CHECK((g.CellOf(Vector3i64{ 0, 0, 0 }) == Cell{ 0, 0, 0 }));
  CHECK((g.CellOf(Vector3i64{ 99, 0, 0 }) == Cell{ 0, 0, 0 }));
  CHECK((g.CellOf(Vector3i64{ 100, 0, 0 }) == Cell{ 1, 0, 0 }));
  CHECK((g.CellOf(Vector3i64{ -1, 0, 0 }) == Cell{ -1, 0, 0 }));
  CHECK((g.CellOf(Vector3i64{ -100, 0, 0 }) == Cell{ -1, 0, 0 }));
  CHECK((g.CellOf(Vector3i64{ -101, 0, 0 }) == Cell{ -2, 0, 0 }));
}

TEST(Grid_QueryNearFindsByRadius)
{
  Grid g(100);
  const Vector3i64 a{ 50, 50, 50 };     // cell (0,0,0)
  const Vector3i64 b{ 150, 50, 50 };    // cell (1,0,0) - adjacent
  const Vector3i64 c{ 350, 50, 50 };    // cell (3,0,0) - far
  g.Insert(1, a);
  g.Insert(2, b);
  g.Insert(3, c);

  auto contains = [](const std::vector<uint64_t>& v, uint64_t id)
  {
    for (uint64_t x : v) if (x == id) return true;
    return false;
  };

  std::vector<uint64_t> r0;
  g.QueryNear(a, 0, r0);
  CHECK(r0.size() == 1);
  CHECK(contains(r0, 1));

  std::vector<uint64_t> r1;
  g.QueryNear(a, 1, r1);          // picks up the adjacent cell
  CHECK(r1.size() == 2);
  CHECK(contains(r1, 1));
  CHECK(contains(r1, 2));
  CHECK(!contains(r1, 3));        // 3 cells away, excluded

  std::vector<uint64_t> r3;
  g.QueryNear(a, 3, r3);
  CHECK(contains(r3, 3));
}

TEST(Grid_MoveRebucketsEntity)
{
  Grid g(100);
  const Vector3i64 from{ 50, 50, 50 };    // cell (0,0,0)
  const Vector3i64 to{ 250, 50, 50 };     // cell (2,0,0)
  g.Insert(1, from);

  g.Move(1, from, to);

  std::vector<uint64_t> atFrom;
  g.QueryNear(from, 0, atFrom);
  CHECK(atFrom.empty());                  // no longer in the old cell

  std::vector<uint64_t> atTo;
  g.QueryNear(to, 0, atTo);
  CHECK(atTo.size() == 1 && atTo[0] == 1);

  // Moving within the same cell is a no-op (still found).
  g.Move(1, to, Vector3i64{ 299, 99, 99 });
  std::vector<uint64_t> still;
  g.QueryNear(to, 0, still);
  CHECK(still.size() == 1 && still[0] == 1);
}

TEST(Grid_RemoveEmptiesCell)
{
  Grid g(100);
  const Vector3i64 p{ 50, 50, 50 };
  g.Insert(1, p);
  g.Insert(2, p);
  CHECK(g.OccupiedCellCount() == 1);

  g.Remove(1, p);
  std::vector<uint64_t> after;
  g.QueryNear(p, 0, after);
  CHECK(after.size() == 1 && after[0] == 2);
  CHECK(g.OccupiedCellCount() == 1);

  g.Remove(2, p);
  CHECK(g.OccupiedCellCount() == 0);      // cell pruned when empty
}
