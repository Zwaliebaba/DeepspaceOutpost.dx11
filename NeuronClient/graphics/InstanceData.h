#pragma once

// InstanceData - the pure per-instance vertex payload behind H2's solid-mesh
// instancing.
//
// H2 draws every ship of one hull type in a single DrawIndexedInstanced call instead
// of one DrawIndexed per ship: the immutable per-type vertex/index buffers stay as
// they are (the SAME solid low-poly geometry - the look is identical), and a second,
// per-instance vertex stream carries each ship's world transform and colour tint. This
// header is the pure marshalling of one ModelDraw into that stream slot, so the packing
// (which the un-runnable D3D layer relies on) is pinned by the headless suite.
//
// The world matrix is stored ROW-vector (p_world = p_model * W): rows 0..2 are the
// ship's world basis (side / roof / nose), row 3 the floating-origin-relative position.
// The instanced vertex shader consumes it that way (mul(pos, W)), matching Render2D's
// convention rather than Scene3D's transposed column-vector upload - no transpose here.
//
// Deliberately free of D3D and DirectXMath (plain floats), so it is unit-tested headlessly.

#include <cstdint>

namespace Neuron::Graphics
{
  // One per-instance vertex: the row-vector world matrix (16 floats) followed by an RGBA
  // tint. tint.a < 0 means "use the mesh's own per-face colours" (the faithful solid look);
  // tint.a >= 0 overrides every face with tint.rgb (e.g. a team/selection wash). Laid out to
  // match the instanced input layout: four float4 world rows then one float4 tint (80 bytes).
  struct InstanceData
  {
    float world[16];
    float tint[4];
  };
  static_assert(sizeof(InstanceData) == 80, "InstanceData must match the instanced vertex stream");

  // Pack one model's world transform into an instance slot. `_rotmat` is the world basis
  // (row 0 side, 1 roof, 2 nose - exactly ModelDraw::rotmat) and `_location` the
  // floating-origin-relative position (ModelDraw::location); this reproduces the same
  // XMMatrixSet the per-model path built, but as plain row-major floats. By default the
  // tint is (0,0,0,-1) - alpha < 0, so the shader keeps the per-face colours.
  [[nodiscard]] inline InstanceData PackInstance(const double _rotmat[3][3], const double _location[3])
  {
    InstanceData d{};
    // Rows 0..2: the world basis, with a 0 in the homogeneous column.
    for (int r = 0; r < 3; ++r)
    {
      d.world[r * 4 + 0] = static_cast<float>(_rotmat[r][0]);
      d.world[r * 4 + 1] = static_cast<float>(_rotmat[r][1]);
      d.world[r * 4 + 2] = static_cast<float>(_rotmat[r][2]);
      d.world[r * 4 + 3] = 0.0f;
    }
    // Row 3: translation (floating-origin-relative), homogeneous 1.
    d.world[12] = static_cast<float>(_location[0]);
    d.world[13] = static_cast<float>(_location[1]);
    d.world[14] = static_cast<float>(_location[2]);
    d.world[15] = 1.0f;

    d.tint[0] = 0.0f;
    d.tint[1] = 0.0f;
    d.tint[2] = 0.0f;
    d.tint[3] = -1.0f; // use the mesh's per-face colours
    return d;
  }

  // Overload that also sets an RGBA8 colour override (0xAABBGGRR, R low byte - the palette
  // order). The alpha byte doubles as the "override on" flag: any opaque colour (a > 0)
  // washes every face with the given rgb; pass 0 to keep the per-face colours.
  [[nodiscard]] inline InstanceData PackInstance(const double _rotmat[3][3], const double _location[3],
                                                 uint32_t _tintRgba)
  {
    InstanceData d = PackInstance(_rotmat, _location);
    const float a = static_cast<float>((_tintRgba >> 24) & 0xFF) / 255.0f;
    if (a > 0.0f)
    {
      d.tint[0] = static_cast<float>(_tintRgba & 0xFF) / 255.0f;
      d.tint[1] = static_cast<float>((_tintRgba >> 8) & 0xFF) / 255.0f;
      d.tint[2] = static_cast<float>((_tintRgba >> 16) & 0xFF) / 255.0f;
      d.tint[3] = a;
    }
    return d;
  }
}
