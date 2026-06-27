#include "pch.h"

#include "Camera.h"

#include "config.h"
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
    // The look direction comes from the active screen. (Game Over keeps the rear
    // view, matching the legacy.)
    Camera cam;

    // Eye position relative to the ship. In the legacy ship-relative frame the
    // followed ship is the origin with an identity local basis (nose = +z), so a
    // Chase offset maps straight onto the eye offset; Cockpit leaves it at zero
    // and the result is bit-identical to the old switch_to_view(). When world
    // positions replicate, FollowShip() in CameraFollow.h does this same mapping
    // against the ship's absolute transform to anchor the floating origin.
    if (g_cameraMode == CameraMode::Chase)
    {
      cam.position.x = g_chaseOffset.right;
      cam.position.y = g_chaseOffset.up;
      cam.position.z = g_chaseOffset.forward;
    }

    switch (current_screen)
    {
      case SCR_REAR_VIEW:
      case SCR_GAME_OVER:
        cam.direction = ViewDirection::Rear;
        break;
      case SCR_LEFT_VIEW:
        cam.direction = ViewDirection::Left;
        break;
      case SCR_RIGHT_VIEW:
        cam.direction = ViewDirection::Right;
        break;
      default:
        cam.direction = ViewDirection::Front;
        break;
    }
    return cam;
  }

  void ApplyCamera(const Camera& _cam, local_object* _obj)
  {
    // Eye offset from the ship. Zero today, so this is a no-op that preserves the
    // legacy behaviour exactly; it is the seam a detached/third-person camera
    // plugs into (render the world relative to the eye, not the ship).
    _obj->location.x -= _cam.position.x;
    _obj->location.y -= _cam.position.y;
    _obj->location.z -= _cam.position.z;

    double tmp;

    switch (_cam.direction)
    {
      case ViewDirection::Rear:
        _obj->location.x = -_obj->location.x;
        _obj->location.z = -_obj->location.z;

        _obj->rotmat[0].x = -_obj->rotmat[0].x;
        _obj->rotmat[0].z = -_obj->rotmat[0].z;

        _obj->rotmat[1].x = -_obj->rotmat[1].x;
        _obj->rotmat[1].z = -_obj->rotmat[1].z;

        _obj->rotmat[2].x = -_obj->rotmat[2].x;
        _obj->rotmat[2].z = -_obj->rotmat[2].z;
        return;

      case ViewDirection::Left:
        tmp = _obj->location.x;
        _obj->location.x = _obj->location.z;
        _obj->location.z = -tmp;

        if (_obj->type < 0)
          return;

        tmp = _obj->rotmat[0].x;
        _obj->rotmat[0].x = _obj->rotmat[0].z;
        _obj->rotmat[0].z = -tmp;

        tmp = _obj->rotmat[1].x;
        _obj->rotmat[1].x = _obj->rotmat[1].z;
        _obj->rotmat[1].z = -tmp;

        tmp = _obj->rotmat[2].x;
        _obj->rotmat[2].x = _obj->rotmat[2].z;
        _obj->rotmat[2].z = -tmp;
        return;

      case ViewDirection::Right:
        tmp = _obj->location.x;
        _obj->location.x = -_obj->location.z;
        _obj->location.z = tmp;

        if (_obj->type < 0)
          return;

        tmp = _obj->rotmat[0].x;
        _obj->rotmat[0].x = -_obj->rotmat[0].z;
        _obj->rotmat[0].z = tmp;

        tmp = _obj->rotmat[1].x;
        _obj->rotmat[1].x = -_obj->rotmat[1].z;
        _obj->rotmat[1].z = tmp;

        tmp = _obj->rotmat[2].x;
        _obj->rotmat[2].x = -_obj->rotmat[2].z;
        _obj->rotmat[2].z = tmp;
        return;

      case ViewDirection::Front:
      default:
        // Looking forward: ship-space already is view-space.
        return;
    }
  }
}
