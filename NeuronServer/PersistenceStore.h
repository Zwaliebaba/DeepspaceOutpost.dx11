#pragma once

// PersistenceStore - the durable-store seam and its in-memory reference (NeuronServer).
//
// IPersistenceStore is the swap point between the production SQL Server backing
// (OdbcStore, a Windows-only .cpp) and the header-only InMemoryStore that every
// test (and a persistence-disabled server) uses - the one interface that keeps CI
// free of a database dependency. Implementations are driven only from the
// persistence service's single WRITER THREAD, so they need not be internally
// thread-safe; the service serializes every call.

#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include "PlayerPersistState.h"

namespace Neuron::Persist
{
  // One append-only command-log entry (audit / refund / replay material, never
  // authority). The payload is a catalog message's generic-codec encoding.
  struct CommandLogEntry
  {
    uint64_t worldTick = 0;
    int32_t  playerId = 0;
    int32_t  messageId = 0;          // the catalog MessageId
    std::vector<uint8_t> payload;
  };

  // One market row. Seeded to the generated baseline by tools/dbseed and then
  // authoritative: the server loads it at boot and persists drift on a cadence,
  // so trade state (stock/price) survives a restart.
  struct MarketRow
  {
    int32_t  systemId = 0;
    int32_t  commodity = 0;
    int32_t  stock = 0;
    int32_t  price = 0;              // legacy x4 fixed-point, as in MarketEntry
    uint64_t updatedTick = 0;
  };

  // One system's durable LOCATION + attributes (v2 schema). The universe is loaded
  // from these rows at boot rather than regenerated from a seed, so the planet id
  // is stable and station_markets can key against it. Positions are the absolute
  // int64 world coordinates (static - not the §12-forbidden per-tick kind).
  struct SystemRow
  {
    int32_t systemId = 0;           // stable galaxy id (the procedural index)
    std::string name;
    int64_t planetX = 0, planetY = 0, planetZ = 0;
    int64_t stationX = 0, stationY = 0, stationZ = 0;
    int32_t economy = 0;            // 0..7
    int32_t government = 0;         // 0..7
    int32_t techLevel = 0;
    int32_t population = 0;
    int32_t productivity = 0;
    int32_t radius = 0;
    int32_t marketSeed = 0;         // fed to GenerateMarket() for the baseline
  };

  // One scene POI (v3 schema, dbo.system_pois). Anchors + parameters only - the
  // server materializes in-area detail (belt rocks) from the seed. Nullable SQL
  // columns are carried as sentinels (-1 = "null") so the plain struct stays POD.
  struct PoiRow
  {
    int32_t poiId = 0;             // stable id (assigned by the seeder, not IDENTITY)
    int32_t systemId = 0;
    int32_t kind = 0;              // GameLogic::PoiKind
    int64_t x = 0, y = 0, z = 0;   // absolute int64 anchor position
    int32_t radius = 0;
    int32_t seed = 0;              // in-area procedural detail
    int32_t commodity = -1;        // AsteroidBelt ore, else -1
    int32_t richness = -1;         // AsteroidBelt baseline pool, else -1
    int32_t encounterDef = -1;     // EncounterSite def id, else -1
    int32_t ownerEmpire = -1;      // future (factory/gun), else -1
    int32_t linkPoi = -1;          // future (jump-gate pairing), else -1
    bool    enabled = true;
    int32_t templateId = 0;        // provenance
    bool    authored = false;      // 1 = hand-edited: dbseed MERGE must not touch it
  };

  // One minable POI's durable resource pool (v3 schema, dbo.poi_resources). Loaded
  // at boot, drift-persisted on the slow cadence (scene.md 3.7b).
  struct PoiResourceRow
  {
    int32_t  poiId = 0;
    int32_t  units = 0;
    int32_t  baseline = 0;
    uint64_t updatedTick = 0;
  };

  class IPersistenceStore
  {
  public:
    virtual ~IPersistenceStore() = default;

