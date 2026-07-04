#pragma once

// Camera - the explicit viewpoint the scene is rendered from (client seam, A4).
//
// In the legacy engine the camera is fused with the player ship at the world
// origin: every object's `location` is stored relative to the ship. This
// promotes that viewpoint to a first-class, named object so a later detached or
// third-person view can offset the eye from the ship WITHOUT touching game
// logic - the ship's position and the camera's position become independent.
//
// The camera always looks along the ship's nose: the cockpit has a single fixed
// forward view (the legacy rear/left/right views were removed). CurrentCamera()
// puts the eye exactly on the ship (zero offset); the `position` field is the
// seam: set it non-zero and the world is rendered from a point offset from the
// ship.

#include "vector.h"

#include "CameraFollow.h"

struct local_object;

namespace Neuron::Client
{
  // Where the eye sits relative to the followed ship. Cockpit is the legacy
  // fused view (eye on the ship); Chase floats the eye behind/above it via a
  // ViewOffset. Default is Cockpit, so behaviour is unchanged until toggled.
  enum class CameraMode
  {
    Cockpit,
    Chase,
  };

  // Select the active camera mode and the offset used by Chase (in the ship's
  // local frame). These are client-only presentation state.
  void SetCameraMode(CameraMode _mode);
  [[nodiscard]] CameraMode GetCameraMode();
  void SetChaseOffset(const ViewOffset& _offset);

  struct Camera
  {
    Vector position{ 0.0, 0.0, 0.0 };           // eye offset from the ship (0 = cockpit)
  };

  // The camera for the current frame. The eye is on the ship (Cockpit) or floats
  // behind it (Chase); the look direction is always the ship's nose.
  [[nodiscard]] Camera CurrentCamera();

  // Transform `_obj` from ship-space into `_cam`'s view-space: translate by the
  // eye offset. Ship-space already is forward-view space.
  void ApplyCamera(const Camera& _cam, local_object* _obj);
}
