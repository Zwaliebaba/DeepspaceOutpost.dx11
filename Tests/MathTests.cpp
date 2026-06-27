#include "TestFramework.h"

#include "Vector3i64.h"

using namespace Neuron::Math;

TEST(Vector3i64_AddSubEquality)
{
  Vector3i64 a{ 10, -20, 30 };
  Vector3i64 b{ 1, 2, 3 };

  CHECK((a + b) == (Vector3i64{ 11, -18, 33 }));
  CHECK((a - b) == (Vector3i64{ 9, -22, 27 }));
  CHECK(a != b);

  Vector3i64 c = a;
  c += b;
  CHECK(c == (Vector3i64{ 11, -18, 33 }));
  c -= b;
  CHECK(c == a);
}

TEST(Vector3i64_DefaultIsZero)
{
  Vector3i64 v;
  CHECK(v == (Vector3i64{ 0, 0, 0 }));
}

TEST(Vector3i64_HoldsHugeCoordinatesWithoutLoss)
{
  // Beyond the ~2^53 exact-integer range of double - must stay exact as int64.
  const int64_t big = 9'000'000'000'000'000'000LL;   // ~9.0e18, near int64 max
  Vector3i64 v{ big, big + 1, -big };
  CHECK(v.x == big);
  CHECK(v.y == big + 1);
  CHECK(v.x != v.y);                 // distinct values a double would collapse
  CHECK((v - Vector3i64{ big, big, -big }) == (Vector3i64{ 0, 1, 0 }));
}

TEST(Vector3i64_RelativeToFloatingOrigin)
{
  // Absolute coords are huge; the offset from a nearby origin is small and
  // exact, which is what the client renders in float.
  const int64_t base = 5'000'000'000'000LL;
  Vector3i64 origin{ base, base, base };
  Vector3i64 world{ base + 100, base - 250, base + 12345 };

  double dx, dy, dz;
  RelativeTo(world, origin, dx, dy, dz);
  CHECK(dx == 100.0);
  CHECK(dy == -250.0);
  CHECK(dz == 12345.0);

  // Same point as its own origin -> zero offset.
  RelativeTo(world, world, dx, dy, dz);
  CHECK(dx == 0.0 && dy == 0.0 && dz == 0.0);
}
