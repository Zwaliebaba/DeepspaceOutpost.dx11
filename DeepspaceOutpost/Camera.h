#pragma once

// Camera - the explicit viewpoint the scene is rendered from (client seam, A4).
//
// In the legacy engine the camera is fused with the player ship at the world
// origin: every object's `location` is stored relative to the ship, and the only
// view freedom is the four fixed directions (front/rear/left/right), applied by
// the old switch_to_view(). This promotes that viewpoint to a first-class, named
// object so a later detached or third-person view can offset the eye from the
// ship WITHOUT touching game logic - the ship's position and the camera's
// position become independent.
//
// Behaviour is unchanged today: CurrentCamera() puts the eye exactly on the ship
// (zero offset) with the direction taken from the active screen, so ApplyCamera()
// reproduces switch_to_view() bit for bit. The `position` field is the seam: set
// it non-zero and the world is rendered from a point offset from the ship.

#include "vector.h"

struct local_object;

namespace Neuron::Client
{
  // The four fixed view directions of the cockpit (front/rear/left/right).
  enum class ViewDirection
  {
    Front,
    Rear,
    Left,
    Right,
  };

  struct Camera
  {
    Vector position{ 0.0, 0.0, 0.0 };           // eye offset from the ship (0 = cockpit)
    ViewDirection direction = ViewDirection::Front;
  };

  // The camera for the current frame, derived from the active screen/view. Today
  // the eye is always on the ship; only the direction varies.
  [[nodiscard]] Camera CurrentCamera();

  // Transform `_obj` from ship-space into `_cam`'s view-space: translate by the
  // (currently zero) eye offset, then apply the fixed view rotation. This is the
  // behaviour formerly hard-coded in switch_to_view().
  void ApplyCamera(const Camera& _cam, local_object* _obj);
}