    // Insert-or-replace the durable snapshot keyed by commander name.
    virtual void UpsertPlayer(const PlayerPersistState& _state) = 0;
    // The stored snapshot for `_commanderName`, or nullopt if unknown (fresh spawn).
    virtual std::optional<PlayerPersistState> LoadPlayer(const std::string& _commanderName) = 0;
    // Append a batch of command-log rows.
    virtual void AppendCommands(const std::vector<CommandLogEntry>& _batch) = 0;
    // Insert-or-replace a batch of market rows (keyed by system+commodity).
    virtual void UpsertMarketRows(const std::vector<MarketRow>& _batch) = 0;
    // Every persisted market row (loaded at boot to restore drifted trade state).
    virtual std::vector<MarketRow> LoadMarketRows() = 0;
    // Insert-or-replace a batch of system (location) rows. Used by the seeding
    // tool; the running server only reads systems.
    virtual void UpsertSystems(const std::vector<SystemRow>& _batch) = 0;
    // Every system row (loaded at boot to lay out the universe). Empty ⇒ unseeded.
    virtual std::vector<SystemRow> LoadSystems() = 0;
    // Insert-or-replace a batch of scene POI rows (seeding tool + editor). The
    // running server only reads them.
    virtual void UpsertPois(const std::vector<PoiRow>& _batch) = 0;
    // Every POI row (loaded at boot to materialize scenes). Empty ⇒ no scenes yet.
    virtual std::vector<PoiRow> LoadPois() = 0;
    // Insert-or-replace a batch of POI resource rows (drift persisted on cadence).
    virtual void UpsertPoiResources(const std::vector<PoiResourceRow>& _batch) = 0;
    // Every POI resource row (loaded at boot to restore drained belt pools).
    virtual std::vector<PoiResourceRow> LoadPoiResources() = 0;
    // World metadata (schema_version, galaxy_seed, world_tick).
    virtual std::optional<std::string> ReadMeta(const std::string& _key) = 0;
    virtual void WriteMeta(const std::string& _key, const std::string& _value) = 0;
  };

  // Header-only in-memory store: the test double AND the reference semantics the
  // OdbcStore must match. Not thread-safe on its own - the service owns the only
  // thread that touches it.
  class InMemoryStore final : public IPersistenceStore
  {
  public:
    void UpsertPlayer(const PlayerPersistState& _state) override
    {
      m_players[_state.commanderName] = _state;
    }

    std::optional<PlayerPersistState> LoadPlayer(const std::string& _commanderName) override
    {
      const auto it = m_players.find(_commanderName);
      if (it == m_players.end())
        return std::nullopt;
      return it->second;
    }

    void AppendCommands(const std::vector<CommandLogEntry>& _batch) override
    {
      m_commands.insert(m_commands.end(), _batch.begin(), _batch.end());
    }

    void UpsertMarketRows(const std::vector<MarketRow>& _batch) override
    {
      for (const MarketRow& r : _batch)
        m_markets[std::make_pair(r.systemId, r.commodity)] = r;
    }

    std::vector<MarketRow> LoadMarketRows() override
    {
      std::vector<MarketRow> out;
      out.reserve(m_markets.size());
      for (const auto& kv : m_markets)
        out.push_back(kv.second);
      return out;
    }

    void UpsertSystems(const std::vector<SystemRow>& _batch) override
    {
      for (const SystemRow& r : _batch)
        m_systems[r.systemId] = r;
    }

    std::vector<SystemRow> LoadSystems() override
    {
      std::vector<SystemRow> out;
      out.reserve(m_systems.size());
      for (const auto& kv : m_systems)
        out.push_back(kv.second);
      return out;
    }

    void UpsertPois(const std::vector<PoiRow>& _batch) override
    {
      for (const PoiRow& r : _batch)
        m_pois[r.poiId] = r;
    }

    std::vector<PoiRow> LoadPois() override
    {
      std::vector<PoiRow> out;
      out.reserve(m_pois.size());
      for (const auto& kv : m_pois)
        out.push_back(kv.second);
      return out;
    }

    void UpsertPoiResources(const std::vector<PoiResourceRow>& _batch) override
    {
      for (const PoiResourceRow& r : _batch)
        m_poiResources[r.poiId] = r;
    }

    std::vector<PoiResourceRow> LoadPoiResources() override
    {
      std::vector<PoiResourceRow> out;
      out.reserve(m_poiResources.size());
      for (const auto& kv : m_poiResources)
        out.push_back(kv.second);
      return out;
    }

    std::optional<std::string> ReadMeta(const std::string& _key) override
    {
      const auto it = m_meta.find(_key);
      if (it == m_meta.end())
        return std::nullopt;
      return it->second;
    }

    void WriteMeta(const std::string& _key, const std::string& _value) override
    {
      m_meta[_key] = _value;
    }

    // --- test inspection ---
    [[nodiscard]] std::size_t PlayerCount() const { return m_players.size(); }
    [[nodiscard]] const std::vector<CommandLogEntry>& Commands() const { return m_commands; }
    [[nodiscard]] std::size_t MarketRowCount() const { return m_markets.size(); }
    [[nodiscard]] std::size_t SystemCount() const { return m_systems.size(); }

  private:
    std::unordered_map<std::string, PlayerPersistState> m_players;
    std::vector<CommandLogEntry> m_commands;
    std::map<std::pair<int32_t, int32_t>, MarketRow> m_markets;   // (system,commodity) -> row
    std::map<int32_t, SystemRow> m_systems;                       // system_id -> row
    std::map<int32_t, PoiRow> m_pois;                             // poi_id -> row
    std::map<int32_t, PoiResourceRow> m_poiResources;            // poi_id -> pool row
    std::unordered_map<std::string, std::string> m_meta;
  };
}
