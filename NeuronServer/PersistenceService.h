#pragma once

// PersistenceService - async, off-sim-thread durable writes (NeuronServer).
//
// Owns a single writer thread and the queues between it and the sim thread, so
// the simulation NEVER touches the store, never blocks on the database, and never
// reads the wall clock for its own purposes (§12: async batched writes off the sim
// thread; timestamps are stamped here, on the persistence thread; the sim
// contributes only tick numbers).
//
// Queues:
//   * player snapshots IN - COALESCED one-per-player, latest wins, so memory is
//     bounded by player count no matter how slow the DB is, and a lost write self-
//     heals on the next cadence snapshot.
//   * command-log batches - a capped ring that drops-oldest and counts the drops
//     (audit material, never authority - bounded loss is acceptable and observable).
//   * load requests IN / load results OUT - a hello triggers a load; the sim thread
//     drains completed loads at a fixed tick step and applies them.
//
// The core is FlushPendingOnce(): drain the queues under the lock, then do all
// store I/O OUTSIDE the lock. The writer thread just calls it on a signal; tests
// construct the service WITHOUT the thread and call FlushPendingOnce() directly,
// which makes coalescing/overflow/load behaviour deterministic (no thread timing).

#include <atomic>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <map>
#include <memory>
#include <mutex>
#include <utility>
#include <optional>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

#include "PersistenceStore.h"

namespace Neuron::Persist
{
  // A completed load handed back to the sim thread. `state` is nullopt for an
  // unknown commander (the server then fresh-spawns and creates the account).
  struct PlayerLoadResult
  {
    std::string commanderName;
    std::optional<PlayerPersistState> state;
  };

  class PersistenceService
  {
  public:
    // Takes ownership of the store. `_startThread` true (production) runs the
    // background writer; false (tests) leaves the caller to drive FlushPendingOnce.
    // `_commandRingCap` bounds the pending command-log ring (drop-oldest beyond it).
    explicit PersistenceService(std::unique_ptr<IPersistenceStore> _store,
                                bool _startThread = true,
                                std::size_t _commandRingCap = 4096);
    ~PersistenceService();

    PersistenceService(const PersistenceService&) = delete;
    PersistenceService& operator=(const PersistenceService&) = delete;

    // --- called from the SIM thread (cheap: copy + enqueue under a short lock) ---

    // Queue a player's durable snapshot. Coalesced: only the latest per commander
    // is kept, so re-snapshotting every cadence is free.
    void QueuePlayerSnapshot(const PlayerPersistState& _state);

    // Append one command-log row (rings; drops-oldest past the cap, counted).
    void QueueCommand(const CommandLogEntry& _entry);

    // Queue drifted market rows. Coalesced per (system,commodity): re-snapshotting
    // an unchanged market every cadence stays free and memory is bounded by the
    // number of distinct rows, not the cadence.
    void QueueMarketRows(const std::vector<MarketRow>& _rows);

    // --- called ONCE at boot, on the sim thread, BEFORE any queue is fed -------
    // Synchronous bulk reads used to lay out the world. Safe to call directly on
    // the store here because no snapshot/command/load has been queued yet, so the
    // writer thread is idle-waiting and cannot be inside a flush.

    // Every system (location) row. Empty ⇒ the DB has not been seeded yet.
    [[nodiscard]] std::vector<SystemRow> LoadSystems();
    // Every persisted market row, to restore drifted trade state at boot.
    [[nodiscard]] std::vector<MarketRow> LoadMarkets();
    // Every scene POI row (scene.md anchors + params), to materialize scenes at boot.
    [[nodiscard]] std::vector<PoiRow> LoadPois();
    // Every persisted POI resource pool, to restore drained belts at boot.
    [[nodiscard]] std::vector<PoiResourceRow> LoadPoiResources();

    // Request an async load for `_commanderName`; the result arrives via DrainLoads.
    void RequestLoad(const std::string& _commanderName);

    // Take the loads that have completed since the last call (the sim thread applies
    // them at a fixed tick step). Empty if none are ready.
    [[nodiscard]] std::vector<PlayerLoadResult> DrainLoads();

    // Number of command-log rows dropped by ring overflow (a metric for D4).
    [[nodiscard]] uint64_t DroppedCommands() const { return m_droppedCommands.load(); }

    // Do one drain-and-write pass synchronously and return the count of player
    // upserts performed. The writer thread calls this on a signal; tests call it
    // directly for deterministic behaviour.
    std::size_t FlushPendingOnce();

  private:
    void WriterLoop();
    [[nodiscard]] bool HasPending() const;   // caller holds m_mutex

    std::unique_ptr<IPersistenceStore> m_store;
    const std::size_t m_commandRingCap;

    mutable std::mutex m_mutex;
    std::condition_variable m_cv;
    std::unordered_map<std::string, PlayerPersistState> m_pendingPlayers;   // coalesced
    std::deque<CommandLogEntry> m_pendingCommands;                          // ring
    std::map<std::pair<int32_t, int32_t>, MarketRow> m_pendingMarkets;      // coalesced by (system,commodity)
    std::vector<std::string> m_pendingLoads;
    std::vector<PlayerLoadResult> m_completedLoads;
    std::atomic<uint64_t> m_droppedCommands{ 0 };

    std::atomic<bool> m_stop{ false };
    std::thread m_thread;
    bool m_threaded = false;
  };
}
