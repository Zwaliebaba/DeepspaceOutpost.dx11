#pragma once

// OrderMenu - the pure, CI-testable logic behind Track I3's contextual orders
// and the RMB-hold / long-press radial menu (docs/interaction.md §3.3, §3.5).
//
// Given WHAT is under the pointer (an EntityClass plus a few semantic facts the
// client already knows - is it me, is it my unit, is it a clean player), this
// answers three questions the command UX needs:
//
//   * DefaultContextOrder() - the single order a plain RMB-click / tap issues.
//   * LegalOrders()         - every order the radial menu should list, plus Info.
//   * AttackRequiresMenu()  - the "clean player" friction: attacking a lawful
//                             player is never the click default; it exists only
//                             as a deliberate, menu-only choice (§3.3), and the
//                             crime is validated + attributed to the owner
//                             server-side either way.
//
// It carries NO rendering, NO input, NO DirectXMath - the client glue maps its
// snapshot type + roster/wanted join into these inputs and renders the returned
// options. Order kinds are the wire OrderKind (one source of truth).

#include <array>
#include <cstddef>

#include "Messages/Defs/UnitOrder.h"   // Neuron::Msg::OrderKind

namespace Neuron::Input
{
  using Neuron::Msg::OrderKind;

  // A coarse classification of a picked entity - what the client can tell from
  // an EntitySnapshot.type plus the roster join. Kept minimal and game-neutral;
  // the raw netType -> EntityClass switch stays in the client glue.
  enum class EntityClass : uint8_t
  {
    None,     // nothing under the pointer (free space)
    Ship,     // any hull (NPC, player, or own unit - refined by the flags)
    Station,  // a dockable station
    Planet,   // a planet body
    Sun,      // a star (approach to a standoff)
    Canister, // a cargo canister
  };

  // What the pointer resolves to for command purposes (§3.3 rows).
  enum class TargetKind : uint8_t
  {
    Empty,       // free space -> Move (via the gizmo)
    OwnShip,     // the commanded unit itself -> Stop
    OwnEscort,   // another unit you own (post-F1) -> Escort
    EnemyShip,   // an NPC, or a wanted player -> Attack
    CleanPlayer, // a lawful player -> Approach (Attack is menu-only)
    Station,     // -> Dock
    Planet,      // -> Approach (standoff outside the kill radius)
    Sun,         // -> Approach (standoff)
    Canister,    // -> Collect
  };

  // A single entry in the radial context menu. Info is not an order - it opens
  // the selection's info card - so it is flagged rather than shoe-horned into
  // OrderKind.
  struct MenuOption
  {
    bool        isInfo = false;
    OrderKind   order  = OrderKind::Stop;
    const char* label  = "";
  };

  // Resolve a picked entity into its command TargetKind from facts the client
  // already has. `isSelf` = this is the commanded unit; `isOwnUnit` = a
  // different unit you own (F1 escort); `isPlayer`/`isWantedPlayer` come from the
  // roster join (wanted level > 0). NPCs are neither isPlayer nor own.
  constexpr TargetKind ClassifyTarget(EntityClass _cls, bool _isSelf, bool _isOwnUnit,
                                      bool _isPlayer, bool _isWantedPlayer)
  {
    switch (_cls)
    {
    case EntityClass::None:     return TargetKind::Empty;
    case EntityClass::Station:  return TargetKind::Station;
    case EntityClass::Planet:   return TargetKind::Planet;
    case EntityClass::Sun:      return TargetKind::Sun;
    case EntityClass::Canister: return TargetKind::Canister;
    case EntityClass::Ship:
      if (_isSelf)    return TargetKind::OwnShip;
      if (_isOwnUnit) return TargetKind::OwnEscort;
      if (_isPlayer && !_isWantedPlayer) return TargetKind::CleanPlayer;
      return TargetKind::EnemyShip; // NPC or wanted player
    }
    return TargetKind::Empty;
  }

  // The default order a plain command verb (RMB-click / tap-with-selection)
  // issues - the §3.3 table. Clean players resolve to Approach on purpose.
  constexpr OrderKind DefaultContextOrder(TargetKind _t)
  {
    switch (_t)
    {
    case TargetKind::Empty:       return OrderKind::Move;
    case TargetKind::OwnShip:     return OrderKind::Stop;
    case TargetKind::OwnEscort:   return OrderKind::Escort;
    case TargetKind::EnemyShip:   return OrderKind::Attack;
    case TargetKind::CleanPlayer: return OrderKind::Approach;
    case TargetKind::Station:     return OrderKind::Dock;
    case TargetKind::Planet:      return OrderKind::Approach;
    case TargetKind::Sun:         return OrderKind::Approach;
    case TargetKind::Canister:    return OrderKind::Collect;
    }
    return OrderKind::Stop;
  }

  // Clean-player friction: Attack on a lawful player must never be the click
  // default; it is reachable only through the radial menu.
  constexpr bool AttackRequiresMenu(TargetKind _t)
  {
    return _t == TargetKind::CleanPlayer;
  }

  // The maximum number of options any target produces (keeps the API allocation-
  // free for the un-CI-testable render glue).
  inline constexpr std::size_t MAX_MENU_OPTIONS = 4;

  struct MenuOptions
  {
    std::array<MenuOption, MAX_MENU_OPTIONS> items{};
    std::size_t count = 0;

    void Add(bool _info, OrderKind _o, const char* _label)
    {
      if (count < MAX_MENU_OPTIONS) items[count++] = MenuOption{_info, _o, _label};
    }
  };

  // Every order the radial menu should list for a target, plus a trailing Info.
  // Free space has no menu (it is the move gizmo), so it returns just Move.
  inline MenuOptions LegalOrders(TargetKind _t)
  {
    MenuOptions m;
    switch (_t)
    {
    case TargetKind::Empty:
      m.Add(false, OrderKind::Move, "Move");
      return m; // no Info card for empty space
    case TargetKind::OwnShip:
      m.Add(false, OrderKind::Stop, "Stop");
      break;
    case TargetKind::OwnEscort:
      m.Add(false, OrderKind::Escort, "Escort");
      m.Add(false, OrderKind::Stop, "Stop");
      break;
    case TargetKind::EnemyShip:
      m.Add(false, OrderKind::Attack, "Attack");
      m.Add(false, OrderKind::Approach, "Approach");
      break;
    case TargetKind::CleanPlayer:
      m.Add(false, OrderKind::Approach, "Approach");
      m.Add(false, OrderKind::Attack, "Attack"); // menu-only (the friction)
      break;
    case TargetKind::Station:
      m.Add(false, OrderKind::Dock, "Dock");
      m.Add(false, OrderKind::Approach, "Approach");
      break;
    case TargetKind::Planet:
      m.Add(false, OrderKind::Approach, "Approach");
      break;
    case TargetKind::Sun:
      m.Add(false, OrderKind::Approach, "Approach");
      break;
    case TargetKind::Canister:
      m.Add(false, OrderKind::Collect, "Collect");
      m.Add(false, OrderKind::Approach, "Approach");
      break;
    }
    m.Add(true, OrderKind::Stop, "Info");
    return m;
  }
}
