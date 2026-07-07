#include "pch.h"
#include "Effects.h"

#include "Camera.h"          // Neuron::Client::MainCamera
#include "SceneParticles.h"  // Neuron::Graphics::SceneParticles

namespace Neuron::Client
{
  void Effects::Advance(float _dt)
  {
    m_particles.Advance(_dt);

    // Rebuild this frame's particle batch against the game-supplied origin and the live camera,
    // then hand it to the renderer (the Scene3D::SetDust precedent). Empty batch -> SetParticles
    // clears the pass, so a frame with no effects draws nothing.
    m_vertScratch.clear();
    m_particles.BuildVertices(m_origin, MainCamera(), m_vertScratch);
    Neuron::Graphics::SceneParticles::SetParticles(m_vertScratch.data(),
                                                   static_cast<int>(m_vertScratch.size()));
  }

  Effects& EffectsInstance()
  {
    static Effects instance;
    return instance;
  }
}
