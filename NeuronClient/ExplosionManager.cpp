#include "pch.h"
#include "ExplosionManager.h"

#include <algorithm>
#include <cmath>

#include "GameMath.h"   // Neuron::Math::CreateRotationMatrix

using namespace DirectX;

namespace Neuron::Client
{
  namespace
  {
    // [0,1) on the shared explosion PRNG (mirrors ParticleSystem::RandomFloat01).
    [[nodiscard]] float Rand01(std::minstd_rand& _rng)
    {
      constexpr uint32_t span = std::minstd_rand::max() - std::minstd_rand::min() + 1u;
      return static_cast<float>(_rng() - std::minstd_rand::min()) / static_cast<float>(span);
    }

    // A random unit vector (uniform-ish; rejection-free is unnecessary for VFX).
    [[nodiscard]] XMVECTOR RandUnit(std::minstd_rand& _rng)
    {
      const XMVECTOR v = XMVectorSet(Rand01(_rng) * 2.0f - 1.0f,
                                     Rand01(_rng) * 2.0f - 1.0f,
                                     Rand01(_rng) * 2.0f - 1.0f, 0.0f);
      if (XMVectorGetX(XMVector3LengthSq(v)) < 1e-6f)
        return XMVectorSet(0.0f, 1.0f, 0.0f, 0.0f);
      return XMVector3Normalize(v);
    }
  } // namespace

  void Tumbler::Advance(float _dt)
  {
    XMVECTOR av = XMLoadFloat3(&angVel);
    const float rate = XMVectorGetX(XMVector3Length(av));
    if (rate > 1e-6f && _dt > 0.0f)
    {
      // Compose this frame's axis-angle nudge onto the fragment orientation. Both factors are
      // pure rotations, so the product stays orthonormal (up to float drift - fine for a
      // seconds-long effect; the unit tests bound it).
      const XMMATRIX spin = Neuron::Math::CreateRotationMatrix(av, rate * _dt);
      XMStoreFloat3x3(&rot, XMMatrixMultiply(XMLoadFloat3x3(&rot), spin));
    }
    const float damp = std::max(0.0f, 1.0f - DEBRIS_ROT_FRICTION * _dt);
    XMStoreFloat3(&angVel, XMVectorScale(av, damp));
  }

  Explosion::Explosion(const Neuron::Graphics::MeshData& _mesh, const Neuron::Math::Vector3i64& _anchor,
                       const XMFLOAT3X3& _basis, float _fraction, std::minstd_rand& _rng)
    : m_anchor(_anchor)
  {
    // Shared spin states: identity orientation, random axis, rates spread so neighbouring
    // fragments visibly tumble apart (donor allocated NUM_TUMBLERS per explosion).
    m_tumblers.resize(NUM_TUMBLERS);
    for (Tumbler& t : m_tumblers)
    {
      XMStoreFloat3x3(&t.rot, XMMatrixIdentity());
      const float rate = 0.5f + Rand01(_rng) * 3.0f; // rad/s
      XMStoreFloat3(&t.angVel, XMVectorScale(RandUnit(_rng), rate));
    }

    // Hull radius (model space) - scales the outward speeds and the degenerate-triangle guard
    // to the hull size, so one constant set fits a canister and a station alike.
    float radius = 0.0f;
    for (const Neuron::Graphics::MeshVertex& v : _mesh.vertices)
      radius = std::max(radius, std::sqrt(v.x * v.x + v.y * v.y + v.z * v.z));
    if (radius <= 0.0f)
      return; // empty/degenerate mesh -> no fragments
    const float minPerimeter = radius * 0.02f; // donor's "circum < 6" guard, rescaled

    // The hull's world basis, rows [side, roof, nose] - the row-vector convention, so
    // v_world = v_model * B via XMVector3TransformNormal.
    const XMMATRIX basis = XMLoadFloat3x3(&_basis);

    const size_t triCount = _mesh.indices.size() / 3;
    m_tris.reserve(triCount);
    for (size_t t = 0; t < triCount; ++t)
    {
      // fraction < 1 randomly drops that share of triangles (donor behaviour).
      if (_fraction < 1.0f && Rand01(_rng) >= _fraction)
        continue;

      const Neuron::Graphics::MeshVertex& a = _mesh.vertices[_mesh.indices[3 * t + 0]];
      const Neuron::Graphics::MeshVertex& b = _mesh.vertices[_mesh.indices[3 * t + 1]];
      const Neuron::Graphics::MeshVertex& c = _mesh.vertices[_mesh.indices[3 * t + 2]];

      // Orient the model-space corners to the hull's world basis once, here; the sim and the
      // per-frame vertex build only ever apply the tumbler on top.
      const XMVECTOR aw = XMVector3TransformNormal(XMVectorSet(a.x, a.y, a.z, 0.0f), basis);
      const XMVECTOR bw = XMVector3TransformNormal(XMVectorSet(b.x, b.y, b.z, 0.0f), basis);
      const XMVECTOR cw = XMVector3TransformNormal(XMVectorSet(c.x, c.y, c.z, 0.0f), basis);

      // Skip slivers/degenerates: donor guarded on the circumference.
      const float perimeter = XMVectorGetX(XMVector3Length(XMVectorSubtract(aw, bw))) +
                              XMVectorGetX(XMVector3Length(XMVectorSubtract(bw, cw))) +
                              XMVectorGetX(XMVector3Length(XMVectorSubtract(cw, aw)));
      if (perimeter < minPerimeter)
        continue;

      const XMVECTOR centre = XMVectorScale(XMVectorAdd(XMVectorAdd(aw, bw), cw), 1.0f / 3.0f);

      ExplodingTri tri{};
      XMStoreFloat3(&tri.v1, XMVectorSubtract(aw, centre));
      XMStoreFloat3(&tri.v2, XMVectorSubtract(bw, centre));
      XMStoreFloat3(&tri.v3, XMVectorSubtract(cw, centre));
      XMStoreFloat3(&tri.normal,
                    XMVector3TransformNormal(XMVectorSet(a.nx, a.ny, a.nz, 0.0f), basis));
      XMStoreFloat3(&tri.offset, centre);

      // Outward velocity from the hull centre, proportional to the fragment's offset (donor:
      // farther pieces fly faster), jittered so co-located fragments separate. Centre-most
      // fragments get a random direction so nothing sits still. No vertical kick (space).
      const float dist = XMVectorGetX(XMVector3Length(centre));
      XMVECTOR dir = (dist > radius * 0.01f) ? XMVectorScale(centre, 1.0f / dist) : RandUnit(_rng);
      const float speed = std::max(dist, radius * 0.15f) * DEBRIS_SPEED_FACTOR * (0.5f + Rand01(_rng));
      XMStoreFloat3(&tri.vel, XMVectorScale(dir, speed));

      tri.colour = a.rgba;
      tri.tumbler = static_cast<int>(_rng() % NUM_TUMBLERS);
      m_tris.push_back(tri);
    }
  }

