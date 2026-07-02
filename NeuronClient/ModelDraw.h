#pragma once

// ModelDraw - one 3D model instance for the GPU scene pass.
//
// The game's flight/space draw pass builds one of these per ship / planet / sun and hands it
// straight to Scene3D (Scene3D::SubmitModel); the client's Scene3D renderer turns it into an
// indexed draw with a real perspective + z-buffer. It carries the object's identity and its
// camera-space transform (orientation basis + position) plus presentation options - POD (no
// D3D, no game headers) so it stays a portable, headless-safe data contract.
//
// (Formerly defined in RenderQueue.h, the retired A1 render seam.)

#include <cstdint>

namespace Neuron::Render
{
  struct ModelDraw
  {
    int      type = 0;        // legacy SHIP_* model id (also SHIP_PLANET / SHIP_SUN billboards)
    int      style = 0;       // render style: ships 0=solid/1=wireframe; planet = planet_render_style
    int      colour = -1;     // palette index: ships < 0 keep face colours; planet/sun primary colour
    int      colour2 = -1;    // secondary palette index (banded planet styles); < 0 if unused
    uint32_t flags = 0;       // legacy local_object flags (e.g. FLG_FIRING)
    double   location[3] = {};         // camera-space position (x right, y up, z forward)
    double   rotmat[3][3] = {};        // orientation basis: row 0 = side, 1 = roof, 2 = nose
    double   distance = 0.0;  // camera distance (LOD / tie-break)
  };
}
