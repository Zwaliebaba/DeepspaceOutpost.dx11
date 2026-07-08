#include <gtest/gtest.h>

#include <vector>

#include <DirectXMath.h>

#include "ParticleSystem.h"
#include "Camera.h"
#include "ParticleVertex.h"

// Device-free tests for the ported particle simulation (explosion.md §9): spawn, integrate,
// friction, cull, the seedable engine PRNG, and the floating-origin rebase in BuildVertices.
// None of these touch D3D - the pool and the vertex build are pure CPU math.

using namespace DirectX;
using namespace Neuron;
using Neuron::Client::ParticleSystem;
using Neuron::Client::ParticleTypeId;
using Neuron::Client::ParticleTypeInfo;

namespace
{
  constexpr float kEps = 1e-3f;
}

TEST(ParticleSystem, CreateAddsToPool)
{
  ParticleSystem ps;
  EXPECT_EQ(ps.Count(), 0u);
  ps.CreateParticle({0, 0, 0}, XMVectorZero(), ParticleTypeId::Spark);
  ps.CreateParticle({0, 0, 0}, XMVectorZero(), ParticleTypeId::Spark);
  EXPECT_EQ(ps.Count(), 2u);
}

TEST(ParticleSystem, InvalidTypeIsRejected)
{
  ParticleSystem ps;
  ps.CreateParticle({0, 0, 0}, XMVectorZero(), ParticleTypeId::Invalid);
  ps.CreateParticle({0, 0, 0}, XMVectorZero(), ParticleTypeId::Count);
  EXPECT_EQ(ps.Count(), 0u);
}

TEST(ParticleSystem, AdvanceIntegratesOffsetThenDampsVelocity)
{
  ParticleSystem ps;
  ps.CreateParticle({0, 0, 0}, XMVectorSet(100.0f, 0.0f, 0.0f, 0.0f), ParticleTypeId::ExplosionCore);

  const float dt = 0.1f;
  ps.Advance(dt);

  ASSERT_EQ(ps.Count(), 1u);
  const Neuron::Client::Particle& p = ps.Particles().front();
  // Offset integrates with the pre-damp velocity: 100 * 0.1 = 10 along x.
  EXPECT_NEAR(p.offset.x, 10.0f, kEps);
  EXPECT_NEAR(p.offset.y, 0.0f, kEps);
  EXPECT_NEAR(p.offset.z, 0.0f, kEps);
  // Velocity is then damped by friction: 100 * (1 - friction*dt).
  const float friction = ParticleTypeInfo(ParticleTypeId::ExplosionCore).friction;
  EXPECT_NEAR(p.vel.x, 100.0f * (1.0f - friction * dt), kEps);
}

TEST(ParticleSystem, AdvanceCullsAtEndOfLife)
{
  ParticleSystem ps;
  ps.CreateParticle({0, 0, 0}, XMVectorZero(), ParticleTypeId::ExplosionCore);
  const float life = ParticleTypeInfo(ParticleTypeId::ExplosionCore).life;

  ps.Advance(life * 0.5f);
  EXPECT_EQ(ps.Count(), 1u); // still alive halfway through

  ps.Advance(life); // total age now exceeds life
  EXPECT_EQ(ps.Count(), 0u);
}

TEST(ParticleSystem, SeededPrngIsDeterministic)
{
  ParticleSystem a;
  ParticleSystem b;
  a.SetSeed(42u);
  b.SetSeed(42u);
  for (int i = 0; i < 16; ++i)
  {
    const float fa = a.RandomFloat01();
    const float fb = b.RandomFloat01();
    EXPECT_FLOAT_EQ(fa, fb);
    EXPECT_GE(fa, 0.0f);
    EXPECT_LT(fa, 1.0f);
  }
}

TEST(ParticleSystem, RandomSpreadStaysInRange)
{
  ParticleSystem ps;
  ps.SetSeed(7u);
  for (int i = 0; i < 64; ++i)
  {
    const float s = ps.RandomSpread(5.0f);
    EXPECT_GE(s, -5.0f);
    EXPECT_LE(s, 5.0f);
  }
}

TEST(ParticleSystem, BuildVerticesRebasesAgainstFloatingOrigin)
{
  ParticleSystem ps;
  // A blast far from the world origin; the render frame is measured against a nearby origin.
  const Neuron::Math::Vector3i64 anchor{1'000'000, 2'000'000, 3'000'000};
  const Neuron::Math::Vector3i64 camOrigin{1'000'000 - 100, 2'000'000 - 100, 3'000'000 - 100};
  ps.CreateParticle(anchor, XMVectorZero(), ParticleTypeId::ExplosionCore);

  Client::Camera cam; // default: eye at origin, looking +z, up +y -> right = +x, up = +y

  std::vector<Neuron::Graphics::ParticleVertex> verts;
  ps.BuildVertices(camOrigin, cam, verts);

  ASSERT_EQ(verts.size(), 6u); // one quad = two triangles
  // The quad centre is the midpoint of the bottom-left (verts[0]) and top-right (verts[2])
  // corners; it must equal (anchor - camOrigin) + offset = (100, 100, 100).
  const auto& bl = verts[0];
  const auto& tr = verts[2];
  EXPECT_NEAR((bl.x + tr.x) * 0.5f, 100.0f, kEps);
  EXPECT_NEAR((bl.y + tr.y) * 0.5f, 100.0f, kEps);
  EXPECT_NEAR((bl.z + tr.z) * 0.5f, 100.0f, kEps);
}
