#pragma once

// StationServices - authoritative docking & trading (GameLogic, server-side).
//
// The server owns the player's wallet and cargo, and the station's market. These
// are the rules behind the docked request/response protocol: a faithful port of
// the legacy buy_stock/sell_stock/total_cargo, generalized from one-unit-at-a-time
// to a quantity, and validated so a client can never conjure credits or cargo.
// Pure (mutates only the structs passed in), so every rule is unit-tested headless.

#include <cstdint>

#include "ECS.h"
#include "Vector3i64.h"
#include "StationProtocol.h"   // Net::StationStatus / StationRequest / StationResponse

#include "SimComponents.h"     // WorldTransform
#include "Economy.h"           // COMMODITY_COUNT, MarketEntry

namespace Neuron::GameLogic
{
  // The player's authoritative commerce state.
  struct Wallet
  {
    int credits = 1000;        // tenths of a credit (legacy units)
  };

  struct CargoHold
  {
    int units[COMMODITY_COUNT] = {};
    int capacity = 20;         // hold size in tonnes
  };

  struct DockState
  {
    bool docked = false;
    uint32_t stationId = 0;
  };

  // The player's owned equipment.
  struct Equipment
  {
    int missiles = 3;
    bool largeCargoBay = false;
    bool ecm = false;
    bool fuelScoop = false;
    bool energyBomb = false;
    bool escapePod = false;
  };

  // A station's authoritative market, stored on the station entity so every
  // player docked there trades against the same shared stock. `systemId` links
  // it back to its galaxy system.
  struct ServerStation
  {
    int systemId = 0;
    MarketEntry market[COMMODITY_COUNT] = {};
  };

  // Only commodities 0..12 are measured in tonnes and count against hold capacity
  // (Gold/Platinum/Gem-Stones/Alien Items are kg/g/special - legacy units != TONNES).
  [[nodiscard]] inline bool CountsAsTonnage(int _commodity)
  {
    return _commodity >= 0 && _commodity <= 12;
  }

  [[nodiscard]] inline int TotalTonnage(const CargoHold& _hold)
  {
    int tonnes = 0;
    for (int i = 0; i < COMMODITY_COUNT; ++i)
      if (CountsAsTonnage(i))
        tonnes += _hold.units[i];
    return tonnes;
  }

  struct TradeResult
  {
    Net::StationStatus status = Net::StationStatus::Ok;
    int credits = 0;           // resulting wallet
    int cargo = 0;             // resulting held quantity of the commodity
  };

  // Buy `_qty` units of `_commodity` (legacy buy_stock, per-quantity). Validates
  // docked state, stock, credits, and (for tonnage goods) hold space; on success
  // moves credits/cargo/stock atomically.
  [[nodiscard]] inline TradeResult BuyCommodity(Wallet& _wallet, CargoHold& _hold,
      MarketEntry* _market, bool _docked, int _commodity, int _qty)
  {
    TradeResult r;
    r.credits = _wallet.credits;

    if (!_docked)
    {
      r.status = Net::StationStatus::NotDocked;
      return r;
    }
    if (_commodity < 0 || _commodity >= COMMODITY_COUNT || _qty <= 0)
    {
      r.status = Net::StationStatus::BadCommodity;
      return r;
    }

    r.cargo = _hold.units[_commodity];

    if (_market[_commodity].quantity < _qty)
    {
      r.status = Net::StationStatus::NoStock;
      return r;
    }

    const int cost = _market[_commodity].price * _qty;
    if (_wallet.credits < cost)
    {
      r.status = Net::StationStatus::NotEnoughCredits;
      return r;
    }

    if (CountsAsTonnage(_commodity) && TotalTonnage(_hold) + _qty > _hold.capacity)
    {
      r.status = Net::StationStatus::HoldFull;
      return r;
    }

    _wallet.credits -= cost;
    _hold.units[_commodity] += _qty;
    _market[_commodity].quantity -= _qty;

    r.status = Net::StationStatus::Ok;
    r.credits = _wallet.credits;
    r.cargo = _hold.units[_commodity];
    return r;
  }

