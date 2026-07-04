#pragma once

// ModelDraw - one 3D model instance for the GPU scene pass.
//
// The game's flight/space draw pass builds one of these per ship / planet / sun and hands it
// straight to Scene3D (Scene3D::SubmitModel); the client's Scene3D renderer composes it with
// the Camera's view + projection and draws it with a real z-buffer. It carries the object's
// identity and its WORLD-frame transform (floating-origin-relative position + world basis)
// plus presentation options - POD (no D3D, no game headers) so it stays a portable,
// headless-safe data contract.
//
// (Formerly defined in RenderQueue.h, the retired A1 render seam.)

#include <cstdint>

namespace Neuron::Render
{
  struct ModelDraw
  {
    int      type = 0;        // legacy SHIP_* model id (also SHIP_PLANET / SHIP_SUN billboards)
    int      colour = -1;     // palette index: ships < 0 keep face colours; planet/sun primary colour
    uint32_t flags = 0;       // legacy local_object flags (e.g. FLG_FIRING)
    double   location[3] = {};         // world-frame position, relative to the floating origin
    double   rotmat[3][3] = {};        // world basis: row 0 = side, 1 = roof, 2 = nose
    double   distance = 0.0;  // distance from the origin/camera (billboard sizing / tie-break)
  };
}
