#pragma once

// EscortSpawn - build a player-owned escort NPC (GameLogic, F1 / roadmap #12).
//
// The first ORDERED unit other than the player's own ship. An escort is a Viper-
// hulled NPC that flies by the same intent pipeline as everything else, but:
//   * it carries an Owner (stamped by the caller via GrantOwnership, so it reaps
//     with the session and counts in "all my units"),
//   * it is on Team::Player and auto-engages, so it defends its owner with the NPC
//     fire discipline (StepCombat), gated to legitimate hostiles (pirates + wanted)
//     by the same PoliceMayEngage rule the police use, and
//   * it is driven by an ActiveOrder (StepOrders), NOT an AiPilot - so it follows
//     the owner (default Escort order) or executes the owner's Move/Attack/Approach
//     orders. No AiPilot means StepAi leaves it to StepOrders; no new steering.
//
// Pure GameLogic (creates an entity + components), so the loadout is unit-tested
// headlessly. The server wraps this with the purchase gate (BuyEscort) and
// ownership grant (GameServer::HandleBuyEscort).

#include "ECS.h"
#include "Vector3i64.h"

#include "SimComponents.h"     // WorldTransform, NetType, ShipType
#include "FlightInput.h"       // Flight, FlightIntent, FlightCaps
#include "CombatSystem.h"      // Combatant, Team
#include "EquipmentSystem.h"   // ShipGear (laser temperature)
#include "AiSystem.h"          // NpcFlightCaps
#include "OrderSystem.h"       // ActiveOrder
#include "Messages/Defs/UnitOrder.h"   // Msg::OrderKind

namespace Neuron::GameLogic
{
  // Escort combat loadout: a shade tougher than a pirate Viper (energy 100, laser
  // 6) but well under a shielded player, and the player's 6000 laser reach.
  inline constexpr int     ESCORT_ENERGY = 100;
  inline constexpr int     ESCORT_LASER  = 6;
  inline constexpr int64_t ESCORT_RANGE  = 6000;

  // Spawn offset from the owner so a fresh escort doesn't appear inside the hull;
  // stepped per existing escort so a wing doesn't stack on one point.
  inline constexpr int64_t ESCORT_SPAWN_OFFSET = 900;

  // Create an escort NPC that follows `_owner` (its default ActiveOrder is
  // Escort -> owner). Does NOT stamp Owner / index it - the caller does that via
  // GrantOwnership so the ownership index and reap stay in one place. `_slot`
  // spreads multiple escorts around the owner. Returns the new entity.
  [[nodiscard]] inline ECS::EntityId SpawnEscort(ECS::Registry& _world, ECS::EntityId _owner, int _slot = 0)
  {
    const Math::Vector3i64 opos = _world.Get<WorldTransform>(_owner).position;
    const int64_t off = ESCORT_SPAWN_OFFSET * static_cast<int64_t>(_slot + 1);

    const ECS::EntityId e = _world.Create();
    _world.Add<WorldTransform>(e, WorldTransform{ { opos.x + off, opos.y, opos.z + ESCORT_SPAWN_OFFSET } });
    _world.Add<Flight>(e, Flight{});
    _world.Add<FlightIntent>(e, FlightIntent{});
    _world.Add<FlightCaps>(e, NpcFlightCaps());
    _world.Add<Combatant>(e, Combatant{ Team::Player, ESCORT_ENERGY, ESCORT_LASER, ESCORT_RANGE, /*autoEngage*/ true });
    _world.Add<ShipGear>(e, ShipGear{});
    _world.Add<NetType>(e, NetType{ ShipType::Viper });
    // Default order: escort the owner (a never-completing follow). The owner can
    // re-order it (Move/Attack/Approach/Stop) through the normal UnitOrder path.
    _world.Add<ActiveOrder>(e, ActiveOrder{ Msg::OrderKind::Escort, _owner.index, {}, false });
    return e;
  }
}
