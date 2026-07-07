#include "pch.h"
#include "ParticleSystem.h"

#include <algorithm>

using namespace DirectX;

namespace Neuron::Client
{
  namespace
  {
    // Per-type table, indexed by ParticleTypeId. Colours are 0xAABBGGRR (R in the low byte),
    // colour1 -> colour2 over life with colour2 alpha ~0 so the additive sprite fades out.
    // Placeholder tuning (donor table unavailable); flagged for phase-5 tuning in the header.
    constexpr ParticleType TABLE[static_cast<size_t>(ParticleTypeId::Count)] = {
      // life  size   frict  colour1      colour2       (0xAABBGGRR)
      { 0.60f, 150.0f, 2.5f, 0xFFFFFFFFu, 0x000080FFu }, // ExplosionCore   white -> orange(a0)
      { 1.20f,  60.0f, 1.0f, 0xFFA0D0FFu, 0x00000030u }, // ExplosionDebris warm  -> dark(a0)
      { 0.80f,  25.0f, 0.5f, 0xFFFFFFFFu, 0x0000C0FFu }, // Spark           white -> amber(a0)
      { 0.15f,  80.0f, 4.0f, 0xFFC0FFFFu, 0x0000A0FFu }, // MuzzleFlash     pale  -> orange(a0)
      { 1.00f,  90.0f, 1.5f, 0xFF00A0FFu, 0x00000040u }, // Fire            orange-> dark(a0)
      { 1.50f,  30.0f, 0.3f, 0xFF909090u, 0x00303030u }, // MissileTrail    smoke -> dark(a0)
      { 0.40f,  40.0f, 3.0f, 0xFFFFFFFFu, 0x000080FFu }, // MissileFire     white -> orange(a0)
    };

    // Lerp two packed 0xAABBGGRR colours by _t in [0,1], per byte lane.
    [[nodiscard]] uint32_t LerpRgba(uint32_t _a, uint32_t _b, float _t)
    {
      _t = std::clamp(_t, 0.0f, 1.0f);
      uint32_t out = 0;
      for (int shift = 0; shift < 32; shift += 8)
      {
        const float av = static_cast<float>((_a >> shift) & 0xFFu);
        const float bv = static_cast<float>((_b >> shift) & 0xFFu);
        const uint32_t v = static_cast<uint32_t>(av + (bv - av) * _t + 0.5f) & 0xFFu;
        out |= v << shift;
      }
      return out;
    }
  } // namespace

  const ParticleType& ParticleTypeInfo(ParticleTypeId _type)
  {
    int i = static_cast<int>(_type);
    if (i < 0 || i >= static_cast<int>(ParticleTypeId::Count))
      i = static_cast<int>(ParticleTypeId::ExplosionCore); // safe default for a bad id
    return TABLE[i];
  }

  void XM_CALLCONV ParticleSystem::CreateParticle(const Neuron::Math::Vector3i64& _worldPos, FXMVECTOR _vel,
                                                  ParticleTypeId _type, float _size)
  {
    if (_type <= ParticleTypeId::Invalid || _type >= ParticleTypeId::Count)
      return;

    Particle p{};
    p.anchor = _worldPos;
    p.offset = {0.0f, 0.0f, 0.0f};
    XMStoreFloat3(&p.vel, _vel);
    p.type = _type;
    p.size = (_size > 0.0f) ? _size : ParticleTypeInfo(_type).size;
    p.age = 0.0f;
    m_particles.push_back(p);
  }

  void ParticleSystem::Advance(float _dt)
  {
    if (_dt <= 0.0f)
      return;

    const XMVECTOR dt = XMVectorReplicate(_dt);
    for (Particle& p : m_particles)
    {
      const ParticleType& t = ParticleTypeInfo(p.type);
      XMVECTOR offset = XMLoadFloat3(&p.offset);
      XMVECTOR vel = XMLoadFloat3(&p.vel);
      offset = XMVectorMultiplyAdd(vel, dt, offset);           // offset += vel * dt
      float damp = 1.0f - t.friction * _dt;                    // light friction (no gravity)
      if (damp < 0.0f)
        damp = 0.0f;
      vel = XMVectorScale(vel, damp);
      XMStoreFloat3(&p.offset, offset);
      XMStoreFloat3(&p.vel, vel);
      p.age += _dt;
    }

    std::erase_if(m_particles, [](const Particle& _p) { return _p.age >= ParticleTypeInfo(_p.type).life; });
  }

