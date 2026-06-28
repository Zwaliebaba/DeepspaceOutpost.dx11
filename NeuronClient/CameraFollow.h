#pragma once

// CameraFollow - place the camera relative to the ship it follows (client A4).
//
// This is the absolute-space half of the ship/camera separation. The server is
// authoritative on a ship's WorldTransform (int64 position) and orientation; the
// camera is a pure client concept that *follows* that (eventually replicated)
// transform. FollowShip() computes the camera's world pose from the ship pose and
// a view offset expressed in the ship's own frame, so the rig stays rigid as the
// ship rolls and pitches:
//
//   cockpit : offset {0,0,0}            -> eye sits exactly on the ship
//   chase   : offset {0, +up, -back}    -> eye floats above and behind it
//
// Header-only and dependency-light (NeuronCore math only), so it is unit-tested
// headlessly even though it belongs to the client. When replication lands, the
// returned eye position is what anchors the client's floating origin.

#include <cmath>

#include "Vector3i64.h"
#include "Vector3d.h"

namespace Neuron::Client
{
  // A camera offset from the ship, in the ship's LOCAL frame (right/up/forward
  // along the ship's side/roof/nose vectors). Cockpit is all-zero.
  struct ViewOffset
  {
    double right = 0.0;
    double up = 0.0;
    double forward = 0.0;
  };

  // The camera's resolved world pose: an absolute eye position plus the basis it
  // looks along (a rigid follow shares the ship's facing).
  struct CameraPose
  {
    Math::Vector3i64 eye{};
    Math::Vector3d side{ 1.0, 0.0, 0.0 };
    Math::Vector3d roof{ 0.0, 1.0, 0.0 };
    Math::Vector3d nose{ 0.0, 0.0, 1.0 };
  };

  // Compute the camera pose for a ship at `_shipPos` with orientation basis
  // (`_side`,`_roof`,`_nose`), offset by `_offset` in that local frame. The local
  // offset is rotated into world space by the ship's basis and added to the
  // (integer) ship position; the result is rounded to the nearest world unit.
  [[nodiscard]] inline CameraPose FollowShip(
      const Math::Vector3i64& _shipPos,
      const Math::Vector3d& _side,
      const Math::Vector3d& _roof,
      const Math::Vector3d& _nose,
      const ViewOffset& _offset)
  {
    const Math::Vector3d world =
        _side * _offset.right + _roof * _offset.up + _nose * _offset.forward;

    CameraPose pose;
    pose.eye = _shipPos + Math::Vector3i64{
      static_cast<int64_t>(std::llround(world.x)),
      static_cast<int64_t>(std::llround(world.y)),
      static_cast<int64_t>(std::llround(world.z)),
    };
    pose.side = _side;
    pose.roof = _roof;
    pose.nose = _nose;
    return pose;
  }
}
