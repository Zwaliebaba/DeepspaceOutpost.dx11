#pragma once

// StationServices - authoritative docking & trading (GameLogic, server-side).
//
// The server owns the player's wallet and cargo, and the station's market. These
// are the rules behind the docked request/response protocol: a faithful port of
// the legacy buy_stock/sell_stock/total_cargo, generalized from one-unit-at-a-time
// to a quantity, and validated so a client can never conjure credits or cargo.
// Pure (mutates only the structs passed in), so every rule is unit-tested headless.

#include <algorithm>
#include <cstdint>
#include <string_view>
#include <vector>

#include "ECS.h"
#include "Vector3i64.h"
#include "StationProtocol.h"   // Net::StationStatus / StationRequest / StationResponse

#include "SimComponents.h"     // WorldTransform
#include "Economy.h"           // COMMODITY_COUNT, MarketEntry
#include "CombatSystem.h"      // Wanted, FUGITIVE_THRESHOLD (fugitives are refused docking)

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
    bool miningLaser = false;   // scene.md 3.7: gates the Mine order's beam cycle
  };

  // Hyperspace fuel (G7): legacy 0..70 tenths = 0.0..7.0 light years. Full at
  // spawn; spent by a jump in proportion to distance (HyperspaceSystem), refilled
  // at a station (Refuel) or by sun-skimming later. `max` grows with no upgrade
  // today but is carried so a future long-range drive can raise the ceiling.
  inline constexpr int MAX_FUEL_TENTHS = 70;

  // Cost to buy one tenth of a light year of fuel (legacy units: tenths of a
  // credit). A full 7.0 LY tank costs 14.0 Cr.
  inline constexpr int FUEL_PRICE_PER_TENTH = 2;

  struct Fuel
  {
    int tenths = MAX_FUEL_TENTHS;
    int max = MAX_FUEL_TENTHS;
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
      case Net::EquipItem::MiningLaser:    return 2000;    // 200.0 Cr
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
      case Net::EquipItem::MiningLaser:    return _eq.miningLaser;
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
      case Net::EquipItem::MiningLaser:    _eq.miningLaser = true; break;
      default: break;
    }

    r.status = Net::StationStatus::Ok;
    r.credits = _wallet.credits;
    return r;
  }

  // F1: the escort is a steep purchase (5000.0 Cr, tenths). A unit, not a fitted
  // upgrade, so it has its own price + buy path rather than EquipPrice/EquipPlayer.
  inline constexpr int ESCORT_FIGHTER_PRICE = 50000;

  // Validate an escort purchase and charge for it (server-authoritative, pure). The
  // caller spawns the escort + grants ownership on Ok; this only gates docking, the
  // per-player escort cap, and credits, and deducts on success. `_currentEscorts` is
  // how many the player already owns; `_maxEscorts` the cap (bounds entity growth,
  // roadmap #20). AlreadyOwned doubles as "at the escort limit".
  [[nodiscard]] inline EquipResult BuyEscort(Wallet& _wallet, const DockState& _dock,
                                             int _currentEscorts, int _maxEscorts)
  {
    EquipResult r;
    r.credits = _wallet.credits;
    if (!_dock.docked)
    {
      r.status = Net::StationStatus::NotDocked;
      return r;
    }
    if (_currentEscorts >= _maxEscorts)
    {
      r.status = Net::StationStatus::AlreadyOwned;   // at the escort limit
      return r;
    }
    if (_wallet.credits < ESCORT_FIGHTER_PRICE)
    {
      r.status = Net::StationStatus::NotEnoughCredits;
      return r;
    }
    _wallet.credits -= ESCORT_FIGHTER_PRICE;
    r.status = Net::StationStatus::Ok;
    r.credits = _wallet.credits;
    return r;
  }

  // Buy hyperspace fuel (legacy buy_fuel). Fills the tank as far as the wallet
  // allows, up to full, and charges for what was actually pumped. Docked-only.
  // Returns Ok on any purchase (or an already-full tank); NotEnoughCredits only
  // when the player can't afford even a single tenth of a partial tank.
  [[nodiscard]] inline Net::StationStatus RefuelPlayer(Wallet& _wallet, Fuel& _fuel, bool _docked)
  {
    if (!_docked)
      return Net::StationStatus::NotDocked;

    const int need = _fuel.max - _fuel.tenths;
    if (need <= 0)
      return Net::StationStatus::Ok;   // already full - a no-op, not an error

    const int affordable = _wallet.credits / FUEL_PRICE_PER_TENTH;
    const int buy = (need < affordable) ? need : affordable;
    if (buy <= 0)
      return Net::StationStatus::NotEnoughCredits;

    _wallet.credits -= buy * FUEL_PRICE_PER_TENTH;
    _fuel.tenths += buy;
    return Net::StationStatus::Ok;
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

  // Respawn a dead player DOCKED at the nearest station anywhere in the world
  // (G3 death rule): relocate them onto the station, mark them docked, and empty
  // their hold. The cargo is lost for now - it will scatter as scoopable canisters
  // once loot entities exist (G4). Returns false (leaving the player in place) only
  // if the player lacks a transform/dock or no station exists.
  inline bool RespawnAtNearestStation(ECS::Registry& _world, ECS::EntityId _player)
  {
    WorldTransform* pt = _world.TryGet<WorldTransform>(_player);
    DockState* dock = _world.TryGet<DockState>(_player);
    if (pt == nullptr || dock == nullptr)
      return false;

    // Unbounded nearest: a range wider than the galaxy so the Chebyshev gate never
    // culls, giving the globally nearest station.
    const ECS::EntityId station = NearestStation(_world, pt->position, INT64_MAX / 4);
    const WorldTransform* st = (station.index != ECS::INVALID_INDEX) ? _world.TryGet<WorldTransform>(station) : nullptr;
    if (st == nullptr)
      return false;

    pt->position = st->position;
    dock->docked = true;
    dock->stationId = station.index;

    // Drop cargo: empty the hold but keep its capacity (the large bay survives death).
    if (CargoHold* hold = _world.TryGet<CargoHold>(_player))
      for (int& units : hold->units)
        units = 0;

    return true;
  }

  // Wake a loaded commander (B4 persistence) docked at the station of `_systemId`
  // (or the nearest station if `_systemId` < 0 or that system has none), WITHOUT
  // touching cargo - unlike RespawnAtNearestStation, a load keeps the held goods.
  // Returns false (leaving the player in place) if it lacks a transform/dock or no
  // station exists.
  inline bool DockAtSystemOrNearest(ECS::Registry& _world, ECS::EntityId _player, int _systemId)
  {
    WorldTransform* pt = _world.TryGet<WorldTransform>(_player);
    DockState* dock = _world.TryGet<DockState>(_player);
    if (pt == nullptr || dock == nullptr)
      return false;

    ECS::EntityId station = (_systemId >= 0) ? FindStationBySystem(_world, _systemId) : ECS::EntityId{};
    if (station.index == ECS::INVALID_INDEX)
      station = NearestStation(_world, pt->position, INT64_MAX / 4);
    const WorldTransform* st = (station.index != ECS::INVALID_INDEX) ? _world.TryGet<WorldTransform>(station) : nullptr;
    if (st == nullptr)
      return false;

    pt->position = st->position;
    dock->docked = true;
    dock->stationId = station.index;
    return true;
  }

  // Choose a spawn system for a fresh commander by hashing their NAME (SplitMix64),
  // then dock them at that system's station. Deterministic and varied per player -
  // no wall-clock RNG (§8: every draw comes from a seeded source), so the same name
  // always starts in the same place and the choice is reproducible in tests. The
  // player's persisted lastSystemId is then captured from where they wake, so a
  // returning commander reappears there. No-op (returns false) when the world has
  // no stations. Used at ACCOUNT CREATION only; returning players wake at their
  // saved system instead.
  inline bool DockAtNameChosenSystem(ECS::Registry& _world, ECS::EntityId _player, std::string_view _name)
  {
    // Collect station system ids in a stable (sorted) order so the pick does not
    // depend on entity-creation order.
    std::vector<int> ids;
    _world.Each<ServerStation>([&](ECS::EntityId, ServerStation& _st) { ids.push_back(_st.systemId); });
    if (ids.empty())
      return false;
    std::sort(ids.begin(), ids.end());

    uint64_t h = 0x9E3779B97F4A7C15ull;
    for (unsigned char c : _name)                 // fold the name in
    {
      h ^= c;
      h *= 0x100000001B3ull;
    }
    h += 0x9E3779B97F4A7C15ull;                   // SplitMix64 finalizer for good spread
    h = (h ^ (h >> 30)) * 0xBF58476D1CE4E5B9ull;
    h = (h ^ (h >> 27)) * 0x94D049BB133111EBull;
    h ^= h >> 31;

    const int systemId = ids[static_cast<std::size_t>(h % ids.size())];
    return DockAtSystemOrNearest(_world, _player, systemId);
  }

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
        // Fugitives are turned away: cool your wanted level down (it decays over
        // time, and dies with you) before a station will let you dock again.
        const Wanted* wnt = _world.TryGet<Wanted>(_player);
        if (wnt != nullptr && wnt->level >= FUGITIVE_THRESHOLD)
        {
          resp.status = Net::StationStatus::DockingRefused;
          break;
        }
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
        // Launch: place the hull at the station bay facing outward, then let it
        // fly itself clear under a gentle cruise (StepLaunchCruise) instead of
        // teleporting. The ship starts inside the station contact range, so grant
        // launch immunity (invulnTicks) for the duration of the slow exit - this
        // both stops the station hull grinding it and protects the vulnerable
        // low-speed launch. The LaunchCruise component drops off (and control
        // returns to the player) once it has travelled LAUNCH_OFFSET units out.
        const ECS::EntityId stn{ dock->stationId, 0 };
        WorldTransform* pt = _world.TryGet<WorldTransform>(_player);
        const WorldTransform* st = _world.TryGet<WorldTransform>(stn);
        if (pt != nullptr && st != nullptr)
        {
          pt->position = st->position;
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
          // Drive the outward fly-out (idempotent: re-launch just refreshes it).
          _world.Remove<LaunchCruise>(_player);
          _world.Add<LaunchCruise>(_player, LaunchCruise{ st->position, LAUNCH_OFFSET, 0.35 });
          if (Combatant* c = _world.TryGet<Combatant>(_player))
            c->invulnTicks = RESPAWN_GRACE_TICKS;   // covers the ~95-tick bay exit
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

      case Net::StationRequestKind::Refuel:
      {
        Fuel* fuel = _world.TryGet<Fuel>(_player);
        if (fuel == nullptr)
        {
          resp.status = Net::StationStatus::BadCommodity;   // not fuel-capable
          break;
        }
        resp.status = RefuelPlayer(*wallet, *fuel, dock->docked);
        resp.credits = wallet->credits;   // the fuel level itself rides PlayerStatus
        break;
      }

      default:
        // Travel commands (Teleport - the fuel-gated hyperspace jump - and
        // JumpDrive) are intercepted by the server loop and routed through
        // HyperspaceSystem before this station-service dispatch, so they never
        // arrive here. Any other/unknown kind is not a station service.
        resp.status = Net::StationStatus::BadCommodity;
        break;
    }

    return resp;
  }
}
