#include "pch.h"

#include "Camera.h"

#include "elite.h"
#include "space.h"

namespace Neuron::Client
{
  namespace
  {
    // Client-only presentation state: which view mode is active and, for Chase,
    // how far the eye floats from the ship (in the ship's local frame).
    CameraMode g_cameraMode = CameraMode::Cockpit;
    ViewOffset g_chaseOffset{ /*right*/ 0.0, /*up*/ 12.0, /*forward*/ -45.0 };
  }

  void SetCameraMode(CameraMode _mode) { g_cameraMode = _mode; }
  CameraMode GetCameraMode() { return g_cameraMode; }
  void SetChaseOffset(const ViewOffset& _offset) { g_chaseOffset = _offset; }

  Camera CurrentCamera()
  {
    // The camera always looks along the ship's nose (the single forward view).
    Camera cam;

    // Eye position relative to the ship. In the legacy ship-relative frame the
    // followed ship is the origin with an identity local basis (nose = +z), so a
    // Chase offset maps straight onto the eye offset; Cockpit leaves it at zero.
    // When world positions replicate, FollowShip() in CameraFollow.h does this
    // same mapping against the ship's absolute transform to anchor the floating
    // origin.
    if (g_cameraMode == CameraMode::Chase)
    {
      cam.position.x = g_chaseOffset.right;
      cam.position.y = g_chaseOffset.up;
      cam.position.z = g_chaseOffset.forward;
    }

    return cam;
  }

  void ApplyCamera(const Camera& _cam, local_object* _obj)
  {
    // Eye offset from the ship (zero in Cockpit mode); it is the seam a
    // detached/third-person camera plugs into (render the world relative to the
    // eye, not the ship). Looking forward: ship-space already is view-space, so
    // no rotation follows.
    _obj->location.x -= _cam.position.x;
    _obj->location.y -= _cam.position.y;
    _obj->location.z -= _cam.position.z;
  }
}
