#pragma once

// GameServer - the dedicated server's orchestrator (Server).
//
// Owns the authoritative world and every per-process service around it
// (sessions, AOI, spawning, the combat message bus, the RNG streams) and runs
// the fixed tick as a sequence of named phases:
//
//   ReceiveDatagrams -> dispatch fire commands -> roster upkeep ->
//   ProcessReliableRequests -> AdvanceSimulation -> ResolveKills ->
//   LootAndScoop -> DecayWantedRecords -> ReapAndDespawn -> PublishState
//
// All game BEHAVIOUR lives in GameLogic systems; this class only sequences them
// and moves their results on/off the wire. Main.cpp is reduced to process
// startup + the loop; world construction lives in WorldBuilder; tuning knobs in
// ServerConfig.h.

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <unordered_set>
#include <vector>

#include "ECS.h"
#include "NetLib.h"
#include "Messages/MessageBus.h"
#include "Messages/Defs/PlayerSession.h"
#include "Messages/Defs/Travel.h"         // TravelRequest / TravelResponse
#include "Messages/Defs/TimeSync.h"       // Ping / Pong (E1 time sync)
#include "Messages/Defs/Strategic.h"      // StrategicSummary (E3 strategic tier)
#include "DatagramPump.h"     // NeuronServer: bounded drain + magic routing
#include "OnChangeCache.h"    // NeuronServer: send-on-change suppression
#include "PersistenceService.h"  // NeuronServer: async off-sim-thread durable writes (B4)
#include "TickMetrics.h"      // NeuronServer: always-on tick counters (D3)

#include "GameLogic.h"
#include "PlayerPersistence.h"   // GameLogic: component <-> PlayerPersistState converters (B4)

namespace DSOServer
{
  class GameServer
  {
  public:
    // Builds the world, provisions the services and registers the combat
    // subscribers. The socket stays owned by main (it is process lifetime).
    explicit GameServer(Neuron::Net::UdpSocket& _socket);

    // On a graceful stop, snapshots every live player one last time; the
    // persistence service then flushes on destruction (bounded).
    ~GameServer();

    // One fixed simulation tick (the caller paces it and updates the clock).
    void RunTick();

    // D3: the server loop reports a dropped-backlog tick (fell behind the fixed
    // step), and reads the rolling metrics (also read by the D5 BotClient harness).
    void NoteOverrun() { m_metrics.NoteOverrun(); }
    [[nodiscard]] const Neuron::Server::TickMetrics& Metrics() const { return m_metrics; }

  private:
    // Catalog messages compare via their Fields() tuple (no operator==).
    struct StatusFieldsEqual
    {
      bool operator()(const Neuron::Msg::PlayerStatus& _a, const Neuron::Msg::PlayerStatus& _b) const
      {
        return _a.Fields() == _b.Fields();
      }
    };

    // One station's market as a flat digest, so the on-change cache can persist a
    // system's rows only when its stock/prices actually drift (a trade), not every
    // cadence. Equal ⇔ every commodity's price and quantity match.
    struct MarketDigest
    {
      std::array<int32_t, Neuron::GameLogic::COMMODITY_COUNT> price{};
      std::array<int32_t, Neuron::GameLogic::COMMODITY_COUNT> stock{};
      bool operator==(const MarketDigest&) const = default;
    };

    // Two durable snapshots are "the same" if every persisted field matches - the
    // world tick differs every save, so it is excluded (else nothing is ever equal).
    struct PersistStateEqual
    {
      bool operator()(const Neuron::Persist::PlayerPersistState& _a,
                      const Neuron::Persist::PlayerPersistState& _b) const
      {
        return _a.commanderName == _b.commanderName && _a.credits == _b.credits
            && _a.fuelTenths == _b.fuelTenths && _a.wantedLevel == _b.wantedLevel
            && _a.score == _b.score && _a.holdCapacity == _b.holdCapacity
            && _a.missiles == _b.missiles && _a.equipFlags == _b.equipFlags
            && _a.lastSystemId == _b.lastSystemId && _a.inWitchspace == _b.inWitchspace
            && _a.cargo == _b.cargo;
      }
    };

    // --- tick phases (in run order) ---
    void ReceiveDatagrams();
    void ProcessReliableRequests();
    void ApplyCompletedLoads();   // B4: finish deferred handshakes whose load returned
    void AdvanceSimulation();
    void ResolveKills();
    void LootAndScoop();
    void DecayWantedRecords();
    void ReapAndDespawn();
    void PublishState();
    void PublishStrategicFor(Neuron::GameLogic::Session& _s);   // E3: per-system rollup to one viewer
    void SavePlayers();           // B4: cadence snapshot of live players (on change)
    void SaveMarkets();           // v2: cadence snapshot of drifted station markets (on change)

