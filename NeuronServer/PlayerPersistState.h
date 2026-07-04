#pragma once

// PlayerPersistState - the flat, durable snapshot of one commander (NeuronServer).
//
// The currency of the persistence layer: a plain-data struct (no ECS/GameLogic
// types) that the store round-trips and the persistence service shuffles between
// the sim thread and the writer thread. Small and trivially copyable, so the
// server can snapshot every live player at cadence without cost. The GameLogic
// side owns the component<->struct converters (GameLogic/PlayerPersistence.h);
// this header stays dependency-free so the store and service never pull in the
// simulation.
//
// §12 invariant: this holds DURABLE state only (wallet, cargo, fuel, equipment,
// standing, and the system to wake docked at) - never per-tick positions.

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>

namespace Neuron::Persist
{
  // Bit assignments for PlayerPersistState.equipFlags. APPEND-ONLY: new equipment
  // takes the next free bit and existing bits never renumber (the same permanence
  // discipline as message ids), so an old row still decodes after new gear lands.
  enum PersistEquipFlags : uint32_t
  {
    EQUIP_LARGE_CARGO_BAY = 1u << 0,
    EQUIP_ECM             = 1u << 1,
    EQUIP_FUEL_SCOOP      = 1u << 2,
    EQUIP_ENERGY_BOMB     = 1u << 3,
    EQUIP_ESCAPE_POD      = 1u << 4,
  };

  // Number of commodity stacks a hold carries; must equal GameLogic::COMMODITY_COUNT
  // (the converter static_asserts it, so a drift there fails the build here).
  inline constexpr std::size_t PERSIST_COMMODITY_COUNT = 17;

  struct PlayerPersistState
  {
    std::string commanderName;    // the account key (the sanitized ClientHello name)
    int32_t  credits = 0;         // Wallet.credits (tenths of a credit)
    int32_t  fuelTenths = 0;      // Fuel.tenths (max stays code-owned)
    int32_t  wantedLevel = 0;     // Wanted.level
    int32_t  score = 0;           // session score (per-player record, C2)
    int32_t  holdCapacity = 0;    // CargoHold.capacity
    int32_t  missiles = 0;        // Equipment.missiles
    uint32_t equipFlags = 0;      // PersistEquipFlags bitmask
    int32_t  lastSystemId = -1;   // system to wake docked at (-1 = home)
    bool     inWitchspace = false;
    uint64_t updatedTick = 0;     // world tick of the snapshot (sim clock, not wall)
    std::array<int32_t, PERSIST_COMMODITY_COUNT> cargo{};   // per-commodity units
  };
}
