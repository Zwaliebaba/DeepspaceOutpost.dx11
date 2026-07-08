#pragma once

#include <cstdint>
#include <vector>

#include <DirectXMath.h>

#include "Vector3i64.h"        // Neuron::Math::Vector3i64
#include "ParticleSystem.h"    // Neuron::Client::ParticleSystem + ParticleTypeId
#include "ExplosionManager.h"  // Neuron::Client::ExplosionManager - the mesh-shatter sim
#include "ParticleVertex.h"    // Neuron::Graphics::ParticleVertex (no D3D)

// The engine-owned client visual-effects subsystem (explosion.md §6, decision 2). It owns the
// ported particle simulation and the mesh-shatter ExplosionManager plus the per-frame origin,
// and feeds the SceneParticles GPU pass. Reached through a Meyers-singleton accessor
// EffectsInstance(), exactly like ReplicationClientInstance() / MainCamera().
//
// Lifecycle is engine-owned: ClientEngine::Frame advances it each frame. The game only spawns
// into it (CreateParticle / AddExplosion) and supplies the floating origin (SetOrigin) - it
// links no simulation code of its own. Everything here lives in NeuronClient and never touches
// the game's ship tables, so the subsystem stays game-agnostic.

namespace Neuron::Client
{
  class Effects
  {
    public:
      // The floating origin this frame's render positions are measured against - the game hands
      // it over once per frame (the engine can't reach the game-side CameraRig). BuildVertices
      // rebases every particle against it.
      void SetOrigin(const Neuron::Math::Vector3i64& _origin) { m_origin = _origin; }

      // Spawn one particle at absolute world point _worldPos (see ParticleSystem::CreateParticle).
      void XM_CALLCONV CreateParticle(const Neuron::Math::Vector3i64& _worldPos, DirectX::FXMVECTOR _vel,
                                      ParticleTypeId _type, float _size = 0.0f)
      {
        m_particles.CreateParticle(_worldPos, _vel, _type, _size);
      }

      // Shatter a hull into tumbling debris at absolute world point _worldPos. _shipType selects
      // the mesh through the game-registered Scene3D mesh provider (the subsystem never touches
      // the ship tables itself); _basis rows are the hull's world basis ([0]=side, [1]=roof,
      // [2]=nose - the RenderRecord convention; pass identity when the orientation is unknown).
      // _fraction in (0,1] keeps that share of triangles. No-op if the type has no mesh.
      void AddExplosion(int _shipType, const Neuron::Math::Vector3i64& _worldPos,
                        const DirectX::XMFLOAT3X3& _basis, float _fraction = 1.0f);

      // Advance both simulations by _dt seconds, rebuild this frame's particle + debris vertex
      // batches against the stored origin + camera, and push them to the SceneParticles
      // renderer. Engine-driven from ClientEngine::Frame; the game never ticks it.
      void Advance(float _dt);

      // Reseed the VFX PRNGs (deterministic tests); engine-local, no dependency on the exe.
      void SetRandomSeed(uint32_t _seed)
      {
        m_particles.SetSeed(_seed);
        m_explosions.SetSeed(_seed);
      }

      [[nodiscard]] ParticleSystem& Particles() { return m_particles; }
      [[nodiscard]] ExplosionManager& Explosions() { return m_explosions; }

    private:
      ParticleSystem m_particles;
      ExplosionManager m_explosions;
      Neuron::Math::Vector3i64 m_origin;
      std::vector<Neuron::Graphics::ParticleVertex> m_vertScratch;   // reused each frame
      std::vector<Neuron::Graphics::MeshVertex> m_debrisScratch;     // reused each frame
  };

  // Process-wide effects subsystem (mirrors ReplicationClientInstance()). Engine-owned.
  [[nodiscard]] Effects& EffectsInstance();
}
