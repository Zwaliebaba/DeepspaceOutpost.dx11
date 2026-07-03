#pragma once

// SnapshotBuilder - assemble a replication snapshot from the authoritative world.
//
// This is server-side glue (GameLogic), NOT shared behaviour: it reads the
// server's authoritative components (WorldTransform + the optional Flight
// orientation) and fills the flat Net::WorldSnapshot DTO the client understands.
// The client never runs this - it only reads the resulting snapshot via the
// shared schema. Pure and headless, so it is unit-tested directly.

#include "ECS.h"
#include "Replication.h"

#include "SimComponents.h"

namespace Neuron::GameLogic
{
  // Fill one entity's snapshot from its authoritative components. Orientation and
  // speed come from the Flight component when present; otherwise the entity is
  // reported at rest with the default facing (e.g. static props / simple movers).
  // Consumed by the per-viewer area-of-interest builder (AreaOfInterest.h).
  [[nodiscard]] inline Net::EntitySnapshot MakeEntitySnapshot(ECS::Registry& _world, ECS::EntityId _id, const WorldTransform& _t)
  {
    Net::EntitySnapshot e;
    e.id = _id.index;
    e.x = _t.position.x;
    e.y = _t.position.y;
    e.z = _t.position.z;

    if (const Flight* f = _world.TryGet<Flight>(_id))
    {
      e.noseX = static_cast<float>(f->nose.x);
      e.noseY = static_cast<float>(f->nose.y);
      e.noseZ = static_cast<float>(f->nose.z);
      e.roofX = static_cast<float>(f->roof.x);
      e.roofY = static_cast<float>(f->roof.y);
      e.roofZ = static_cast<float>(f->roof.z);
      e.speed = static_cast<float>(f->speed);
    }

    if (const NetType* nt = _world.TryGet<NetType>(_id))
      e.type = static_cast<int16_t>(nt->type);

    return e;
  }
}
