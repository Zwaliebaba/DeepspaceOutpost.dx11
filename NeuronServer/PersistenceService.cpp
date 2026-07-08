// PersistenceService - see PersistenceService.h.

#include "pch.h"

#include "PersistenceService.h"

#include <chrono>
#include <utility>

namespace Neuron::Persist
{
  PersistenceService::PersistenceService(std::unique_ptr<IPersistenceStore> _store,
                                         bool _startThread, std::size_t _commandRingCap)
    : m_store(std::move(_store))
    , m_commandRingCap(_commandRingCap == 0 ? 1 : _commandRingCap)
  {
    if (_startThread)
    {
      m_threaded = true;
      m_thread = std::thread([this] { WriterLoop(); });
    }
  }

  PersistenceService::~PersistenceService()
  {
    if (m_threaded)
    {
      {
        std::lock_guard<std::mutex> lk(m_mutex);
        m_stop.store(true);
      }
      m_cv.notify_all();
      if (m_thread.joinable())
        m_thread.join();   // the loop does a final flush on the way out
    }
  }

  void PersistenceService::QueuePlayerSnapshot(const PlayerPersistState& _state)
  {
    {
      std::lock_guard<std::mutex> lk(m_mutex);
      m_pendingPlayers[_state.commanderName] = _state;   // coalesce: latest wins
    }
    m_cv.notify_one();
  }

  void PersistenceService::QueueCommand(const CommandLogEntry& _entry)
  {
    {
      std::lock_guard<std::mutex> lk(m_mutex);
      m_pendingCommands.push_back(_entry);
      while (m_pendingCommands.size() > m_commandRingCap)
      {
        m_pendingCommands.pop_front();               // drop-oldest
        m_droppedCommands.fetch_add(1);
      }
    }
    m_cv.notify_one();
  }

  void PersistenceService::QueueMarketRows(const std::vector<MarketRow>& _rows)
  {
    if (_rows.empty())
      return;
    {
      std::lock_guard<std::mutex> lk(m_mutex);
      for (const MarketRow& r : _rows)
        m_pendingMarkets[std::make_pair(r.systemId, r.commodity)] = r;   // coalesce: latest wins
    }
    m_cv.notify_one();
  }

  std::vector<SystemRow> PersistenceService::LoadSystems()
  {
    return m_store->LoadSystems();   // boot-only: writer thread is idle (see header)
  }

  std::vector<MarketRow> PersistenceService::LoadMarkets()
  {
    return m_store->LoadMarketRows();   // boot-only: writer thread is idle (see header)
  }

  std::vector<PoiRow> PersistenceService::LoadPois()
  {
    return m_store->LoadPois();   // boot-only: writer thread is idle (see header)
  }

  std::vector<PoiResourceRow> PersistenceService::LoadPoiResources()
  {
    return m_store->LoadPoiResources();   // boot-only: writer thread is idle (see header)
  }

  void PersistenceService::RequestLoad(const std::string& _commanderName)
  {
    {
      std::lock_guard<std::mutex> lk(m_mutex);
      m_pendingLoads.push_back(_commanderName);
    }
    m_cv.notify_one();
  }

  std::vector<PlayerLoadResult> PersistenceService::DrainLoads()
  {
    std::lock_guard<std::mutex> lk(m_mutex);
    std::vector<PlayerLoadResult> out;
    out.swap(m_completedLoads);
    return out;
  }

  bool PersistenceService::HasPending() const
  {
    return !m_pendingPlayers.empty() || !m_pendingCommands.empty()
        || !m_pendingMarkets.empty() || !m_pendingLoads.empty();
  }

  std::size_t PersistenceService::FlushPendingOnce()
  {
    // Drain everything under the lock into locals, then do all store I/O unlocked.
    std::unordered_map<std::string, PlayerPersistState> players;
    std::vector<CommandLogEntry> commands;
    std::vector<MarketRow> markets;
    std::vector<std::string> loads;
    {
      std::lock_guard<std::mutex> lk(m_mutex);
      players.swap(m_pendingPlayers);
      commands.assign(m_pendingCommands.begin(), m_pendingCommands.end());
      m_pendingCommands.clear();
      markets.reserve(m_pendingMarkets.size());
      for (auto& kv : m_pendingMarkets)
        markets.push_back(kv.second);
      m_pendingMarkets.clear();
      loads.swap(m_pendingLoads);
    }

    for (const auto& kv : players)
      m_store->UpsertPlayer(kv.second);

    if (!commands.empty())
      m_store->AppendCommands(commands);

    if (!markets.empty())
      m_store->UpsertMarketRows(markets);

    std::vector<PlayerLoadResult> results;
    results.reserve(loads.size());
    for (const std::string& name : loads)
      results.push_back(PlayerLoadResult{ name, m_store->LoadPlayer(name) });

    if (!results.empty())
    {
      std::lock_guard<std::mutex> lk(m_mutex);
      for (PlayerLoadResult& r : results)
        m_completedLoads.push_back(std::move(r));
    }

    return players.size();
  }

  void PersistenceService::WriterLoop()
  {
    for (;;)
    {
      {
        std::unique_lock<std::mutex> lk(m_mutex);
        m_cv.wait(lk, [this] { return m_stop.load() || HasPending(); });
        if (m_stop.load() && !HasPending())
          break;
      }
      try
      {
        FlushPendingOnce();
      }
      catch (...)
      {
        // A transient store failure loses this pass's coalesced snapshots, but the
        // next cadence snapshot re-sends them (loss-free by coalescing). Back off a
        // little so a hard-down DB doesn't spin the thread. (Wall-clock sleep is
        // fine here - this is the persistence thread, not the sim.)
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
      }
    }

    // Final drain on shutdown so a graceful stop doesn't lose the last snapshots.
    try
    {
      FlushPendingOnce();
    }
    catch (...)
    {
    }
  }
}