  void ParticleSystem::BuildVertices(const Neuron::Math::Vector3i64& _camOrigin, Camera& _camera,
                                     std::vector<Neuron::Graphics::ParticleVertex>& _out) const
  {
    if (m_particles.empty())
      return;

    // Shared camera basis: the quads face the eye along the camera up / right (world space), the
    // same billboard construction the smoke test used, so u_MVP = view-projection transforms
    // them exactly like Scene3D::renderBillboard's world-space path.
    const XMFLOAT3 eyeF = _camera.Eye();
    const XMFLOAT3 atF = _camera.LookAt();
    const XMFLOAT3 upF = _camera.Up();
    const XMVECTOR eye = XMLoadFloat3(&eyeF);
    const XMVECTOR fwd = XMVector3Normalize(XMVectorSubtract(XMLoadFloat3(&atF), eye));
    const XMVECTOR right = XMVector3Normalize(XMVector3Cross(XMLoadFloat3(&upF), fwd));
    const XMVECTOR up = XMVector3Cross(fwd, right);

    _out.reserve(_out.size() + m_particles.size() * 6);

    const auto push = [&_out](FXMVECTOR _p, float _u, float _vv, uint32_t _rgba)
    {
      XMFLOAT3 f;
      XMStoreFloat3(&f, _p);
      _out.push_back({f.x, f.y, f.z, _u, _vv, _rgba});
    };

    for (const Particle& p : m_particles)
    {
      const ParticleType& t = ParticleTypeInfo(p.type);

      // Render-frame centre = (anchor - camOrigin) + offset. The int64 subtraction is exact and
      // the result is small (near the player), so it fits float without large-coordinate jitter.
      double dx, dy, dz;
      Neuron::Math::RelativeTo(p.anchor, _camOrigin, dx, dy, dz);
      const XMVECTOR centre = XMVectorSet(static_cast<float>(dx) + p.offset.x,
                                          static_cast<float>(dy) + p.offset.y,
                                          static_cast<float>(dz) + p.offset.z, 0.0f);

      const float half = p.size * 0.5f;
      const XMVECTOR rH = XMVectorScale(right, half);
      const XMVECTOR uH = XMVectorScale(up, half);
      const float life = (t.life > 0.0f) ? t.life : 1.0f;
      const uint32_t col = LerpRgba(t.colour1, t.colour2, p.age / life);

      const XMVECTOR bl = XMVectorSubtract(XMVectorSubtract(centre, rH), uH);
      const XMVECTOR br = XMVectorSubtract(XMVectorAdd(centre, rH), uH);
      const XMVECTOR tr = XMVectorAdd(XMVectorAdd(centre, rH), uH);
      const XMVECTOR tl = XMVectorAdd(XMVectorSubtract(centre, rH), uH);

      push(bl, 0.0f, 1.0f, col);
      push(br, 1.0f, 1.0f, col);
      push(tr, 1.0f, 0.0f, col);
      push(bl, 0.0f, 1.0f, col);
      push(tr, 1.0f, 0.0f, col);
      push(tl, 0.0f, 0.0f, col);
    }
  }

  float ParticleSystem::RandomFloat01()
  {
    // [0,1) on the engine-local minstd PRNG (deterministic within a build for seeded tests).
    constexpr uint32_t span = std::minstd_rand::max() - std::minstd_rand::min() + 1u;
    return static_cast<float>(m_rng() - std::minstd_rand::min()) / static_cast<float>(span);
  }

  float ParticleSystem::RandomSpread(float _x)
  {
    return (RandomFloat01() * 2.0f - 1.0f) * _x;
  }
}
