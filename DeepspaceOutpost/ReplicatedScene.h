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
  // The floating origin is the local player's snapshot position (or the world
  // origin if it is not in the set yet), so positions stay small and float-safe.
  [[nodiscard]] inline std::vector<RenderRecord> BuildRenderRecords(
      const std::vector<Net::EntitySnapshot>& _entities, uint32_t _localPlayerId)
  {
    Math::Vector3i64 origin{ 0, 0, 0 };
    for (const Net::EntitySnapshot& e : _entities)
    {
      if (e.id == _localPlayerId)
      {
        origin = Math::Vector3i64{ e.x, e.y, e.z };
        break;
      }
    }

    std::vector<RenderRecord> records;
    records.reserve(_entities.size());
    for (const Net::EntitySnapshot& e : _entities)
    {
      if (e.id == _localPlayerId)
        continue;

      RenderRecord r;
      r.id = e.id;
      r.type = e.type;
      r.location.x = static_cast<double>(e.x - origin.x);
      r.location.y = static_cast<double>(e.y - origin.y);
      r.location.z = static_cast<double>(e.z - origin.z);
      r.distance = std::sqrt(r.location.x * r.location.x +
                             r.location.y * r.location.y +
                             r.location.z * r.location.z);

      const Vector nose{ e.noseX, e.noseY, e.noseZ };
      const Vector roof{ e.roofX, e.roofY, e.roofZ };
      const Vector side{ roof.y * nose.z - roof.z * nose.y,
                         roof.z * nose.x - roof.x * nose.z,
                         roof.x * nose.y - roof.y * nose.x };   // side = roof x nose

      r.rotmat[0] = side;
      r.rotmat[1] = roof;
      r.rotmat[2] = nose;

      records.push_back(r);
    }

    return records;
  }
}