    // --- handlers & helpers ---
    void RegisterSubscribers();
    void OnInputPacket(const Neuron::Net::Endpoint& _from, const uint8_t* _data, std::size_t _size);
    void OnCrime(const Neuron::GameLogic::Crime& _c);
    void OnEntityKilled(const Neuron::GameLogic::EntityKilled& _k);
    void HandleStationRequest(Neuron::GameLogic::Session& _session, const Neuron::Net::StationRequest& _req);
    void HandleTravelRequest(Neuron::GameLogic::Session& _session, const Neuron::Msg::TravelRequest& _req);
    void SendCargoTo(uint32_t _entityIndex);
    void BroadcastPlayerInfo(uint32_t _entityIndex);

    // B4 persistence: build the store from DSO_DB (null = disabled), and finish a
    // deferred handshake by spawning + applying the loaded state (or fresh-spawning
    // and creating the account when the commander is unknown).
    static std::unique_ptr<Neuron::Persist::PersistenceService> MakePersistenceService();
    void FinishLoadedSpawn(const Neuron::Net::Endpoint& _ep,
                           const std::optional<Neuron::Persist::PlayerPersistState>& _state);

    // Append a reliable gameplay command (station/travel) to the audit/replay log,
    // as the message's own encoded bytes. No-op when persistence is disabled.
    void LogCommand(const Neuron::GameLogic::Session& _s, const Neuron::Net::ReliableMessage& _msg);

    // --- state ---
    Neuron::Net::UdpSocket& m_socket;
    Neuron::ECS::Registry m_world;
    std::vector<Neuron::ECS::EntityId> m_landmarks;
    Neuron::GameLogic::AreaOfInterest m_aoi;
    Neuron::GameLogic::ServerSessions m_sessions;
    Neuron::GameLogic::DespawnTracker m_despawns;
    Neuron::GameLogic::SpawnDirector m_spawner;
    Neuron::GameLogic::TransformHistory m_combatHistory;   // E1: per-tick rewind buffer for lag comp
    Neuron::Msg::MessageBus m_bus;

    std::vector<Neuron::Server::MagicRoute> m_routes;   // datagram magic -> handler
    uint8_t m_recv[2048] = {};

    uint32_t m_tick = 0;

    // Deterministic RNG streams (seeds in ServerConfig.h).
    uint32_t m_lootRng;
    uint32_t m_aiRng;
    uint32_t m_hyperRng;

    // Last PlayerStatus sent per session (by endpoint key): resend on change only.
    Neuron::Server::OnChangeCache<uint64_t, Neuron::Msg::PlayerStatus, StatusFieldsEqual> m_lastStatus;

    // B4 persistence (null when DSO_DB is unset - the whole feature is off and the
    // server behaves exactly as before). The change-cache skips unchanged saves.
    std::unique_ptr<Neuron::Persist::PersistenceService> m_persist;
    Neuron::Server::OnChangeCache<uint64_t, Neuron::Persist::PlayerPersistState, PersistStateEqual> m_lastPersist;

    // v2 galaxy persistence: true once the world was laid out from seeded system
    // rows (only then may market drift be written back - the FK needs the systems
    // row to exist). Last-persisted market per system id, so a cadence save writes
    // only the systems whose stock/prices actually changed.
    bool m_marketsPersisted = false;
    Neuron::Server::OnChangeCache<int32_t, MarketDigest> m_lastMarket;

    // D3 tick metrics: per-tick timing/counters, summarized periodically. The byte
    // counter is summed across this tick's sends; the candidate-pair counter is
    // fed by D1's grid. Timing comes from QPC (off the sim determinism path).
    Neuron::Server::TickMetrics m_metrics;
    uint64_t m_bytesThisTick = 0;
    uint64_t m_candidatePairsThisTick = 0;
    uint64_t m_droppedThisTick = 0;   // E2c: entities shed by the send budget this tick
    double m_metricsWindowStartMs = 0.0;

    // D2: persistent per-tick working storage for the GameLogic systems (see
    // FrameScratch.h) plus the two Server-side per-tick scratch buffers
    // (SnapshotHelpers), owned once here instead of allocated fresh every tick.
    Neuron::GameLogic::FrameScratch m_scratch;
    std::vector<uint32_t> m_currentIdsScratch;
    std::unordered_set<uint32_t> m_landmarkPresentScratch;

    // Rate-limited respawn logging.
    uint32_t m_lastRespawnLogTick = 0;
    int m_suppressedRespawns = 0;
  };
}