  // Sell `_qty` units of `_commodity` (legacy sell_stock, per-quantity).
  [[nodiscard]] inline TradeResult SellCommodity(Wallet& _wallet, CargoHold& _hold,
      MarketEntry* _market, bool _docked, int _commodity, int _qty)
  {
    TradeResult r;
    r.credits = _wallet.credits;

    if (!_docked)
    {
      r.status = Net::StationStatus::NotDocked;
      return r;
    }
    if (_commodity < 0 || _commodity >= COMMODITY_COUNT || _qty <= 0)
    {
      r.status = Net::StationStatus::BadCommodity;
      return r;
    }

    r.cargo = _hold.units[_commodity];

    if (_hold.units[_commodity] < _qty)
    {
      r.status = Net::StationStatus::NoCargo;
      return r;
    }

    _wallet.credits += _market[_commodity].price * _qty;
    _hold.units[_commodity] -= _qty;
    _market[_commodity].quantity += _qty;

    r.status = Net::StationStatus::Ok;
    r.credits = _wallet.credits;
    r.cargo = _hold.units[_commodity];
    return r;
  }

  // Catalog price for an equipment item (legacy units: tenths of a credit).
  [[nodiscard]] inline int EquipPrice(Net::EquipItem _item)
  {
    switch (_item)
    {
      case Net::EquipItem::Missile:       return 300;     // 30.0 Cr
      case Net::EquipItem::LargeCargoBay:  return 4000;
      case Net::EquipItem::Ecm:            return 6000;
      case Net::EquipItem::FuelScoop:      return 5250;
      case Net::EquipItem::EnergyBomb:     return 9000;
      case Net::EquipItem::EscapePod:      return 10000;
      default:                             return 0;      // unknown item
    }
  }

  // Whether the player already has the item (so a one-shot purchase is rejected;
  // missiles cap at 4).
  [[nodiscard]] inline bool AlreadyHas(const Equipment& _eq, Net::EquipItem _item)
  {
    switch (_item)
    {
      case Net::EquipItem::Missile:        return _eq.missiles >= 4;
      case Net::EquipItem::LargeCargoBay:  return _eq.largeCargoBay;
      case Net::EquipItem::Ecm:            return _eq.ecm;
      case Net::EquipItem::FuelScoop:      return _eq.fuelScoop;
      case Net::EquipItem::EnergyBomb:     return _eq.energyBomb;
      case Net::EquipItem::EscapePod:      return _eq.escapePod;
      default:                             return false;
    }
  }

  struct EquipResult
  {
    Net::StationStatus status = Net::StationStatus::Ok;
    int credits = 0;
  };

  // Buy one equipment item (server-authoritative). Validates the catalog,
  // duplicate ownership and credits, then grants it - the large cargo bay also
  // enlarges the hold.
  [[nodiscard]] inline EquipResult EquipPlayer(Wallet& _wallet, Equipment& _eq, CargoHold& _hold, Net::EquipItem _item)
  {
    EquipResult r;
    r.credits = _wallet.credits;

    const int price = EquipPrice(_item);
    if (price <= 0)
    {
      r.status = Net::StationStatus::BadCommodity;
      return r;
    }
    if (AlreadyHas(_eq, _item))
    {
      r.status = Net::StationStatus::AlreadyOwned;
      return r;
    }
    if (_wallet.credits < price)
    {
      r.status = Net::StationStatus::NotEnoughCredits;
      return r;
    }

    _wallet.credits -= price;
    switch (_item)
    {
      case Net::EquipItem::Missile:        _eq.missiles++; break;
      case Net::EquipItem::LargeCargoBay:  _eq.largeCargoBay = true; _hold.capacity += 15; break;
      case Net::EquipItem::Ecm:            _eq.ecm = true; break;
      case Net::EquipItem::FuelScoop:      _eq.fuelScoop = true; break;
      case Net::EquipItem::EnergyBomb:     _eq.energyBomb = true; break;
      case Net::EquipItem::EscapePod:      _eq.escapePod = true; break;
      default: break;
    }

    r.status = Net::StationStatus::Ok;
    r.credits = _wallet.credits;
    return r;
  }