  bool Explosion::Advance(float _dt)
  {
    if (_dt <= 0.0f)
      return m_age >= EXPLOSION_LIFETIME;

    for (Tumbler& t : m_tumblers)
      t.Advance(_dt);

    const XMVECTOR dt = XMVectorReplicate(_dt);
    const float damp = std::max(0.0f, 1.0f - DEBRIS_FRICTION * _dt);
    for (ExplodingTri& tri : m_tris)
    {
      XMVECTOR offset = XMLoadFloat3(&tri.offset);
      XMVECTOR vel = XMLoadFloat3(&tri.vel);
      offset = XMVectorMultiplyAdd(vel, dt, offset); // offset += vel * dt
      vel = XMVectorScale(vel, damp);                // light friction, no gravity
      XMStoreFloat3(&tri.offset, offset);
      XMStoreFloat3(&tri.vel, vel);
    }

    m_age += _dt;
    return m_age >= EXPLOSION_LIFETIME;
  }

  void Explosion::AppendVertices(const Neuron::Math::Vector3i64& _camOrigin,
                                 std::vector<Neuron::Graphics::MeshVertex>& _out) const
  {
    if (m_tris.empty())
      return;

    // Geometric fade: full size until EXPLOSION_SHRINK_START of the life, then shrink linearly
    // to nothing (the debris pass is opaque - the scene3d PS ignores vertex alpha - so the fade
    // must be in the geometry, explosion.md §5a).
    const float lifeFrac = m_age / EXPLOSION_LIFETIME;
    float shrink = 1.0f;
    if (lifeFrac > EXPLOSION_SHRINK_START)
      shrink = std::max(0.0f, 1.0f - (lifeFrac - EXPLOSION_SHRINK_START) / (1.0f - EXPLOSION_SHRINK_START));
    if (shrink <= 0.0f)
      return;

    // Render-frame base = (anchor - camOrigin): exact int64 subtraction, small near the player.
    double dx, dy, dz;
    Neuron::Math::RelativeTo(m_anchor, _camOrigin, dx, dy, dz);
    const XMVECTOR base = XMVectorSet(static_cast<float>(dx), static_cast<float>(dy),
                                      static_cast<float>(dz), 0.0f);

    _out.reserve(_out.size() + m_tris.size() * 3);
    for (const ExplodingTri& tri : m_tris)
    {
      const XMMATRIX rot = XMLoadFloat3x3(&m_tumblers[tri.tumbler].rot);
      const XMVECTOR pos = XMVectorAdd(XMLoadFloat3(&tri.offset), base);
      const XMVECTOR n = XMVector3TransformNormal(XMLoadFloat3(&tri.normal), rot);

      XMFLOAT3 nf;
      XMStoreFloat3(&nf, n);

      const auto push = [&](const XMFLOAT3& _v)
      {
        const XMVECTOR w = XMVectorAdd(
            XMVector3TransformNormal(XMVectorScale(XMLoadFloat3(&_v), shrink), rot), pos);
        XMFLOAT3 wf;
        XMStoreFloat3(&wf, w);
        _out.push_back({wf.x, wf.y, wf.z, nf.x, nf.y, nf.z, tri.colour});
      };
      push(tri.v1);
      push(tri.v2);
      push(tri.v3);
    }
  }

  void ExplosionManager::AddExplosion(const Neuron::Graphics::MeshData& _mesh,
                                      const Neuron::Math::Vector3i64& _anchor,
                                      const XMFLOAT3X3& _basis, float _fraction)
  {
    Explosion e(_mesh, _anchor, _basis, _fraction, m_rng);
    if (e.TriCount() > 0)
      m_explosions.push_back(std::move(e));
  }

  void ExplosionManager::Advance(float _dt)
  {
    // Advance first, cull second: erase_if's predicate must not mutate the elements.
    for (Explosion& e : m_explosions)
      e.Advance(_dt);
    std::erase_if(m_explosions, [](const Explosion& _e) { return _e.Age() >= EXPLOSION_LIFETIME; });
  }

  void ExplosionManager::AppendVertices(const Neuron::Math::Vector3i64& _camOrigin,
                                        std::vector<Neuron::Graphics::MeshVertex>& _out) const
  {
    for (const Explosion& e : m_explosions)
      e.AppendVertices(_camOrigin, _out);
  }
}
