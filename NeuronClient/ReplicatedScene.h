#pragma once

// ReplicatedScene - turn replicated snapshots into client render records.
//
// The bridge for the client's "last hop": it takes the interpolated, absolute
// int64 entity snapshots and produces draw-ready records in the WORLD frame,
// rebased about a floating origin (the camera's eye). The camera is decoupled
// from the ship, so no rotation happens here - the view transform is the
// Camera's job (Scene3D applies View() * Projection() on the GPU; the few CPU
// paths view-transform per object). The local player's ship is included: with
// no cockpit view it renders like any other entity.
//
// Pure math over POD types (no D3D, no game state), so the conversion is unit-
// tested headlessly; the thin glue that pushes records through draw_ship() lives
// in the client and is build-verified.

#include <cmath>
#include <cstdint>
#include <vector>

#include "vector.h"          // legacy Vector / Matrix (POD)

#include "Replication.h"

namespace Neuron::Client
{
  // A single draw-ready entity in the world frame (floating-origin-relative).
  struct RenderRecord
  {
    uint32_t id = 0;
    int type = 0;                       // replicated ship type (legacy SHIP_*)
    Vector location{ 0.0, 0.0, 0.0 };   // world offset from the floating origin
    Matrix rotmat{};                    // world basis: [0]=side, [1]=roof, [2]=nose
    double distance = 0.0;              // distance from the origin (the camera)
  };

  // Build render records for every replicated entity, rebased about the given
  // floating origin (the camera eye, rounded to int64). Positions become small
  // world-frame offsets (exact integer subtraction before any float math, per
  // the floating-origin doctrine); orientations stay in the world frame - the
  // camera's view matrix rotates them at render time.
  [[nodiscard]] inline std::vector<RenderRecord> BuildRenderRecords(
      const std::vector<Net::EntitySnapshot>& _entities,
      int64_t _originX, int64_t _originY, int64_t _originZ)
  {
    std::vector<RenderRecord> records;
    records.reserve(_entities.size());

    for (const Net::EntitySnapshot& e : _entities)
    {
      const double wx = static_cast<double>(e.x - _originX);
      const double wy = static_cast<double>(e.y - _originY);
      const double wz = static_cast<double>(e.z - _originZ);

      RenderRecord r;
      r.id = e.id;
      r.type = e.type;
      r.location = Vector{ wx, wy, wz };
      r.distance = std::sqrt(wx * wx + wy * wy + wz * wz);

      // The entity's world basis: nose/roof replicate; side = roof x nose.
      const Vector eNose{ e.noseX, e.noseY, e.noseZ };
      const Vector eRoof{ e.roofX, e.roofY, e.roofZ };
      const Vector eSide{ eRoof.y * eNose.z - eRoof.z * eNose.y,
                          eRoof.z * eNose.x - eRoof.x * eNose.z,
                          eRoof.x * eNose.y - eRoof.y * eNose.x };

      r.rotmat[0] = eSide;
      r.rotmat[1] = eRoof;
      r.rotmat[2] = eNose;

      records.push_back(r);
    }

    return records;
  }
}
