#pragma once

// ReplicatedScene - turn replicated snapshots into client render records.
//
// The bridge for the client's "last hop": it takes the interpolated, absolute
// int64 entity snapshots and produces draw-ready records in the render frame -
// position relative to a floating origin (the local player), and the orientation
// basis as a rotation matrix (rows side/roof/nose). The local player is the
// origin and is not drawn (you don't render your own cockpit ship).
//
// DirectXMath migration (docs/MathMigration.md): records now store XMFLOAT3 /
// XMFLOAT4X4 and the world->camera math runs in XMVECTOR / XMMATRIX. Storage is
// POD (no D3D, no game state), so the conversion stays headlessly unit-tested;
// the thin glue that pushes records through draw_ship() lives in the client.

#include <cmath>
#include <vector>

#include <DirectXMath.h>

#include "Vector3i64.h"
#include "Replication.h"

namespace Neuron::Client
{
  using namespace DirectX;

  // A single draw-ready entity in the render frame.
  struct RenderRecord
  {
    uint32_t id = 0;
    int type = 0;                            // replicated ship type (legacy SHIP_*)
    XMFLOAT3 location{ 0.0f, 0.0f, 0.0f };   // relative to the floating origin
    XMFLOAT4X4 rotmat{ 1.0f, 0.0f, 0.0f, 0.0f,
                       0.0f, 1.0f, 0.0f, 0.0f,
                       0.0f, 0.0f, 1.0f, 0.0f,
                       0.0f, 0.0f, 0.0f, 1.0f };   // rows [0]=side, [1]=roof, [2]=nose
    double distance = 0.0;
  };

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

    // Local ship basis (rows side/roof/nose). The world->camera transform rebuilds
    // an offset FROM this basis (x*side + y*roof + z*nose), i.e.
    // XMVector3TransformNormal with B's rows = side/roof/nose. That is the transpose
    // of a textbook view matrix, but it is the convention the legacy
    // move_local_object() / starfield use, so replicated objects rotate WITH the
    // stars (docs/MathMigration.md §4). At the identity orientation the two agree.
    const XMVECTOR pNose = XMVectorSet(me->noseX, me->noseY, me->noseZ, 0.0f);
    const XMVECTOR pRoof = XMVectorSet(me->roofX, me->roofY, me->roofZ, 0.0f);
    const XMVECTOR pSide = XMVector3Cross(pRoof, pNose);
    const XMMATRIX viewBasis(pSide, pRoof, pNose, g_XMIdentityR3);

    records.reserve(_entities.size());
    for (const Net::EntitySnapshot& e : _entities)
    {
      if (e.id == _localPlayerId)
        continue;

      const double wx = static_cast<double>(e.x - origin.x);
      const double wy = static_cast<double>(e.y - origin.y);
      const double wz = static_cast<double>(e.z - origin.z);

      const XMVECTOR offset = XMVectorSet(
          static_cast<float>(wx), static_cast<float>(wy), static_cast<float>(wz), 0.0f);

      RenderRecord r;
      r.id = e.id;
      r.type = e.type;
      XMStoreFloat3(&r.location, XMVector3TransformNormal(offset, viewBasis));
      r.distance = std::sqrt(wx * wx + wy * wy + wz * wz);

      // The entity's own orientation, also expressed in the camera frame: its basis
      // (rows eSide/eRoof/eNose) carried through the same transform, i.e. E * viewBasis.
      const XMVECTOR eNose = XMVectorSet(e.noseX, e.noseY, e.noseZ, 0.0f);
      const XMVECTOR eRoof = XMVectorSet(e.roofX, e.roofY, e.roofZ, 0.0f);
      const XMVECTOR eSide = XMVector3Cross(eRoof, eNose);
      const XMMATRIX entityBasis(eSide, eRoof, eNose, g_XMIdentityR3);
      XMStoreFloat4x4(&r.rotmat, XMMatrixMultiply(entityBasis, viewBasis));

      records.push_back(r);
    }

    return records;
  }
}