  // Can the player dock? Proximity check to the station (Chebyshev, overflow-safe
  // on absolute coordinates).
  [[nodiscard]] inline bool CanDock(const Math::Vector3i64& _player, const Math::Vector3i64& _station, int64_t _range)
  {
    const int64_t dx = _player.x - _station.x;
    const int64_t dy = _player.y - _station.y;
    const int64_t dz = _player.z - _station.z;
    const int64_t ax = dx < 0 ? -dx : dx;
    const int64_t ay = dy < 0 ? -dy : dy;
    const int64_t az = dz < 0 ? -dz : dz;
    return ax <= _range && ay <= _range && az <= _range;
  }

  // Find the nearest station entity to `_pos` within `_range` (Chebyshev gate,
  // nearest by Manhattan distance). Returns an invalid id when none is in range.
  [[nodiscard]] inline ECS::EntityId NearestStation(ECS::Registry& _world, const Math::Vector3i64& _pos, int64_t _range)
  {
    ECS::EntityId best;
    bool found = false;
    int64_t bestDist = 0;
    _world.Each<ServerStation, WorldTransform>([&](ECS::EntityId _id, ServerStation&, WorldTransform& _t)
    {
      const int64_t dx = _t.position.x - _pos.x;
      const int64_t dy = _t.position.y - _pos.y;
      const int64_t dz = _t.position.z - _pos.z;
      const int64_t ax = dx < 0 ? -dx : dx;
      const int64_t ay = dy < 0 ? -dy : dy;
      const int64_t az = dz < 0 ? -dz : dz;
      if (ax > _range || ay > _range || az > _range)
        return;
      const int64_t d = ax + ay + az;
      if (!found || d < bestDist)
      {
        found = true;
        bestDist = d;
        best = _id;
      }
    });
    return found ? best : ECS::EntityId{};
  }

  // Find the station entity belonging to galaxy system `_systemId` (invalid id if
  // none). Used by teleport to locate the destination.
  [[nodiscard]] inline ECS::EntityId FindStationBySystem(ECS::Registry& _world, int _systemId)
  {
    ECS::EntityId result;
    bool found = false;
    _world.Each<ServerStation, WorldTransform>([&](ECS::EntityId _id, ServerStation& _st, WorldTransform&)
    {
      if (!found && _st.systemId == _systemId)
      {
        found = true;
        result = _id;
      }
    });
    return found ? result : ECS::EntityId{};
  }

  // How far in front of a station a launching player is ejected (world units).
  // Comfortably outside both the server dock range and the client dock trigger so
  // a fresh launch never instantly re-docks.
  constexpr int64_t LAUNCH_OFFSET = 2000;

