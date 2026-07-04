#pragma once

// ServerConfig - every tuning knob of the dedicated server, in one place.
//
// These are DEPLOYMENT/tuning constants (ports, cadences, interest ranges, RNG
// seeds) - the GAME rules' constants live with their systems in GameLogic
// (combat ranges, bounties, fuel prices, ...). Values are documented where they
// are used; docs/ARCHITECTURE.md carries the full quick-reference table.

#include <cstdint>

namespace DSOServer::Cfg
{
  inline constexpr uint16_t SERVER_PORT = 40000;            // clients send input here
  inline constexpr int RECV_BUDGET = 256;                   // max datagrams drained per tick
  inline constexpr uint32_t TICK_SLEEP_MS = 33;             // ~30 Hz fixed tick

  inline constexpr int64_t AOI_CELL_SIZE = 100000;          // interest-management cell size
  inline constexpr int AOI_RADIUS_CELLS = 1;                // viewers see +/- 1 cell
  inline constexpr uint32_t SESSION_TIMEOUT_TICKS = 300;    // reap a pending (pre-hello) shell idle this long (~10s)
  inline constexpr uint32_t SESSION_GRACE_TICKS = 1800;     // keep an AUTHENTICATED session alive this long on silence (~60s) for reconnect (B3)
  inline constexpr uint32_t SESSION_PARK_TICKS = 45;        // after this much silence, safe-park a live ship (zero its intent) (~1.5s) (B3)
  inline constexpr uint32_t PERSIST_INTERVAL = 150;        // snapshot live players to the store this often (~5s) (B4)

  inline constexpr int64_t DOCK_RANGE = 5000;               // how close a player must be to dock
  inline constexpr int64_t FIRE_RANGE = 6000;               // player front-laser reach
  inline constexpr double AIM_CONE = 0.9;                   // ~25deg aiming cone for a hit

  // How far a system's landmarks (planet + station) stay visible, independent of
  // the per-ship AOI radius. A planet/station is a huge, static body you fly
  // toward for a long time, so culling it at the +/-1 ship cell (~100k-300k
  // units) makes it pop in and out as you cross the system. ~20 AOI cells keeps
  // the local system on the wire while distant systems (scattered tens of
  // millions of units apart) stay culled.
  inline constexpr int64_t LANDMARK_VIS_DIST = 2'000'000;

  // Gameplay cadences (in ticks).
  inline constexpr uint32_t WANTED_DECAY_INTERVAL = 600;    // a wanted level cools ~every 20s
  inline constexpr uint32_t SHIELD_REGEN_INTERVAL = 8;      // legacy regenerated ~every 8 frames

  // Dynamic spawning.
  inline constexpr uint32_t PIRATE_SPAWN_INTERVAL = 600;    // one pirate near a player, up to the cap
  inline constexpr int MAX_NPCS = 12;

  // Rate-limit window for the player-respawn console log (~10s).
  inline constexpr uint32_t RESPAWN_LOG_WINDOW = 300;

  // Deterministic RNG stream seeds (the engine forbids wall-clock randomness).
  // Separate streams so loot, AI and hyperspace draws can't perturb each other.
  inline constexpr uint32_t SPAWN_SEED = 0x51EEDu;
  inline constexpr uint32_t LOOT_SEED = 0x1007C0DEu;
  inline constexpr uint32_t AI_SEED = 0xA11CEu;
  inline constexpr uint32_t HYPER_SEED = 0x5A17Eu;
}
