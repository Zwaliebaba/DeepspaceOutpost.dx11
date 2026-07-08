#include "pch.h"
#include "Effects.h"

#include "Camera.h"          // Neuron::Client::MainCamera
#include "Scene3D.h"         // Neuron::Graphics::Scene3D::BuildMeshData (the registered provider)
#include "SceneParticles.h"  // Neuron::Graphics::SceneParticles

namespace Neuron::Client
{
  void Effects::AddExplosion(int _shipType, const Neuron::Math::Vector3i64& _worldPos,
                             const DirectX::XMFLOAT3X3& _basis, float _fraction)
  {
    // Resolve the hull mesh through the game's one provider registration (SceneMeshes.cpp ->
    // Scene3D::SetMeshProvider); the manager itself stays neutral MeshData in, triangles out.
    Neuron::Graphics::MeshData mesh;
    if (Neuron::Graphics::Scene3D::BuildMeshData(_shipType, mesh) && !mesh.vertices.empty())
      m_explosions.AddExplosion(mesh, _worldPos, _basis, _fraction);
  }

  void Effects::Advance(float _dt)
  {
    m_particles.Advance(_dt);
    m_explosions.Advance(_dt);

    // Rebuild this frame's batches against the game-supplied origin (and the live camera for
    // the billboards), then hand them to the renderer (the Scene3D::SetDust precedent). Empty
    // batches clear the passes, so a frame with no effects draws nothing.
    m_vertScratch.clear();
    m_particles.BuildVertices(m_origin, MainCamera(), m_vertScratch);
    Neuron::Graphics::SceneParticles::SetParticles(m_vertScratch.data(),
                                                   static_cast<int>(m_vertScratch.size()));

    m_debrisScratch.clear();
    m_explosions.AppendVertices(m_origin, m_debrisScratch);
    Neuron::Graphics::SceneParticles::SetDebris(m_debrisScratch.data(),
                                                static_cast<int>(m_debrisScratch.size()));
  }

  Effects& EffectsInstance()
  {
    static Effects instance;
    return instance;
  }
}