  // Apply a station request to `_player`'s authoritative components and the market
  // of the station they are docked at, returning the response to send back. Dock
  // attaches to the nearest in-range station; trades hit THAT station's market.
  // Pure apart from the world/market it mutates, so it is unit-tested directly.
  [[nodiscard]] inline Net::StationResponse ProcessStationRequest(
      ECS::Registry& _world, ECS::EntityId _player, int64_t _dockRange, const Net::StationRequest& _req)
  {
    Net::StationResponse resp;
    resp.kind = _req.kind;
    resp.commodity = _req.commodity;

    Wallet* wallet = _world.TryGet<Wallet>(_player);
    CargoHold* hold = _world.TryGet<CargoHold>(_player);
    DockState* dock = _world.TryGet<DockState>(_player);
    if (wallet == nullptr || hold == nullptr || dock == nullptr)
    {
      resp.status = Net::StationStatus::BadCommodity;   // player not trade-capable
      return resp;
    }

    resp.credits = wallet->credits;
    resp.cargo = static_cast<uint16_t>(hold->units[_req.commodity < COMMODITY_COUNT ? _req.commodity : 0]);

    switch (_req.kind)
    {
      case Net::StationRequestKind::Dock:
      {
        const WorldTransform* t = _world.TryGet<WorldTransform>(_player);
        const ECS::EntityId station = (t != nullptr)
          ? NearestStation(_world, t->position, _dockRange) : ECS::EntityId{};
        if (station.index != ECS::INVALID_INDEX)
        {
          dock->docked = true;
          dock->stationId = station.index;
          resp.status = Net::StationStatus::Ok;
        }
        else
        {
          resp.status = Net::StationStatus::CantDock;
        }
        break;
      }

      case Net::StationRequestKind::Undock:
      {
        // Eject the player clear of the station and facing outward, so flying
        // forward leaves rather than immediately re-docking. Launch a fixed
        // distance along +z (well outside both the server dock range and the
        // client's dock trigger) with a clean, level basis.
        const ECS::EntityId stn{ dock->stationId, 0 };
        WorldTransform* pt = _world.TryGet<WorldTransform>(_player);
        const WorldTransform* st = _world.TryGet<WorldTransform>(stn);
        if (pt != nullptr && st != nullptr)
        {
          pt->position = st->position;
          pt->position += Math::Vector3i64{ 0, 0, LAUNCH_OFFSET };
          if (Flight* f = _world.TryGet<Flight>(_player))
          {
            f->side = Math::Vector3d{ 1.0, 0.0, 0.0 };
            f->roof = Math::Vector3d{ 0.0, 1.0, 0.0 };
            f->nose = Math::Vector3d{ 0.0, 0.0, 1.0 };
            f->roll = 0.0;
            f->pitch = 0.0;
            f->speed = 0.0;
            f->carry = Math::Vector3d{ 0.0, 0.0, 0.0 };
          }
        }
        dock->docked = false;
        resp.status = Net::StationStatus::Ok;
        break;
      }

      case Net::StationRequestKind::Buy:
      case Net::StationRequestKind::Sell:
      {
        if (!dock->docked)
        {
          resp.status = Net::StationStatus::NotDocked;
          break;
        }
        ServerStation* st = _world.TryGet<ServerStation>(ECS::EntityId{ dock->stationId, 0 });
        if (st == nullptr)
        {
          resp.status = Net::StationStatus::CantDock;   // docked station no longer exists
          break;
        }
        const TradeResult tr = (_req.kind == Net::StationRequestKind::Buy)
          ? BuyCommodity(*wallet, *hold, st->market, true, _req.commodity, _req.quantity)
          : SellCommodity(*wallet, *hold, st->market, true, _req.commodity, _req.quantity);
        resp.status = tr.status;
        resp.credits = tr.credits;
        resp.cargo = static_cast<uint16_t>(tr.cargo);
        break;
      }

      case Net::StationRequestKind::Equip:
      {
        Equipment* eq = _world.TryGet<Equipment>(_player);
        if (eq == nullptr || !dock->docked)
        {
          resp.status = eq == nullptr ? Net::StationStatus::BadCommodity : Net::StationStatus::NotDocked;
          break;
        }
        const EquipResult er = EquipPlayer(*wallet, *eq, *hold, static_cast<Net::EquipItem>(_req.commodity));
        resp.status = er.status;
        resp.credits = er.credits;
        break;
      }

      case Net::StationRequestKind::Teleport:
      {
        // Jump from this station to the destination system's station (the
        // "teleport building" - only available while docked). Arrive docked at
        // the destination.
        if (!dock->docked)
        {
          resp.status = Net::StationStatus::NotDocked;
          break;
        }
        const ECS::EntityId dest = FindStationBySystem(_world, static_cast<int>(_req.stationId));
        WorldTransform* pt = _world.TryGet<WorldTransform>(_player);
        WorldTransform* dt = (dest.index != ECS::INVALID_INDEX) ? _world.TryGet<WorldTransform>(dest) : nullptr;
        if (pt != nullptr && dt != nullptr)
        {
          pt->position = dt->position;
          dock->docked = true;
          dock->stationId = dest.index;
          resp.status = Net::StationStatus::Ok;
        }
        else
        {
          resp.status = Net::StationStatus::CantDock;   // unknown destination
        }
        break;
      }
    }

    return resp;
  }
}
