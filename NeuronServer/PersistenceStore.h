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

  // One drifted market row (lazily materialized; authoritative once F4 makes
  // markets mutable). Schema now so the retrofit is additive.
  struct MarketRow
  {
    int32_t  systemId = 0;
    int32_t  commodity = 0;
    int32_t  stock = 0;
    int32_t  price = 0;              // legacy x4 fixed-point, as in MarketEntry
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

  private:
    std::unordered_map<std::string, PlayerPersistState> m_players;
    std::vector<CommandLogEntry> m_commands;
    std::map<std::pair<int32_t, int32_t>, MarketRow> m_markets;   // (system,commodity) -> row
    std::unordered_map<std::string, std::string> m_meta;
  };
}
