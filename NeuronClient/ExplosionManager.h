#pragma once

#include <cstdint>
#include <random>
#include <vector>

#include <DirectXMath.h>

#include "Vector3i64.h"   // Neuron::Math::Vector3i64 - absolute world anchor
#include "Mesh.h"         // Neuron::Graphics::MeshData / MeshVertex - the shattered hull triangles

// Ported Darwinia mesh-shatter explosion, space-adapted (explosion.md §6b). A dead hull's mesh
// breaks into one flying triangle per mesh face triangle: each fragment gets an outward velocity
// from the hull centre, a shared Tumbler (a rotation nudged every frame so the piece spins),
// light friction and a lifetime; the fragments shrink away over the last part of the life (the
// debris pass draws opaque, so the fade is geometric, not alpha). Zero-g: the donor's gravity
// and vertical kick are dropped.
//
// Pure CPU simulation - no D3D, device-free, headlessly unit-testable. The manager consumes a
// neutral MeshData (the Effects subsystem resolves it from the game-registered Scene3D mesh
// provider), so it carries no game-specific data.

namespace Neuron::Client
{
  // Debris tuning. Speeds scale with the hull's own radius, so one set of constants fits every
  // hull size. Placeholder values (donor table unavailable); flagged for phase-5 tuning.
  inline constexpr float EXPLOSION_LIFETIME = 3.0f;     // seconds a shatter lives
  inline constexpr float EXPLOSION_SHRINK_START = 0.6f; // fraction of life where shrink begins
  inline constexpr float DEBRIS_FRICTION = 0.6f;        // velocity damping per second
  inline constexpr float DEBRIS_ROT_FRICTION = 0.7f;    // tumble damping per second
  inline constexpr float DEBRIS_SPEED_FACTOR = 1.2f;    // outward speed per unit of centre offset / s
  inline constexpr int NUM_TUMBLERS = 16;               // shared spin states per explosion

  // Per-fragment spin, shared by NUM_TUMBLERS-sized groups of fragments (donor Tumbler). The
  // rotation is storage-only (XMFLOAT3X3); Advance composes the frame's axis-angle nudge in
  // XMMATRIX and damps the angular velocity toward a slow drift.
  struct Tumbler
  {
    DirectX::XMFLOAT3X3 rot;    // orientation applied to the fragment's centred verts
    DirectX::XMFLOAT3 angVel;   // axis * rate (radians / second)
    void Advance(float _dt);
  };

  // One flying triangle: world-oriented corners relative to the fragment's own centre, the
  // integrated centre offset (render frame, relative to the explosion anchor), and the flat
  // face colour from the source mesh. Storage-only vectors; arithmetic stays in XMVECTOR.
  struct ExplodingTri
  {
    DirectX::XMFLOAT3 v1, v2, v3;
    DirectX::XMFLOAT3 normal;
    DirectX::XMFLOAT3 offset;
    DirectX::XMFLOAT3 vel;
    uint32_t colour;
    int tumbler;
  };

  // One shattered hull: the fragment list, the shared tumblers, and the absolute anchor the
  // render positions rebase against. Built from a MeshData whose triangles are already
  // fan-triangulated (triangle t = indices[3t..3t+2]) - no fragment tree (explosion.md §6b).
  class Explosion
  {
    public:
      // _basis rows are the hull's world basis ([0]=side, [1]=roof, [2]=nose - the RenderRecord
      // convention); corners/normals/velocities are oriented by it at build time so the sim and
      // render never re-apply it. _fraction in (0,1] keeps that share of triangles (donor
      // behaviour); degenerate/tiny triangles are skipped. _rng drives the random spins/culls.
      Explosion(const Neuron::Graphics::MeshData& _mesh, const Neuron::Math::Vector3i64& _anchor,
                const DirectX::XMFLOAT3X3& _basis, float _fraction, std::minstd_rand& _rng);

      // Integrate fragments + tumblers by _dt seconds; returns true when the explosion has
      // outlived EXPLOSION_LIFETIME and should be dropped.
      bool Advance(float _dt);

      // Append the live fragments as flat-shaded triangles (3 MeshVertex each) in the render
      // frame: tumbled centred corners + integrated offset + (anchor - camOrigin). Fragments
      // shrink to nothing over the last (1 - EXPLOSION_SHRINK_START) of the life.
      void AppendVertices(const Neuron::Math::Vector3i64& _camOrigin,
                          std::vector<Neuron::Graphics::MeshVertex>& _out) const;

      [[nodiscard]] size_t TriCount() const { return m_tris.size(); }
      [[nodiscard]] float Age() const { return m_age; }
      [[nodiscard]] const std::vector<Tumbler>& Tumblers() const { return m_tumblers; }

    private:
      Neuron::Math::Vector3i64 m_anchor;
      std::vector<ExplodingTri> m_tris;
      std::vector<Tumbler> m_tumblers;
      float m_age = 0.0f;
  };

  // The pool of live explosions (donor ExplosionManager; LList -> std::vector).
  class ExplosionManager
  {
    public:
      void AddExplosion(const Neuron::Graphics::MeshData& _mesh, const Neuron::Math::Vector3i64& _anchor,
                        const DirectX::XMFLOAT3X3& _basis, float _fraction = 1.0f);

      // Advance every explosion and compact the finished ones.
      void Advance(float _dt);

      // Append every live explosion's debris triangles for this frame's SetDebris batch.
      void AppendVertices(const Neuron::Math::Vector3i64& _camOrigin,
                          std::vector<Neuron::Graphics::MeshVertex>& _out) const;

      // Reseed the VFX PRNG (deterministic tests); engine-local, like ParticleSystem::SetSeed.
      void SetSeed(uint32_t _seed) { m_rng.seed(_seed); }

      [[nodiscard]] size_t Count() const { return m_explosions.size(); }
      [[nodiscard]] const std::vector<Explosion>& Explosions() const { return m_explosions; }

    private:
      std::vector<Explosion> m_explosions;
      std::minstd_rand m_rng{ 24680u }; // fixed default seed; SetSeed reseeds for tests
  };
}
