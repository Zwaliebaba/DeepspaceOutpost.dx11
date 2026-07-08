#include <gtest/gtest.h>

#include <cmath>
#include <random>
#include <vector>

#include <DirectXMath.h>

#include "ExplosionManager.h"
#include "Mesh.h"

// Device-free tests for the ported mesh-shatter simulation (explosion.md §9): triangle
// extraction from a synthetic MeshData, degenerate skipping, seeded fraction culling, tumbler
// orthonormality, lifetime, and the floating-origin rebase in AppendVertices. No D3D.

using namespace DirectX;
using namespace Neuron;
using Neuron::Client::Explosion;
using Neuron::Client::ExplosionManager;
using Neuron::Client::Tumbler;

namespace
{
  constexpr float kEps = 1e-3f;

  XMFLOAT3X3 IdentityBasis()
  {
    XMFLOAT3X3 b;
    XMStoreFloat3x3(&b, XMMatrixIdentity());
    return b;
  }

  // One right triangle, comfortably non-degenerate, ~50 units from the hull centre.
  Graphics::MeshData OneTriMesh()
  {
    Graphics::MeshData m;
    m.vertices = {
      {50.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0xFF0000FFu},
      {60.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0xFF0000FFu},
      {50.0f, 10.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0xFF0000FFu},
    };
    m.indices = {0, 1, 2};
    return m;
  }
}

TEST(ExplosionManager, OneTriangleInOneFragmentOut)
{
  ExplosionManager mgr;
  mgr.AddExplosion(OneTriMesh(), {0, 0, 0}, IdentityBasis());
  ASSERT_EQ(mgr.Count(), 1u);
  EXPECT_EQ(mgr.Explosions().front().TriCount(), 1u);
}

TEST(ExplosionManager, DegenerateTriangleIsSkipped)
{
  // Three coincident points: zero perimeter, under any hull-relative guard.
  Graphics::MeshData m;
  m.vertices = {
    {50.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0xFFFFFFFFu},
    {50.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0xFFFFFFFFu},
    {50.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0xFFFFFFFFu},
  };
  m.indices = {0, 1, 2};

  ExplosionManager mgr;
  mgr.AddExplosion(m, {0, 0, 0}, IdentityBasis());
  EXPECT_EQ(mgr.Count(), 0u); // all fragments skipped -> the explosion is dropped
}

TEST(ExplosionManager, FractionCullsASeededShare)
{
  // 100 copies of the same solid triangle; fraction 0.5 keeps roughly half (seeded, so the
  // exact count is deterministic per build - assert loose bounds, not the number).
  Graphics::MeshData m = OneTriMesh();
  const Graphics::MeshData one = m;
  for (int i = 1; i < 100; ++i)
    m.indices.insert(m.indices.end(), one.indices.begin(), one.indices.end());
  ASSERT_EQ(m.indices.size(), 300u);

  ExplosionManager mgr;
  mgr.SetSeed(1234u);
  mgr.AddExplosion(m, {0, 0, 0}, IdentityBasis(), 0.5f);
  ASSERT_EQ(mgr.Count(), 1u);
  const size_t kept = mgr.Explosions().front().TriCount();
  EXPECT_GT(kept, 25u);
  EXPECT_LT(kept, 75u);

  // Same seed -> same cull.
  ExplosionManager mgr2;
  mgr2.SetSeed(1234u);
  mgr2.AddExplosion(m, {0, 0, 0}, IdentityBasis(), 0.5f);
  ASSERT_EQ(mgr2.Count(), 1u);
  EXPECT_EQ(mgr2.Explosions().front().TriCount(), kept);
}

