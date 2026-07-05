#pragma once

// GaussianKernel - the pure blur weights behind H4's emissive-glow pass.
//
// H4 renders the solid low-poly scene a second time into an emissive target, blurs
// that with a separable Gaussian, and additively composites the blur back over the
// sharp scene to give the retro-vector meshes their glow. The blur weights are the
// one piece of that pass that is pure math and can be pinned in CI; the HLSL blur
// shader (blurPS.hlsl) bakes the same weights as literals. Deliberately free of D3D
// (plain floats + std::vector) so it is unit-tested headlessly.
//
// (This header once also held the screen-space wire-expansion math for a glowing
// *wireframe* look; that was dropped when the art direction settled on solid meshes.)

#include <cmath>
#include <vector>

namespace Neuron::Graphics
{
  // Normalised 1D Gaussian weights for a separable blur of the given radius (the
  // kernel has 2*radius+1 taps, symmetric, summing to 1). sigma defaults to
  // radius/2. Used by the emissive-glow blur pass; pure + unit-tested.
  [[nodiscard]] inline std::vector<float> GaussianKernel(int _radius, float _sigma = 0.0f)
  {
    std::vector<float> w;
    if (_radius < 0)
      return w;
    const float sigma = (_sigma > 0.0f) ? _sigma : (static_cast<float>(_radius) * 0.5f + 0.0001f);
    float sum = 0.0f;
    for (int i = -_radius; i <= _radius; ++i)
    {
      const float g = std::exp(-static_cast<float>(i * i) / (2.0f * sigma * sigma));
      w.push_back(g);
      sum += g;
    }
    for (float& v : w)
      v /= sum;
    return w;
  }
}
