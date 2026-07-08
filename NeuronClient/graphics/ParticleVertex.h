#pragma once

#include <cstdint>

// The additive particle billboard vertex, factored out of SceneParticles so the CPU-side
// simulation (Neuron::Client::ParticleSystem) and its headless tests can name it without pulling
// in D3D. One camera-facing quad corner: render-frame position, sprite uv, and a packed RGBA8
// colour (0xAABBGGRR, R in the low byte - the same order Render2D / the palette use). The GPU
// input layout in SceneParticles matches this exactly.

namespace Neuron::Graphics
{
  struct ParticleVertex
  {
    float x, y, z;
    float u, v;
    uint32_t rgba;
  };
}
