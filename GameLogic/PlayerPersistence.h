#pragma once

// PlayerPersistence - convert a player entity's durable components to/from the
// flat persistence snapshot (GameLogic).
//
// The bridge between the simulation's ECS components and NeuronServer's
// dependency-free PlayerPersistState. It lives HERE (not in NeuronServer) so the
// persistence infrastructure never has to know the game's components; only the
// game knows how to serialize itself. GameServer calls FromComponents at save
// cadence and ApplyToComponents after a load completes.

#include <cstdint>

#include "ECS.h"
#include "Economy.h"            // COMMODITY_COUNT
#include "StationServices.h"    // Wallet, CargoHold, DockState, Equipment, Fuel, ServerStation, FindStationBySystem
#include "CombatSystem.h"       // Wanted, PlayerRecord, Witchspace

#include "PlayerPersistState.h" // Neuron::Persist::PlayerPersistState (NeuronServer, header-only)

namespace Neuron::GameLogic
{
  static_assert(static_cast<std::size_t>(COMMODITY_COUNT) == Persist::PERSIST_COMMODITY_COUNT,
                "persistence cargo width must match the game's COMMODITY_COUNT");

  // Pack a player entity's durable components into a flat snapshot, stamped with
  // the world tick. `lastSystemId` is the system of the station the player is
  // docked at (so a reconnect wakes there); -1 (home) when not docked, since §12
  // forbids persisting the free-flight position.
  [[nodiscard]] inline Persist::PlayerPersistState PlayerStateFromComponents(
      ECS::Registry& _world, ECS::EntityId _entity, uint64_t _worldTick)
  {
    Persist::PlayerPersistState s;
    s.updatedTick = _worldTick;

    if (const PlayerRecord* pr = _world.TryGet<PlayerRecord>(_entity))
    {
      s.commanderName = pr->name;
      s.score = pr->score;
    }
    if (const Wallet* w = _world.TryGet<Wallet>(_entity))
      s.credits = w->credits;
    if (const Fuel* f = _world.TryGet<Fuel>(_entity))
      s.fuelTenths = f->tenths;
    if (const Wanted* wn = _world.TryGet<Wanted>(_entity))
      s.wantedLevel = wn->level;
    if (const CargoHold* h = _world.TryGet<CargoHold>(_entity))
    {
      s.holdCapacity = h->capacity;
      for (int i = 0; i < COMMODITY_COUNT; ++i)
        s.cargo[static_cast<std::size_t>(i)] = h->units[i];
    }
    if (const Equipment* e = _world.TryGet<Equipment>(_entity))
    {
      s.missiles = e->missiles;
      s.equipFlags = (e->largeCargoBay ? Persist::EQUIP_LARGE_CARGO_BAY : 0u)
                   | (e->ecm          ? Persist::EQUIP_ECM              : 0u)
                   | (e->fuelScoop    ? Persist::EQUIP_FUEL_SCOOP       : 0u)
                   | (e->energyBomb   ? Persist::EQUIP_ENERGY_BOMB      : 0u)
                   | (e->escapePod    ? Persist::EQUIP_ESCAPE_POD       : 0u);
    }
    s.inWitchspace = _world.Has<Witchspace>(_entity);

    // Wake-docked system: only known when docked (station entity -> its systemId).
    s.lastSystemId = -1;
    if (const DockState* d = _world.TryGet<DockState>(_entity); d != nullptr && d->docked)
      if (const ServerStation* st = _world.TryGet<ServerStation>(ECS::EntityId{ d->stationId, 0 }))
        s.lastSystemId = st->systemId;

    return s;
  }

  // Restore the durable component VALUES onto a freshly-spawned player entity. Does
  // NOT touch position/dock/health - the caller docks the player at `lastSystemId`
  // and the fresh spawn provides the rest. The commander name is owned by the
  // account/session, so it is NOT overwritten here.
  inline void PlayerStateApplyToComponents(ECS::Registry& _world, ECS::EntityId _entity,
                                           const Persist::PlayerPersistState& _s)
  {
    if (Wallet* w = _world.TryGet<Wallet>(_entity))
      w->credits = _s.credits;
    if (Fuel* f = _world.TryGet<Fuel>(_entity))
      f->tenths = _s.fuelTenths;
    if (Wanted* wn = _world.TryGet<Wanted>(_entity))
      wn->level = _s.wantedLevel;
    if (PlayerRecord* pr = _world.TryGet<PlayerRecord>(_entity))
      pr->score = _s.score;
    if (CargoHold* h = _world.TryGet<CargoHold>(_entity))
    {
      h->capacity = _s.holdCapacity;
      for (int i = 0; i < COMMODITY_COUNT; ++i)
        h->units[i] = _s.cargo[static_cast<std::size_t>(i)];
    }
    if (Equipment* e = _world.TryGet<Equipment>(_entity))
    {
      e->missiles = _s.missiles;
      e->largeCargoBay = (_s.equipFlags & Persist::EQUIP_LARGE_CARGO_BAY) != 0;
      e->ecm           = (_s.equipFlags & Persist::EQUIP_ECM) != 0;
      e->fuelScoop     = (_s.equipFlags & Persist::EQUIP_FUEL_SCOOP) != 0;
      e->energyBomb    = (_s.equipFlags & Persist::EQUIP_ENERGY_BOMB) != 0;
      e->escapePod     = (_s.equipFlags & Persist::EQUIP_ESCAPE_POD) != 0;
    }

    // Witchspace is a tag: add/remove to match the snapshot.
    if (_s.inWitchspace && !_world.Has<Witchspace>(_entity))
      _world.Add<Witchspace>(_entity, Witchspace{});
    else if (!_s.inWitchspace && _world.Has<Witchspace>(_entity))
      _world.Remove<Witchspace>(_entity);
  }
}
