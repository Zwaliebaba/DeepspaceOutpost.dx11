#include <gtest/gtest.h>

#include <cmath>

// Phase 0 golden / characterization tests for the legacy DeepspaceOutpost math
// (DeepspaceOutpost/vector.cpp), captured BEFORE the DirectXMath migration so
// every later phase can be diffed against the exact double-precision behaviour.
// See docs/MathMigration.md (Phase 0).
//
// Home: these live under NeuronCore.Tests because the migration's replacement
// helper (Neuron::Math::Orthonormalize, GameMath.h) lives in NeuronCore, and
// there is no DeepspaceOutpost test project (the client is a Win32 exe). The
// legacy routines themselves live in DeepspaceOutpost/vector.cpp, which #includes
// the heavy client pch and can't link into a test exe, so the tiny pure
// functions are embedded VERBATIM as a reference oracle. It is the spec the
// migration must preserve; keep it bit-identical to vector.cpp until that file is
// deleted in Phase 7, at which point these tests guard the DirectXMath port.
//
// Most expected values are computed exactly by hand (simple integer inputs). The
// drift-sensitive tidy_matrix case is marked TODO(windows): capture the literal
// output from the first Windows/MSVC build of the legacy code and replace the
// placeholder. The tolerance here is tight (double oracle vs double oracle); the
// later float32-port comparison will use a looser, magnitude-scaled epsilon
// (docs/MathMigration.md §7).

namespace
{
  constexpr double kEps = 1e-9;

  // --- Verbatim oracle: mirrors DeepspaceOutpost/vector.{h,cpp} ---------------
  struct Vec { double x, y, z; };
  using Mat = Vec[3];

  // vector.cpp: vector_dot_product
  double DotProduct(const Vec* first, const Vec* second)
  {
    return (first->x * second->x) + (first->y * second->y) + (first->z * second->z);
  }

  // vector.cpp: unit_vector
  Vec UnitVector(const Vec* vec)
  {
    const double lx = vec->x, ly = vec->y, lz = vec->z;
    const double uni = std::sqrt(lx * lx + ly * ly + lz * lz);
    return { lx / uni, ly / uni, lz / uni };
  }

  // vector.cpp: mult_vector (row-vector by 3x3; result.i = dot(vec, mat[i]) -> M*v)
  void MultVector(Vec* vec, const Vec* mat)
  {
    const double x = (vec->x * mat[0].x) + (vec->y * mat[0].y) + (vec->z * mat[0].z);
    const double y = (vec->x * mat[1].x) + (vec->y * mat[1].y) + (vec->z * mat[1].z);
    const double z = (vec->x * mat[2].x) + (vec->y * mat[2].y) + (vec->z * mat[2].z);
    vec->x = x; vec->y = y; vec->z = z;
  }

  // vector.cpp: mult_matrix (result written back into `first`)
  void MultMatrix(Vec* first, const Vec* second)
  {
    Vec rv[3];
    for (int i = 0; i < 3; i++)
    {
      rv[i].x = (first[0].x * second[i].x) + (first[1].x * second[i].y) + (first[2].x * second[i].z);
      rv[i].y = (first[0].y * second[i].x) + (first[1].y * second[i].y) + (first[2].y * second[i].z);
      rv[i].z = (first[0].z * second[i].x) + (first[1].z * second[i].y) + (first[2].z * second[i].z);
    }
    for (int i = 0; i < 3; i++)
      first[i] = rv[i];
  }

  // vector.cpp: tidy_matrix (re-orthonormalize: nose=mat[2], roof=mat[1], side=mat[0])
  void TidyMatrix(Vec* mat)
  {
    mat[2] = UnitVector(&mat[2]);

    if ((mat[2].x > -1) && (mat[2].x < 1))
    {
      if ((mat[2].y > -1) && (mat[2].y < 1))
        mat[1].z = -(mat[2].x * mat[1].x + mat[2].y * mat[1].y) / mat[2].z;
      else
        mat[1].y = -(mat[2].x * mat[1].x + mat[2].z * mat[1].z) / mat[2].y;
    }
    else
    {
      mat[1].x = -(mat[2].y * mat[1].y + mat[2].z * mat[1].z) / mat[2].x;
    }

    mat[1] = UnitVector(&mat[1]);

    mat[0].x = mat[1].y * mat[2].z - mat[1].z * mat[2].y;
    mat[0].y = mat[1].z * mat[2].x - mat[1].x * mat[2].z;
    mat[0].z = mat[1].x * mat[2].y - mat[1].y * mat[2].x;
  }

  // space.cpp: rotate_vec (sequential shear integrator, NOT a true rotation)
  void RotateVec(Vec* vec, double alpha, double beta)
  {
    double x = vec->x, y = vec->y, z = vec->z;
    y = y - alpha * x;
    x = x + alpha * y;
    y = y - beta * z;
    z = z + beta * y;
    vec->x = x; vec->y = y; vec->z = z;
  }

  // Per-component near check with GoogleTest reporting.
  void ExpectVecNear(const Vec& v, double x, double y, double z, double eps = kEps)
  {
    EXPECT_NEAR(v.x, x, eps);
    EXPECT_NEAR(v.y, y, eps);
    EXPECT_NEAR(v.z, z, eps);
  }
}

// --- Golden tests -------------------------------------------------------------

