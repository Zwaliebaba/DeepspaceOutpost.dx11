#pragma once

#include <cstdint>
#include <random>
#include <vector>

#include <DirectXMath.h>

#include "Vector3i64.h"       // Neuron::Math::Vector3i64 - absolute world anchor
#include "Camera.h"           // Neuron::Client::Camera - billboard basis + view/projection
#include "ParticleVertex.h"   // Neuron::Graphics::ParticleVertex - the GPU vertex (no D3D)

// Ported Darwinia billboard particle engine, space-adapted (explosion.md §6a). A pool of
// additive camera-facing sprites (the fireball core, sparks, trails) driven by a small static
// per-type table. Pure CPU simulation - no D3D, device-free, headlessly unit-testable. The
// space adaptation drops the donor's gravity, landscape bounce and child-spawn; what remains is
// outward drift, light friction and a colour/alpha fade over life.

namespace Neuron::Client
{
  // The space-relevant subset (explosion.md decision 3). The Darwinia-only types (Leaf, Brass,
  // BlueSpark, ...) are dropped; the donor's TypeInvalid / TypeNumTypes sentinels become the
  // Invalid / Count enumerators of this scoped enum (coding-standards.md).
  enum class ParticleTypeId : int8_t
  {
    Invalid = -1,
    ExplosionCore,
    ExplosionDebris,
    Spark,
    MuzzleFlash,
    Fire,
    MissileTrail,
    MissileFire,
    Count
  };

  // Static per-type tuning (donor life / size / friction / colour, minus gravity - space is
  // zero-g). colour1 -> colour2 is lerped over the particle's life (0xAABBGGRR, R in the low
  // byte); colour2's low alpha fades the additive contribution to nothing as it dies. NOTE: the
  // donor's numeric table was not available at port time, so these are plausible space values
  // flagged for phase-5 tuning.
  struct ParticleType
  {
    float life;        // seconds before the particle is culled
    float size;        // billboard size in world units at spawn
    float friction;    // velocity damping per second (vel *= 1 - friction*dt)
    uint32_t colour1;  // start colour, 0xAABBGGRR
    uint32_t colour2;  // end colour (typically alpha 0)
  };

  // One live particle. Storage-only vectors (XMFLOAT3); all arithmetic happens in XMVECTOR (the
  // AGENTS.md SIMD boundary). The absolute spawn point is an int64 anchor and motion integrates
  // in the small float offset, so the render position (anchor - camOrigin) + offset stays
  // precise near the player however far the blast is from the world origin.
  struct Particle
  {
    Neuron::Math::Vector3i64 anchor;
    DirectX::XMFLOAT3 offset;
    DirectX::XMFLOAT3 vel;
    ParticleTypeId type;
    float size;
    float age;
  };

  class ParticleSystem
  {
    public:
      // Spawn one particle of _type at absolute world point _worldPos with initial velocity
      // _vel (world units / second). _size overrides the type's base size (<= 0 -> use the type).
      void XM_CALLCONV CreateParticle(const Neuron::Math::Vector3i64& _worldPos, DirectX::FXMVECTOR _vel,
                                      ParticleTypeId _type, float _size = 0.0f);

      // Integrate every particle by _dt seconds: drift by velocity, damp by friction, age, and
      // cull the dead. No gravity, no landscape bounce (space).
      void Advance(float _dt);

      // Append this frame's camera-facing billboard quads (6 verts each) to _out. _camOrigin is
      // the floating origin the render frame is measured against; _camera supplies the world
      // up / right the quads face along. Colour lerps colour1 -> colour2 over life.
      void BuildVertices(const Neuron::Math::Vector3i64& _camOrigin, Camera& _camera,
                         std::vector<Neuron::Graphics::ParticleVertex>& _out) const;

      // Reseed the VFX PRNG (deterministic tests). This is presentation randomness only - not a
      // sim-determinism contract - and engine-local, so NeuronClient never depends on the exe's
      // random.h (dependency direction).
      void SetSeed(uint32_t _seed) { m_rng.seed(_seed); }

      [[nodiscard]] size_t Count() const { return m_particles.size(); }
      [[nodiscard]] const std::vector<Particle>& Particles() const { return m_particles; }

      // [0,1) and [-_x,_x) on the engine-local PRNG (the donor's frand / sfrand).
      [[nodiscard]] float RandomFloat01();
      [[nodiscard]] float RandomSpread(float _x);

    private:
      std::vector<Particle> m_particles;
      std::minstd_rand m_rng{ 12345u }; // fixed default seed; SetSeed reseeds for tests
  };

  // The static per-type table entry for _type (defined in ParticleSystem.cpp).
  [[nodiscard]] const ParticleType& ParticleTypeInfo(ParticleTypeId _type);
}
