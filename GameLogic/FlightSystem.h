#pragma once

// FlightSystem - advance steerable craft one fixed simulation tick (GameLogic).
//
// The authoritative, absolute-space reimplementation of the legacy flight model.
// The legacy move_local_object() kept the player ship fixed at the origin and
// rotated/translated the entire world around it (a player-relative frame). The
// server cannot do that - there are up to 100 ships and one shared world - so
// here each ship instead carries its own orientation and moves through a static
// int64 world.
//
// The rotation feel is preserved by reusing the legacy primitives: RotateAxis()
// is rotate_vec() and Orthonormalize() is tidy_matrix(), applied to the ship's
// own basis rather than to every world object. Forward motion is `nose * speed`,
// committed to the integer position with a sub-unit carry so slow craft still
// move deterministically.

#include "ECS.h"

#include "SimComponents.h"

namespace Neuron::GameLogic
{
  namespace Detail
  {
    // Incrementally rotate a basis vector by roll (alpha) then pitch (beta).
    // Bit-for-bit the legacy rotate_vec().
    inline void RotateAxis(Math::Vector3d& _v, double _roll, double _pitch)
    {
      double x = _v.x;
      double y = _v.y;
      double z = _v.z;

      y = y - _roll * x;
      x = x + _roll * y;
      y = y - _pitch * z;
      z = z + _pitch * y;

      _v.x = x;
      _v.y = y;
      _v.z = z;
    }

    // Rotate a ship's basis by this tick's roll/pitch about the ship's OWN axes
    // (side = roll/pitch's pivots), preserving the legacy rotate_vec feel.
    //
    // Applying RotateAxis() straight to the world-space basis vectors (as an
    // earlier version did) rotates them about the WORLD x/z axes, which is only
    // correct while the ship sits at the identity orientation. Once the ship has
    // turned, that couples the controls into the wrong axis - e.g. after a roll a
    // pure pitch also yaws, so an object dead ahead drifts sideways across the
    // screen instead of moving straight up/down. Instead we express the rotation
    // in the ship's LOCAL frame: rotate the standard basis (giving the rotation's
    // columns in local coordinates) and re-project through the current basis,
    // i.e. B' = B * M. At the identity orientation this is bit-identical to the
    // legacy world-frame version, so the ported feel and the golden runs are
    // preserved; for a turned ship it correctly pivots about side/roof/nose.
    inline void RotateBasis(Flight& _f)
    {
      Math::Vector3d localX{ 1.0, 0.0, 0.0 };
      Math::Vector3d localY{ 0.0, 1.0, 0.0 };
      Math::Vector3d localZ{ 0.0, 0.0, 1.0 };
      RotateAxis(localX, _f.roll, _f.pitch);
      RotateAxis(localY, _f.roll, _f.pitch);
      RotateAxis(localZ, _f.roll, _f.pitch);

      const Math::Vector3d side = _f.side;
      const Math::Vector3d roof = _f.roof;
      const Math::Vector3d nose = _f.nose;

      _f.side = side * localX.x + roof * localX.y + nose * localX.z;
      _f.roof = side * localY.x + roof * localY.y + nose * localY.z;
      _f.nose = side * localZ.x + roof * localZ.y + nose * localZ.z;
    }

    // Re-orthonormalize a ship's basis after the incremental rotation has let it
    // drift. Port of tidy_matrix() with nose = mat[2], roof = mat[1],
    // side = mat[0]: renormalize the nose, square the roof against it, then
    // rebuild the side as roof x nose.
    inline void Orthonormalize(Flight& _f)
    {
      _f.nose = Math::Normalized(_f.nose);

      if (_f.nose.x > -1.0 && _f.nose.x < 1.0)
      {
        if (_f.nose.y > -1.0 && _f.nose.y < 1.0)
          _f.roof.z = -(_f.nose.x * _f.roof.x + _f.nose.y * _f.roof.y) / _f.nose.z;
        else
          _f.roof.y = -(_f.nose.x * _f.roof.x + _f.nose.z * _f.roof.z) / _f.nose.y;
      }
      else
      {
        _f.roof.x = -(_f.nose.y * _f.roof.y + _f.nose.z * _f.roof.z) / _f.nose.x;
      }

      _f.roof = Math::Normalized(_f.roof);
      _f.side = Math::Cross(_f.roof, _f.nose);
    }
  }

  // Step every craft carrying a Flight + WorldTransform: rotate its basis by the
  // current roll/pitch, re-orthonormalize, then advance the integer position
  // along the nose by `speed`, carrying the sub-unit remainder.
  inline void StepFlight(ECS::Registry& _world)
  {
    _world.Each<WorldTransform, Flight>([&_world](ECS::EntityId _id, WorldTransform& _t, Flight& _f)
    {
      if (_world.Has<Missile>(_id))
        return;   // missiles are steered + advanced by StepMissiles, not the flight model

      Detail::RotateBasis(_f);

      Detail::Orthonormalize(_f);

      // Displacement this tick = nose * speed, plus whatever sub-unit motion was
      // left over last tick. Commit the whole-unit part to the int64 position and
      // keep the fraction for next tick (truncation toward zero is symmetric, so
      // forward/backward motion accrues identically).
      const Math::Vector3d step = _f.nose * _f.speed + _f.carry;

      const int64_t wx = static_cast<int64_t>(step.x);
      const int64_t wy = static_cast<int64_t>(step.y);
      const int64_t wz = static_cast<int64_t>(step.z);

      _t.position += Math::Vector3i64{ wx, wy, wz };

      _f.carry = Math::Vector3d{
        step.x - static_cast<double>(wx),
        step.y - static_cast<double>(wy),
        step.z - static_cast<double>(wz),
      };
    });
  }
}
