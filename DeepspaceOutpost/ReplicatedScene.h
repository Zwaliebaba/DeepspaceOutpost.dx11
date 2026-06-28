#pragma once

// ReplicatedScene - turn replicated snapshots into client render records.
//
// The bridge for the client's "last hop": it takes the interpolated, absolute
// int64 entity snapshots and produces draw-ready records in the legacy render
// frame - position relative to a floating origin (the local player), and the
// orientation basis unpacked into the legacy rotmat (side/roof/nose). The local
// player is the origin and is not drawn (you don't render your own cockpit ship).
//
// Pure math over POD types (no D3D, no game state), so the conversion is unit-
// tested headlessly; the thin glue that pushes records through draw_ship() lives
// in the client and is build-verified.

#include <cmath>
#include <vector>

#include "vector.h"          // legacy Vector / Matrix (POD)

#include "Vector3i64.h"
#include "Replication.h"

namespace Neuron::Client
{
  // A single draw-ready entity in the legacy render frame.
  struct RenderRecord
  {
    uint32_t id = 0;
    int type = 0;                       // replicated ship type (legacy SHIP_*)
    Vector location{ 0.0, 0.0, 0.0 };   // relative to the floating origin
    Matrix rotmat{};                    // [0]=side, [1]=roof, [2]=nose
    double distance = 0.0;
  };

  // Build render records for every replicated entity except the local player.
  // Build render records relative to the local player's ship. The camera IS the
  // ship: the world is rebased to the ship's position (floating origin) AND rotated
  // into the ship's basis (side/roof/nose), so rolling/pitching the ship rotates
  // the view. Until the local ship is present in the snapshot we render nothing
  // (rather than rebase against a bogus origin). With a default ship orientation
  // (nose +z, roof +y) the rotation is the identity, so the legacy front view is
  // reproduced exactly.
  [[nodiscard]] inline std::vector<RenderRecord> BuildRenderRecords(
      const std::vector<Net::EntitySnapshot>& _entities, uint32_t _localPlayerId)
  {
    const Net::EntitySnapshot* me = nullptr;
    for (const Net::EntitySnapshot& e : _entities)
    {
      if (e.id == _localPlayerId)
      {
        me = &e;
        break;
      }
    }

    std::vector<RenderRecord> records;
    if (me == nullptr)
      return records;   // we don't know where we are yet

    const Math::Vector3i64 origin{ me->x, me->y, me->z };

    const Vector pNose{ me->noseX, me->noseY, me->noseZ };
    const Vector pRoof{ me->roofX, me->roofY, me->roofZ };
    const Vector pSide{ pRoof.y * pNose.z - pRoof.z * pNose.y,
                        pRoof.z * pNose.x - pRoof.x * pNose.z,
                        pRoof.x * pNose.y - pRoof.y * pNose.x };

    // Express a world-frame offset in the ship's view frame using the SAME
    // convention as the legacy move_local_object() and the locally-rendered
    // starfield: the world rotates around a fixed cockpit, so the offset is rebuilt
    // from the ship's basis (x*side + y*roof + z*nose) rather than projected onto it
    // (x*side, y*roof, z*nose). That is the transpose of a textbook view matrix, but
    // it is what the starfield uses; matching it keeps replicated objects (planet,
    // station, ships) rotating WITH the stars instead of counter to them when the
    // ship rolls or pitches. At the identity orientation the two are identical.
    const auto toCamera = [&](double _x, double _y, double _z) -> Vector
    {
      return Vector{ _x * pSide.x + _y * pRoof.x + _z * pNose.x,
                     _x * pSide.y + _y * pRoof.y + _z * pNose.y,
                     _x * pSide.z + _y * pRoof.z + _z * pNose.z };
    };

    records.reserve(_entities.size());
    for (const Net::EntitySnapshot& e : _entities)
    {
      if (e.id == _localPlayerId)
        continue;

      const double wx = static_cast<double>(e.x - origin.x);
      const double wy = static_cast<double>(e.y - origin.y);
      const double wz = static_cast<double>(e.z - origin.z);

      RenderRecord r;
      r.id = e.id;
      r.type = e.type;
      r.location = toCamera(wx, wy, wz);
      r.distance = std::sqrt(wx * wx + wy * wy + wz * wz);

      // The entity's own orientation, also expressed in the camera frame.
      const Vector eNose{ e.noseX, e.noseY, e.noseZ };
      const Vector eRoof{ e.roofX, e.roofY, e.roofZ };
      const Vector eSide{ eRoof.y * eNose.z - eRoof.z * eNose.y,
                          eRoof.z * eNose.x - eRoof.x * eNose.z,
                          eRoof.x * eNose.y - eRoof.y * eNose.x };

      r.rotmat[0] = toCamera(eSide.x, eSide.y, eSide.z);
      r.rotmat[1] = toCamera(eRoof.x, eRoof.y, eRoof.z);
      r.rotmat[2] = toCamera(eNose.x, eNose.y, eNose.z);

      records.push_back(r);
    }

    return records;
  }
}