TEST(ExplosionManager, TumblerStaysOrthonormalAndDecays)
{
  Tumbler t{};
  XMStoreFloat3x3(&t.rot, XMMatrixIdentity());
  t.angVel = {1.5f, -2.0f, 0.7f};
  const float rate0 = std::sqrt(1.5f * 1.5f + 2.0f * 2.0f + 0.7f * 0.7f);

  for (int i = 0; i < 300; ++i) // ~10 s at 30 Hz
    t.Advance(1.0f / 30.0f);

  // R * R^T must still be identity within float drift.
  const XMMATRIX r = XMLoadFloat3x3(&t.rot);
  XMFLOAT3X3 rrt;
  XMStoreFloat3x3(&rrt, XMMatrixMultiply(r, XMMatrixTranspose(r)));
  for (int i = 0; i < 3; ++i)
    for (int j = 0; j < 3; ++j)
      EXPECT_NEAR(rrt(i, j), (i == j) ? 1.0f : 0.0f, 1e-3f);

  // The angular rate decays toward a slow drift.
  const float rate = std::sqrt(t.angVel.x * t.angVel.x + t.angVel.y * t.angVel.y +
                               t.angVel.z * t.angVel.z);
  EXPECT_LT(rate, rate0 * 0.05f);
}

TEST(ExplosionManager, ExplosionExpiresAfterLifetime)
{
  ExplosionManager mgr;
  mgr.AddExplosion(OneTriMesh(), {0, 0, 0}, IdentityBasis());
  ASSERT_EQ(mgr.Count(), 1u);

  mgr.Advance(Client::EXPLOSION_LIFETIME * 0.5f);
  EXPECT_EQ(mgr.Count(), 1u); // alive at half life

  mgr.Advance(Client::EXPLOSION_LIFETIME); // total age past the lifetime
  EXPECT_EQ(mgr.Count(), 0u);
}

TEST(ExplosionManager, AppendVerticesRebasesAgainstFloatingOrigin)
{
  // Before any Advance the tumbler is identity and the offset is the tri centre, so the
  // emitted triangle is the original world-oriented one translated by (anchor - camOrigin).
  const Neuron::Math::Vector3i64 anchor{5'000'000, 6'000'000, 7'000'000};
  const Neuron::Math::Vector3i64 camOrigin{5'000'000 - 200, 6'000'000, 7'000'000 + 300};

  ExplosionManager mgr;
  mgr.AddExplosion(OneTriMesh(), anchor, IdentityBasis());
  ASSERT_EQ(mgr.Count(), 1u);

  std::vector<Graphics::MeshVertex> verts;
  mgr.AppendVertices(camOrigin, verts);
  ASSERT_EQ(verts.size(), 3u);

  // anchor - camOrigin = (200, 0, -300); first mesh corner was (50, 0, 0).
  EXPECT_NEAR(verts[0].x, 200.0f + 50.0f, kEps);
  EXPECT_NEAR(verts[0].y, 0.0f, kEps);
  EXPECT_NEAR(verts[0].z, -300.0f, kEps);
  // Colour and normal carry through from the source mesh.
  EXPECT_EQ(verts[0].rgba, 0xFF0000FFu);
  EXPECT_NEAR(verts[0].nz, 1.0f, kEps);
}

TEST(ExplosionManager, FragmentsFlyOutward)
{
  ExplosionManager mgr;
  mgr.AddExplosion(OneTriMesh(), {0, 0, 0}, IdentityBasis());
  ASSERT_EQ(mgr.Count(), 1u);

  std::vector<Graphics::MeshVertex> before;
  mgr.AppendVertices({0, 0, 0}, before);
  ASSERT_EQ(before.size(), 3u);
  const float d0 = std::sqrt(before[0].x * before[0].x + before[0].y * before[0].y +
                             before[0].z * before[0].z);

  mgr.Advance(0.5f);

  std::vector<Graphics::MeshVertex> after;
  mgr.AppendVertices({0, 0, 0}, after);
  ASSERT_EQ(after.size(), 3u);
  const float d1 = std::sqrt(after[0].x * after[0].x + after[0].y * after[0].y +
                             after[0].z * after[0].z);

  EXPECT_GT(d1, d0); // the fragment moved away from the hull centre
}