TEST(LegacyMath, DotProduct)
{
  const Vec a{ 1.0, 2.0, 3.0 };
  const Vec b{ 4.0, 5.0, 6.0 };
  EXPECT_NEAR(DotProduct(&a, &b), 32.0, kEps);   // 4 + 10 + 18
}

TEST(LegacyMath, UnitVector)
{
  Vec v{ 3.0, 4.0, 0.0 };
  const Vec u = UnitVector(&v);
  ExpectVecNear(u, 0.6, 0.8, 0.0);               // |(3,4,0)| = 5
}

// THE key convention test: legacy mult_vector computes result = M * v (column
// convention, result.i = dot(v, row_i)). The DirectXMath port uses
// XMVector3Transform = v * M. On this deliberately NON-symmetric (cyclic
// permutation) matrix the two disagree, which pins the required transpose:
//   legacy M*v   -> (2, 3, 1)
//   naive v*M    -> (3, 1, 2)   <-- WRONG port (missing XMMatrixTranspose)
// A symmetric matrix would hide this. See docs/MathMigration.md §4.
TEST(LegacyMath, MultVectorIsColumnConvention)
{
  Mat m = {
    { 0.0, 1.0, 0.0 },
    { 0.0, 0.0, 1.0 },
    { 1.0, 0.0, 0.0 },
  };
  Vec v{ 1.0, 2.0, 3.0 };
  MultVector(&v, m);
  ExpectVecNear(v, 2.0, 3.0, 1.0);
}

// mult_matrix(identity, M) leaves identity holding M. Pins the basic semantics.
TEST(LegacyMath, MultMatrixIdentityYieldsSecond)
{
  Mat first = {
    { 1.0, 0.0, 0.0 },
    { 0.0, 1.0, 0.0 },
    { 0.0, 0.0, 1.0 },
  };
  Mat second = {
    { 0.0, 1.0, 0.0 },
    { 0.0, 0.0, 1.0 },
    { 1.0, 0.0, 0.0 },
  };
  MultMatrix(first, second);
  ExpectVecNear(first[0], second[0].x, second[0].y, second[0].z);
  ExpectVecNear(first[1], second[1].x, second[1].y, second[1].z);
  ExpectVecNear(first[2], second[2].x, second[2].y, second[2].z);
}

// Pins the factor order of mult_matrix on a non-commuting case (review flagged
// "verify factor order against mult_matrix, which writes into first").
TEST(LegacyMath, MultMatrixFactorOrder)
{
  Mat p = {
    { 0.0, 1.0, 0.0 },
    { 0.0, 0.0, 1.0 },
    { 1.0, 0.0, 0.0 },
  };
  Mat second = {
    { 0.0, 1.0, 0.0 },
    { 0.0, 0.0, 1.0 },
    { 1.0, 0.0, 0.0 },
  };
  MultMatrix(p, second);                   // p := p (.) second
  // Hand-computed: rv[i] = (P[i].z, P[i].x, P[i].y).
  ExpectVecNear(p[0], 0.0, 0.0, 1.0);
  ExpectVecNear(p[1], 1.0, 0.0, 0.0);
  ExpectVecNear(p[2], 0.0, 1.0, 0.0);
}

// rotate_vec is a sequential shear, not a true rotation: it uses the freshly
// updated y when computing x and z. Hand-traced for (1,0,0), a=0.1, b=0.2.
// Substituting XMMatrixRotation* would NOT reproduce these numbers (see §1(A)/§7).
TEST(LegacyMath, RotateVecIsSequentialShear)
{
  Vec v{ 1.0, 0.0, 0.0 };
  RotateVec(&v, 0.1, 0.2);
  ExpectVecNear(v, 0.99, -0.1, -0.02);
}

// tidy_matrix on the engine's start_matrix flips the X axis to -1 (left-handed
// -Z basis). Characterizes the handedness the port must preserve (§5).
TEST(LegacyMath, TidyMatrixStartBasis)
{
  Mat m = {
    { 1.0, 0.0, 0.0 },
    { 0.0, 1.0, 0.0 },
    { 0.0, 0.0, -1.0 },
  };
  TidyMatrix(m);
  ExpectVecNear(m[0], -1.0, 0.0, 0.0);
  ExpectVecNear(m[1], 0.0, 1.0, 0.0);
  ExpectVecNear(m[2], 0.0, 0.0, -1.0);
}

// Drift case: a perturbed (non-orthonormal) basis re-orthonormalized. The output
// is messy float; capture it from the legacy build and replace the placeholder.
TEST(LegacyMath, TidyMatrixPerturbedBasis)
{
  Mat m = {
    { 0.98, 0.05, 0.10 },
    { 0.03, 1.01, -0.04 },
    { 0.12, 0.02, -0.97 },
  };
  TidyMatrix(m);
  // TODO(windows): replace with literals captured from the legacy MSVC build.
  // ExpectVecNear(m[0], /*x*/, /*y*/, /*z*/);
  // ExpectVecNear(m[1], /*x*/, /*y*/, /*z*/);
  // ExpectVecNear(m[2], /*x*/, /*y*/, /*z*/);
  EXPECT_NEAR(DotProduct(&m[0], &m[1]), 0.0, kEps);   // invariant: orthogonal
  EXPECT_NEAR(DotProduct(&m[1], &m[2]), 0.0, kEps);
  EXPECT_NEAR(DotProduct(&m[0], &m[2]), 0.0, kEps);
}
