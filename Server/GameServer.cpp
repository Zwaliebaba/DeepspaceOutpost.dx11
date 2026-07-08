// GameServer - the dedicated server's orchestrator. See GameServer.h.

#include "GameServer.h"

#include <cstdio>
#include <cstdlib>   // getenv (DSO_DB)
#include <ranges>
#include <utility>

#include "SnapshotPacketizer.h"
#include "SnapshotBudget.h"   // per-session send budget + distance-sorted drop (E2c)
#include "Messages/Defs/InputCommand.h"
#include "StationProtocol.h"
#include "Messages/Framing.h"
#include "Messages/Reliable.h"
#include "Messages/MessageEndpoint.h"
#include "Messages/Defs/CoreEvents.h"
#include "Messages/Defs/EquipmentEvents.h"

#include "ServerConfig.h"
#include "SnapshotHelpers.h"
#include "WorldBuilder.h"
#include "SecureRandom.h"   // OS CSPRNG for session tokens (B2)
#include "OdbcStore.h"      // SQL-Server backend (B4.3), compiled only under DSO_ENABLE_ODBC

using namespace Neuron;

namespace DSOServer
{
  namespace
  {
    // High-resolution wall clock in milliseconds for tick METRICS only (D3). This
    // is off the simulation's determinism path - it times phases, it never feeds
    // the sim - so reading the wall clock here does not violate the §12 rule.
    [[nodiscard]] double QpcMs()
    {
      LARGE_INTEGER freq;
      LARGE_INTEGER now;
      QueryPerformanceFrequency(&freq);
      QueryPerformanceCounter(&now);
      return static_cast<double>(now.QuadPart) * 1000.0 / static_cast<double>(freq.QuadPart);
    }
  }

  GameServer::GameServer(Net::UdpSocket& _socket, Net::UdpSocket* _adminSocket, uint16_t _gamePort)
    : m_socket(_socket)
    , m_adminSocket(_adminSocket)
    , m_aoi(Cfg::AOI_CELL_SIZE)
    , m_spawner(Cfg::SPAWN_SEED, Cfg::PIRATE_SPAWN_INTERVAL, Cfg::MAX_NPCS)
    , m_lootRng(Cfg::LOOT_SEED)
    , m_aiRng(Cfg::AI_SEED)
    , m_hyperRng(Cfg::HYPER_SEED)
    , m_admin(MakeAdminChannel())
  {
    // Persistence (B4): null unless DSO_DB is set (then the connect flow defers the
    // spawn until the commander's durable state loads). Built BEFORE the world so
    // the galaxy layout can be loaded from the store (v2).
    m_persist = MakePersistenceService();

    // The authoritative world: laid out from the persisted system rows (the
    // initial-loading mechanism) when the DB is seeded, else generated from the
    // seed. Drifted markets are restored on top. Every client gets the chart
    // manifest on connect.
    std::vector<Neuron::Persist::SystemRow> systems;
    std::vector<Neuron::Persist::MarketRow> markets;
    std::vector<Neuron::Persist::PoiRow> pois;
    std::vector<Neuron::Persist::PoiResourceRow> poiResources;
    if (m_persist)
    {
      systems = m_persist->LoadSystems();   // boot-only synchronous read (writer idle)
      markets = m_persist->LoadMarkets();
      pois = m_persist->LoadPois();                   // scene.md: POI anchors + params
      poiResources = m_persist->LoadPoiResources();   // drained belt pools (drift restore)
      if (systems.empty())
        std::fprintf(stderr,
            "[persist] DSO_DB is set but dbo.systems is empty - run tools/dbseed to seed the galaxy. "
            "Falling back to seed-generated locations; market drift will NOT be persisted this run.\n");
      else
        m_marketsPersisted = true;   // the FK targets exist, so drift can be written back
    }

    WorldSetup setup = BuildWorld(m_world, systems, markets, pois, poiResources);
    m_landmarks = std::move(setup.landmarks);
    m_sessions.SetManifest(std::move(setup.manifest));
    m_sceneIndex = std::move(setup.sceneIndex);   // scene.md: kept for scene-chunk + POI-jump

    // Session tokens (B2) come from the OS CSPRNG, not a gameplay RNG stream: a
    // token must be unguessable, and determinism rules stop at the GameLogic edge.
    m_sessions.SetTokenSource(&SecureRandom64);

    // Datagram routing: 'NMSG' packets carry the unreliable InputCommand lane;
    // 'NRLB' datagrams feed each session's reliable lanes (and provision a pending
    // shell for a brand-new endpoint so its ClientHello can be received).
    m_routes = {
      { Msg::MESSAGE_MAGIC,
        [this](const Net::Endpoint& _from, const uint8_t* _data, std::size_t _size)
        { OnInputPacket(_from, _data, _size); } },
      { Msg::RELIABLE_MAGIC,
        [this](const Net::Endpoint& _from, const uint8_t* _data, std::size_t _size)
        { m_sessions.OnReliable(_from, _data, _size, m_tick); } },
    };

    RegisterSubscribers();

    // sm.md management channel: only live when DSO_ADMIN_KEY was set AND main opened
    // an admin socket. Stamp the identity the manager's header bar shows, wire the
    // admin socket's reliable-magic route, and record that the server came up.
    if (m_adminSocket != nullptr && m_admin.Enabled())
    {
      const uint16_t gamePort = (_gamePort != 0) ? _gamePort : Cfg::SERVER_PORT;
      m_admin.SetIdentity(GameLogic::Version(), Cfg::TICK_SLEEP_MS, gamePort);
      m_adminRoutes = {
        { Msg::RELIABLE_MAGIC,
          [this](const Net::Endpoint& _from, const uint8_t* _data, std::size_t _size)
          { m_admin.OnDatagram(_from, _data, _size, m_tick); } },
      };
      AdminNote(Msg::AdminEventKind::ServerStarted, 0, "server started");
    }
  }

  GameServer::~GameServer()
  {
    // Graceful stop: snapshot every live player one last time, then let m_persist's
    // destructor stop the writer thread after a final flush. (A hard kill skips
    // this; the ~5 s cadence save bounds the loss.)
    if (m_persist)
      for (auto& kv : m_sessions.All())
        if (m_world.IsValid(kv.second.entity))
          m_persist->QueuePlayerSnapshot(GameLogic::PlayerStateFromComponents(
              m_world, kv.second.entity, m_tick, kv.second.name, kv.second.score));
  }

  std::unique_ptr<Neuron::Persist::PersistenceService> GameServer::MakePersistenceService()
  {
    const char* db = std::getenv("DSO_DB");
    if (db == nullptr || db[0] == '\0')
      return nullptr;   // persistence disabled: the server behaves exactly as before

    std::unique_ptr<Neuron::Persist::IPersistenceStore> store;
#ifdef DSO_ENABLE_ODBC
    // Production: connect to SQL Server. A connect failure disables persistence
    // ENTIRELY (no store, no deferred load) rather than falling back to a volatile
    // store - a transient DB outage must never spawn a commander fresh and then
    // alias their real saved rows over the top.
    store = MakeOdbcStore(db);
    if (!store)
    {
      std::fprintf(stderr, "[persist] ODBC connect failed for DSO_DB; running WITHOUT persistence\n");
      return nullptr;
    }
#else
    // Built without the ODBC backend: an in-memory store proves the wiring (durable
    // only within a run). This is the dev/CI path when DSO_DB is set.
    store = std::make_unique<Neuron::Persist::InMemoryStore>();
#endif
    return std::make_unique<Neuron::Persist::PersistenceService>(std::move(store), /*startThread*/ true);
  }

  Neuron::Server::AdminChannel GameServer::MakeAdminChannel()
  {
    // The management channel is OFF unless DSO_ADMIN_KEY is set: an empty-key channel
    // ignores every datagram, and main never opens the socket. Tokens come from the OS
    // CSPRNG (SecureRandom64), like session tokens - an admin token must be unguessable.
    const char* key = std::getenv("DSO_ADMIN_KEY");
    if (key == nullptr || key[0] == '\0')
      return Neuron::Server::AdminChannel{};
    return Neuron::Server::AdminChannel{ std::string(key), &SecureRandom64 };
  }

  void GameServer::NoteOverrun()
  {
    m_metrics.NoteOverrun();
    ++m_lifetimeOverruns;
    // Surface overruns on the management feed, but rate-limited: a sustained overload
    // must not flood the event ring (one line per health window is plenty).
    if (m_admin.Enabled() && m_tick - m_lastOverrunEventTick >= Cfg::ADMIN_HEALTH_INTERVAL)
    {
      m_lastOverrunEventTick = m_tick;
      AdminNote(Msg::AdminEventKind::TickOverrun, 0, "server tick overran the fixed step");
    }
  }

  void GameServer::AdminNote(Msg::AdminEventKind _kind, uint32_t _subject, std::string _text)
  {
    if (m_admin.Enabled())
      m_admin.PushEvent(m_tick, _kind, _subject, std::move(_text));
  }

  void GameServer::RunTick()
  {
    const double tickStartMs = QpcMs();
    if (m_metricsWindowStartMs == 0.0)
      m_metricsWindowStartMs = tickStartMs;
    m_bytesThisTick = 0;
    m_candidatePairsThisTick = 0;   // fed by D1's grid queries
    m_droppedThisTick = 0;          // fed by E2c's per-session send budget

    // 1. Receive client input and acks. Input from an endpoint that hasn't
    //    completed the ClientHello handshake is ignored (the hello is the front
    //    door - see ProcessReliableRequests). Then resolve this tick's fire
    //    commands -> Crime / EntityKilled facts (police dispatch and death/destroy
    //    happen in their subscribers) before the simulation advances.
    ReceiveDatagrams();
    m_bus.Dispatch();

    // 1a''. Drain the SEPARATE management socket (sm.md) into the AdminChannel. Its
    //       own small budget means an admin-port flood can't steal game-tick time.
    PumpAdmin();

    // 1b. Process the reliable channel: the ClientHello handshake (which connects
    //     a client and broadcasts the refreshed roster), plus station/travel/chart
    //     requests from already-connected sessions.
    ProcessReliableRequests();

    // 1c. Finish any deferred (persistence) handshakes whose load has returned:
    //     spawn the commander from their durable state (B4). No-op when disabled.
    ApplyCompletedLoads();

    // 2. NPC tactics + the simulation tick + dynamic spawning + shield regen.
    AdvanceSimulation();

    // 2a'. Record this tick's final transforms for lag compensation (E1): next
    //      tick's player-fire resolution rewinds targets against this ring. Derived
    //      state only - it never feeds back into the authoritative simulation.
    m_combatHistory.Capture(m_world);

    // 2b. Missiles, realtime combat and collision grinding -> the death pipeline.
    ResolveKills();

    // 2c. Cargo canisters age/despawn; players scoop what they fly into.
    LootAndScoop();

    // 2d. Wanted records cool on a slow cadence.
    DecayWantedRecords();

    // 3. Reap idle clients; broadcast every despawn.
    ReapAndDespawn();

    // 4. Per-viewer snapshots + on-change private status + reliable flush.
    PublishState();

    // 4b. Management channel (sm.md): roster deltas + a periodic health sample to
    //     every connected ServerManager, then flush the admin socket. No-op when off.
    PublishAdmin();

    // 5. Persist changed players on a slow cadence (B4). No-op when disabled.
    if (m_persist && m_tick % Cfg::PERSIST_INTERVAL == 0)
      SavePlayers();

    // 5b. Persist drifted station markets on a slower cadence (v2). Only when the
    //     galaxy was loaded from seeded rows (else the FK targets don't exist).
    if (m_persist && m_marketsPersisted && m_tick % Cfg::MARKET_PERSIST_INTERVAL == 0)
      SaveMarkets();

    // 6. Record this tick's metrics (D3) and emit a rolling summary line.
    Server::TickSample sample;
    sample.durationMs = QpcMs() - tickStartMs;
    sample.entityCount = static_cast<uint32_t>(m_world.AliveCount());
    sample.sessionCount = static_cast<uint32_t>(m_sessions.Count());
    sample.candidatePairs = m_candidatePairsThisTick;
    sample.bytesSent = m_bytesThisTick;
    sample.droppedEntities = m_droppedThisTick;
    m_metrics.Record(sample);

    if (m_metrics.WindowTicks() >= Cfg::METRICS_WINDOW_TICKS)
    {
      const double nowMs = QpcMs();
      const double windowSec = (nowMs - m_metricsWindowStartMs) / 1000.0;
      const Server::TickSummary s = m_metrics.Snapshot(windowSec > 0.0 ? windowSec : 1.0);
      printf("[metrics] ticks=%llu avg=%.2fms max=%.2fms overruns=%llu entities=%u sessions=%u pairs=%llu bytes/s=%llu dropped=%llu\n",
             static_cast<unsigned long long>(s.ticks), s.avgMs, s.maxMs,
             static_cast<unsigned long long>(s.overruns), s.entities, s.sessions,
             static_cast<unsigned long long>(s.avgCandidatePairs),
             static_cast<unsigned long long>(s.bytesPerSecond),
             static_cast<unsigned long long>(s.droppedEntities));
      fflush(stdout);   // the D5 harness parses this line from a redirected pipe
      m_metrics.Reset();
      m_metricsWindowStartMs = nowMs;
    }
  }

  // --- receive ---------------------------------------------------------------

  void GameServer::ReceiveDatagrams()
  {
    Server::PumpDatagrams(m_socket, m_recv, sizeof(m_recv), Cfg::RECV_BUDGET, m_routes);
  }

  void GameServer::PumpAdmin()
  {
    // Drain the management socket with its OWN small budget and route (reliable
    // 'NRLB' only). Reuses the shared recv buffer - this runs to completion before
    // any other socket read this tick, so there is no aliasing.
    if (m_adminSocket == nullptr || !m_admin.Enabled())
      return;
    Server::PumpDatagrams(*m_adminSocket, m_recv, sizeof(m_recv), Cfg::ADMIN_RECV_BUDGET, m_adminRoutes);
  }

  void GameServer::OnInputPacket(const Net::Endpoint& _from, const uint8_t* _data, std::size_t _size)
  {
    // Unified 'NMSG' framing. Today the client uses the UNRELIABLE lane for its
    // InputCommand; other lanes fold in as later phases land.
    Msg::PacketHeader hdr;
    std::vector<Msg::Record> records;
    if (!Msg::ReadPacket(_data, _size, hdr, records) || hdr.lane != Msg::MessageLane::Unreliable)
      return;

    for (const Msg::Record& rec : records)
    {
      // Inbound validation: only the expected command id on this lane, and only
      // if it decodes (direction is guaranteed by the message type: InputCommand
      // is ClientToServer). Malformed/unknown records are dropped;
      // stale/duplicate inputs are rejected by OnInput's sequence.
      static_assert(Msg::InputCommand::Dir == Msg::Direction::ClientToServer);
      if (rec.id != Msg::InputCommand::Id)
        continue;
      Msg::InputCommand in;
      if (!Msg::DecodeRecord(rec, in))
        continue;

      // The heartbeat carries only freshness + the snapshot ack. Equipment
      // activations arrive as reliable AbilityRequests (HandleAbilityRequest)
      // and movement as UnitOrders - nothing on this lane fires a weapon.
      m_sessions.OnInput(m_world, _from, hdr.token, in, m_tick);
    }
  }

  // --- roster / reliable requests ---------------------------------------------

  void GameServer::ProcessReliableRequests()
  {
    for (auto& s : m_sessions.All() | std::views::values)
    {
      Net::ReliableMessage msg;
      while (s.events.Receive(msg))
      {
        // The ClientHello is the front door (Control lane, drained first): a valid
        // one connects a pending client (spawns its entity, queues HelloAck), or
        // resumes a live one that reconnected (B3). A version mismatch is rejected
        // inside OnHello.
        Msg::ClientHello hello;
        if (Msg::TryDecode(msg, hello))
        {
          // With persistence on, defer the spawn: OnHello parks the session and we
          // load the commander first (ApplyCompletedLoads finishes the handshake),
          // so a returning commander is never spawned-fresh.
          const bool defer = (m_persist != nullptr);
          const GameLogic::HelloOutcome out = m_sessions.OnHello(m_world, s.endpoint, hello, m_tick, defer);
          if (out.result == GameLogic::HelloResult::Accepted)
          {
            // No persistence (this path only runs with DSO_DB unset): a fresh
            // commander is placed docked at a system chosen from their name, so
            // players scatter across the galaxy rather than all launching from one
            // spot. (With persistence on, account creation docks in FinishLoadedSpawn.)
            GameLogic::DockAtNameChosenSystem(m_world, out.entity, s.name);
            printf("Client connected: entity %u (\"%s\")\n", out.entity.index, s.name.c_str());
            // Replay the full roster: the joiner learns everyone, everyone learns
            // the joiner. (A leaver's ship goes out as EntityDespawn on reap.)
            for (const Msg::PlayerInfo& pi : m_sessions.Roster(m_world))
              m_sessions.Broadcast(pi);
          }
          else if (out.result == GameLogic::HelloResult::Loading)
          {
            // Parked (persistence): a named commander is loaded from the store; a
            // blank name has no account, so spawn it fresh right away.
            if (s.name.empty())
              FinishLoadedSpawn(s.endpoint, std::nullopt);
            else
              m_persist->RequestLoad(s.name);
          }
          else if (out.result == GameLogic::HelloResult::Resumed)
          {
            // Reconnect (B3): OnHello already re-queued HelloAck. Re-sync this one
            // client - replay the full roster to it, resend its cargo, and force a
            // PlayerStatus resend (evict the change-cache) - and, if the reconnect
            // also renamed, tell everyone.
            printf("Client resumed: entity %u (\"%s\")\n", out.entity.index, s.name.c_str());
            for (const Msg::PlayerInfo& pi : m_sessions.Roster(m_world))
              s.events.Send(pi);
            SendCargoTo(out.entity.index);
            m_lastStatus.Forget(GameLogic::EndpointKey(s.endpoint));
            if (out.nameChanged)
              BroadcastPlayerInfo(out.entity.index);
            AdminNote(Msg::AdminEventKind::PlayerReconnected, s.playerId,
                      std::string("commander ") + s.name + " reconnected");
          }
          continue;
        }

        // Everything else is gameplay: a pending (pre-hello) shell can't do it, so
        // ignore until the session is live.
        if (!s.Live())
          continue;

        Net::StationRequest req;
        Msg::TravelRequest travel;
        Msg::UnitOrder order;
        Msg::AbilityRequest ability;
        Msg::GalaxyChunkRequest chunkReq;
        Msg::SceneChunkRequest sceneReq;
        Msg::Ping ping;
        Msg::Chat chat;
        if (Msg::TryDecode(msg, req))
        {
          LogCommand(s, msg);   // audit/replay (before the mutation it authorizes)
          HandleStationRequest(s, req);
        }
        else if (Msg::TryDecode(msg, travel))
        {
          LogCommand(s, msg);
          HandleTravelRequest(s, travel);
        }
        else if (Msg::TryDecode(msg, order))
        {
          LogCommand(s, msg);   // ordered commands are audit/replay material (I1)
          HandleUnitOrder(s, order);
        }
        else if (Msg::TryDecode(msg, ability))
          HandleAbilityRequest(s, ability);   // a fire action; not logged (like input fire)
        else if (Msg::TryDecode(msg, chunkReq))
          m_sessions.SendGalaxyChunks(s, chunkReq.baseIndex, chunkReq.count);   // Bulk lane (not logged)
        else if (Msg::TryDecode(msg, sceneReq))
          HandleSceneChunkRequest(s, sceneReq);   // scene.md: system POIs (Bulk lane, not logged)
        else if (Msg::TryDecode(msg, ping))
        {
          // Time sync (E1): record the client's reported RTT for lag compensation
          // (stored raw; the fire path clamps it to the history window), and echo
          // the timestamp + our current tick so the client can measure RTT and
          // align its clock. Not logged - it mutates no game state.
          s.rttMs = ping.rttMs;
          s.events.Send(Msg::Pong{ ping.clientTimeMs, m_tick });
        }
        else if (Msg::TryDecode(msg, chat))
          HandleChat(s, chat);   // G3: rate-limited, sanitised relay
      }
    }
  }

  // G3: relay a chat line. Rate-limit per session (drop + warn over cap), sanitise
  // the text server-side, stamp the AUTHENTICATED sender (playerId, so clients can
  // mute by it), and rebroadcast to the roster. AOI-scoped delivery is a refinement;
  // roster-wide is a superset for now.
  void GameServer::HandleChat(GameLogic::Session& _session, const Msg::Chat& _in)
  {
    if (!GameLogic::ChatAllowed(_session.chat, m_tick))
    {
      _session.events.Send(Msg::Chat{ 0, "You are chatting too fast." });   // sender 0 = system
      return;
    }
    const std::string text = GameLogic::SanitizeChat(_in.text);
    if (text.empty())
      return;
    m_sessions.Broadcast(Msg::Chat{ _session.playerId, text });
    AdminNote(Msg::AdminEventKind::Chat, _session.playerId, _session.name + ": " + text);
  }

  void GameServer::ApplyCompletedLoads()
  {
    if (!m_persist)
      return;

    for (const Neuron::Persist::PlayerLoadResult& r : m_persist->DrainLoads())
    {
      // Find the still-loading session that requested this commander (loads are
      // keyed by name). A duplicate result (from a lost-load re-request) finds no
      // loading session and is harmlessly skipped by FinishLoadedSpawn.
      const Neuron::GameLogic::Session* found = nullptr;
      for (const auto& kv : m_sessions.All())
        if (kv.second.loading && kv.second.name == r.commanderName)
        {
          found = &kv.second;
          break;
        }
      if (found != nullptr)
        FinishLoadedSpawn(found->endpoint, r.state);
    }
  }

  void GameServer::FinishLoadedSpawn(const Net::Endpoint& _ep,
                                     const std::optional<Neuron::Persist::PlayerPersistState>& _state)
  {
    const ECS::EntityId e = m_sessions.SpawnLoaded(m_world, _ep, m_tick);
    if (!m_world.IsValid(e))
      return;   // not (or no longer) a loading session

    // SpawnLoaded just spawned for this endpoint, so its session must exist; the
    // per-player record (name/score, C2) lives on it.
    auto sit = m_sessions.All().find(GameLogic::EndpointKey(_ep));
    if (sit == m_sessions.All().end())
      return;   // unreachable in practice
    GameLogic::Session& session = sit->second;

    if (_state.has_value())
    {
      // Returning commander: restore durable state (hull components + the
      // session's score record) and wake them docked at their last system (or
      // the nearest station / home if that system has none).
      GameLogic::PlayerStateApplyToComponents(m_world, e, *_state);
      session.score = _state->score;
      GameLogic::DockAtSystemOrNearest(m_world, e, _state->lastSystemId);
      printf("Client connected (loaded \"%s\"): entity %u\n", _state->commanderName.c_str(), e.index);
    }
    else if (m_persist)
    {
      // Unknown commander = account creation: place them docked at a system chosen
      // from their name (deterministic, scattered), THEN persist - so the saved
      // lastSystemId is where they were created and they wake there next time.
      GameLogic::DockAtNameChosenSystem(m_world, e, session.name);
      m_persist->QueuePlayerSnapshot(GameLogic::PlayerStateFromComponents(
          m_world, e, m_tick, session.name, session.score));
      printf("Client connected (new account): entity %u\n", e.index);
    }

    // Same connect side-effects as the immediate-spawn path: everyone learns the
    // joiner, and the joiner gets its full cargo manifest.
    for (const Msg::PlayerInfo& pi : m_sessions.Roster(m_world))
      m_sessions.Broadcast(pi);
    SendCargoTo(e.index);
  }

  void GameServer::LogCommand(const GameLogic::Session& _s, const Net::ReliableMessage& _msg)
  {
    if (!m_persist)
      return;
    Neuron::Persist::CommandLogEntry e;
    e.worldTick = m_tick;
    e.playerId = static_cast<int32_t>(_s.playerId);   // the real player identity (C)
    e.messageId = static_cast<int32_t>(_msg.type);
    e.payload = _msg.payload;   // the message's generic-codec encoding (no re-encode)
    m_persist->QueueCommand(e);
  }

  void GameServer::SavePlayers()
  {
    if (!m_persist)
      return;

    // Snapshot every LIVE player (a loading session has no entity and no durable
    // state to save yet), enqueuing only those whose durable fields changed since
    // the last accepted snapshot.
    for (auto& [key, s] : m_sessions.All())
    {
      if (!m_world.IsValid(s.entity))
        continue;
      Neuron::Persist::PlayerPersistState st = GameLogic::PlayerStateFromComponents(
          m_world, s.entity, m_tick, s.name, s.score);
      if (m_lastPersist.Changed(key, st))
        m_persist->QueuePlayerSnapshot(st);
    }

    // Lost-load recovery: re-request loads for any session still parked (a store
    // error dropped its earlier load); duplicate results are ignored downstream.
    for (const auto& kv : m_sessions.All())
      if (kv.second.loading && !kv.second.name.empty())
        m_persist->RequestLoad(kv.second.name);

    m_lastPersist.Prune([this](uint64_t _key) { return m_sessions.All().count(_key) != 0; });
  }

  void GameServer::SaveMarkets()
  {
    if (!m_persist)
      return;

    // Snapshot each station's market; enqueue a system's rows only when its
    // stock/prices actually drifted since the last save (trades are rare relative
    // to the tick rate, so most cadences enqueue nothing).
    m_world.Each<GameLogic::ServerStation>([this](ECS::EntityId, GameLogic::ServerStation& _st)
    {
      MarketDigest digest;
      for (int c = 0; c < GameLogic::COMMODITY_COUNT; ++c)
      {
        digest.price[c] = _st.market[c].price;
        digest.stock[c] = _st.market[c].quantity;
      }
      if (!m_lastMarket.Changed(_st.systemId, digest))
        return;

      std::vector<Neuron::Persist::MarketRow> rows;
      rows.reserve(GameLogic::COMMODITY_COUNT);
      for (int c = 0; c < GameLogic::COMMODITY_COUNT; ++c)
        rows.push_back(Neuron::Persist::MarketRow{ _st.systemId, c,
                                                   _st.market[c].quantity, _st.market[c].price, m_tick });
      m_persist->QueueMarketRows(rows);
    });
  }

  void GameServer::HandleStationRequest(GameLogic::Session& _session, const Net::StationRequest& _req)
  {
    // F1: the escort is a purchased UNIT, not a fitted upgrade, so the pure equip
    // dispatcher (which only knows the Equipment booleans) can't handle it - it
    // needs to spawn an entity + grant ownership. Intercept that one case here where
    // the world + session (playerId, ownership) are in reach.
    if (_req.kind == Net::StationRequestKind::Equip
        && _req.commodity == static_cast<uint16_t>(Net::EquipItem::EscortFighter))
    {
      HandleBuyEscort(_session);
      return;
    }

    // Docking + commerce only. The retired travel kinds (Teleport/JumpDrive)
    // fall through to the station dispatcher, which rejects them - travel rides
    // TravelRequest (HandleTravelRequest) since the protocol split.
    const Net::StationResponse resp =
        GameLogic::ProcessStationRequest(m_world, _session.entity, Cfg::DOCK_RANGE, _req);
    _session.events.Send(resp);   // Gameplay lane
  }

  // F1: buy an escort. Validate docking + the per-player cap + credits in pure
  // GameLogic (BuyEscort, which charges on success), then spawn the Viper escort and
  // grant ownership so it reaps with the session and counts in "all my units". The
  // reply reuses the Equip StationResponse the client already understands.
  void GameServer::HandleBuyEscort(GameLogic::Session& _session)
  {
    Net::StationResponse resp;
    resp.kind = Net::StationRequestKind::Equip;
    resp.commodity = static_cast<uint16_t>(Net::EquipItem::EscortFighter);

    GameLogic::Wallet* wallet = m_world.IsValid(_session.entity)
                              ? m_world.TryGet<GameLogic::Wallet>(_session.entity) : nullptr;
    GameLogic::DockState* dock = m_world.IsValid(_session.entity)
                              ? m_world.TryGet<GameLogic::DockState>(_session.entity) : nullptr;
    if (wallet == nullptr || dock == nullptr)
    {
      resp.status = Net::StationStatus::BadCommodity;   // not a commerce-capable ship
      _session.events.Send(resp);
      return;
    }

    // Escorts already owned = every owned entity minus the primary ship.
    const int owned = static_cast<int>(m_sessions.Ownership().OwnedCount(_session.playerId));
    const int currentEscorts = owned > 0 ? owned - 1 : 0;

    const GameLogic::EquipResult r =
        GameLogic::BuyEscort(*wallet, *dock, currentEscorts, Cfg::MAX_ESCORTS);
    resp.status = r.status;
    resp.credits = r.credits;

    if (r.status == Net::StationStatus::Ok)
    {
      const ECS::EntityId esc = GameLogic::SpawnEscort(m_world, _session.entity, currentEscorts);
      m_sessions.GrantOwnership(m_world, _session.playerId, esc);
      printf("[tick %u] player %u bought escort %u (now %d escorts)\n",
             m_tick, _session.playerId, esc.index, currentEscorts + 1);
    }

    _session.events.Send(resp);   // Gameplay lane
  }

  void GameServer::HandleTravelRequest(GameLogic::Session& _session, const Msg::TravelRequest& _req)
  {
    Msg::TravelResponse resp;
    resp.kind = _req.kind;

    if (_req.kind == Msg::TravelKind::Hyperspace)
    {
      // G7: a fuel-gated hyperspace jump (may misfire into witchspace).
      const GameLogic::HyperspaceOutcome hj =
          GameLogic::Hyperspace(m_world, _session.entity, _req.systemId, m_hyperRng);
      resp.status = hj.status;
      if (hj.wantedChanged)
        BroadcastPlayerInfo(_session.entity.index);
      if (hj.jumped)
        printf("[tick %u] player %u hyperspace -> system %u (%s)\n", m_tick, _session.entity.index,
               _req.systemId, hj.witchspace ? "WITCHSPACE misjump" : "arrived");
    }
    else if (_req.kind == Msg::TravelKind::InSystemJump)
    {
      // G7: in-system fast jump toward the planet (mass-lock gated).
      const GameLogic::JumpDriveOutcome jd = GameLogic::InSystemJump(m_world, _session.entity);
      resp.status = jd.status;
    }
    else if (_req.kind == Msg::TravelKind::PoiJump)
    {
      // scene.md 3.6: targeted in-system jump to a scene POI. Resolve the anchor
      // from the request's poiId via the scene index; JumpToPoi validates it is
      // system-local + not mass-locked, then places the hull off the anchor.
      const ECS::EntityId anchor = m_sceneIndex.Anchor(_req.poiId);
      const GameLogic::JumpDriveOutcome jd = GameLogic::JumpToPoi(m_world, _session.entity, anchor, m_tick);
      resp.status = jd.status;
    }

    _session.events.Send(resp);   // Gameplay lane
  }

  void GameServer::HandleSceneChunkRequest(GameLogic::Session& _session, const Msg::SceneChunkRequest& _req)
  {
    // scene.md 3.9: reply with the requested system's POIs (anchors + kinds +
    // positions) so the client can list local areas and target a POI jump. Cold
    // chart data on the Bulk lane; a system is small (a handful of POIs), so one
    // chunk suffices.
    Msg::SceneChunk chunk;
    chunk.systemId = _req.systemId;
    const auto it = m_sceneIndex.bySystem.find(static_cast<int32_t>(_req.systemId));
    if (it != m_sceneIndex.bySystem.end())
      for (const ECS::EntityId anchor : it->second)
        if (const GameLogic::ScenePoi* poi = m_world.TryGet<GameLogic::ScenePoi>(anchor))
          if (const GameLogic::WorldTransform* t = m_world.TryGet<GameLogic::WorldTransform>(anchor))
            chunk.pois.push_back(Msg::ScenePoiEntry{
                poi->poiId, static_cast<uint8_t>(poi->kind),
                t->position.x, t->position.y, t->position.z, poi->radius });
    _session.events.Send(chunk);   // Bulk lane
  }

  void GameServer::HandleUnitOrder(GameLogic::Session& _session, const Msg::UnitOrder& _req)
  {
    Msg::UnitOrderAck ack;
    ack.unitId = _req.unitId;
    ack.order = _req.order;

    // Validate + build the order in pure GameLogic (ownership, docked gating, target
    // type, Move clamp - the anti-cheat matrix, unit-tested headlessly).
    const GameLogic::OrderPlan plan =
        GameLogic::PlanUnitOrder(m_world, _session.playerId, _req, Cfg::ORDER_MAX_MOVE_DIST);
    ack.status = plan.status;

    if (plan.status == Msg::OrderStatus::Accepted)
    {
      // Crime at ORDER time (interaction.md): committing an Attack on a PROTECTED
      // victim (station/police/trader/clean player) makes you wanted the moment you
      // order it - even if the target dodges - closing the "order the hit, dodge the
      // blame" loophole. Attributed to the OWNER's own ship (_session.entity), not the
      // ordered unit: F1 makes ordering an ESCORT to attack a protected victim make
      // YOU wanted, not the drone - true owner attribution (I1 noted this follow-up).
      // Fire-time flagging still applies on the shots the player's own ship lands.
      if (_req.order == Msg::OrderKind::Attack)
      {
        const ECS::EntityId tgt = m_world.LiveEntity(_req.target);
        if (const GameLogic::Combatant* tc = m_world.TryGet<GameLogic::Combatant>(tgt))
          GameLogic::FlagIfCrime(m_world, m_bus, _session.entity, tgt, tc->team);
        m_bus.Dispatch();   // publish the Crime fact (police dispatch + roster refresh)
      }

      // Record the order (Add upserts, so latest order wins).
      m_world.Add<GameLogic::ActiveOrder>(plan.unit, plan.order);
    }

    _session.events.Send(ack);
  }

  void GameServer::HandleAbilityRequest(GameLogic::Session& _session, const Msg::AbilityRequest& _req)
  {
    if (!m_world.IsValid(_session.entity))
      return;
    // A discrete equipment activation on the reliable lane - the same FireWeapon
    // commands OnInputPacket used to synthesise from the unreliable input flags, now
    // driven by an undroppable request. Resolution (ownership/energy/cooldown/dock
    // gating + crime) stays the combat bus subscriber.
    switch (_req.kind)
    {
      case Msg::AbilityKind::FireMissile:
        m_bus.Publish(GameLogic::FireWeapon{ _session.entity, GameLogic::Weapon::Missile, _req.target });
        break;
      case Msg::AbilityKind::Ecm:
        m_bus.Publish(GameLogic::FireWeapon{ _session.entity, GameLogic::Weapon::Ecm, Msg::NO_MISSILE_TARGET });
        break;
      case Msg::AbilityKind::EnergyBomb:
        m_bus.Publish(GameLogic::FireWeapon{ _session.entity, GameLogic::Weapon::EnergyBomb, Msg::NO_MISSILE_TARGET });
        break;
      case Msg::AbilityKind::EscapePod:
        m_bus.Publish(GameLogic::FireWeapon{ _session.entity, GameLogic::Weapon::EscapePod, Msg::NO_MISSILE_TARGET });
        break;
    }
  }

  void GameServer::CompleteDockOrders()
  {
    // A ship carrying a Dock order that has reached dock range docks now - reusing
    // the tested station path so the response (and the client's docked flip) are
    // identical to a manual dock - and the order is cleared.
    std::vector<uint32_t> docked;
    m_world.Each<GameLogic::ActiveOrder>([&](ECS::EntityId _id, GameLogic::ActiveOrder& _o)
    {
      if (_o.order != Msg::OrderKind::Dock)
        return;
      const GameLogic::WorldTransform* t = m_world.TryGet<GameLogic::WorldTransform>(_id);
      if (t == nullptr)
        return;
      const ECS::EntityId station = GameLogic::NearestStation(m_world, t->position, Cfg::DOCK_RANGE);
      if (station.index != ECS::INVALID_INDEX)
        docked.push_back(_id.index);
    });

    for (uint32_t idx : docked)
    {
      // Route through the owning session so the StationResponse lands on the right
      // client's reliable lane; issue the same Dock request a manual dock sends.
      for (auto& entry : m_sessions.All())
        if (entry.second.entity.index == idx)
        {
          HandleStationRequest(entry.second, Net::StationRequest{ Net::StationRequestKind::Dock, 0, 0 });
          break;
        }
      if (const ECS::EntityId u = m_world.LiveEntity(idx); m_world.IsValid(u))
        m_world.Remove<GameLogic::ActiveOrder>(u);   // order fulfilled
    }
  }

  // --- simulation --------------------------------------------------------------

  void GameServer::AdvanceSimulation()
  {
    // Safe-park (B3): a live client that has gone silent (disconnect / reconnect
    // gap) has its flight intent zeroed so its ship stops coasting on stale input
    // instead of flying off during the grace window. A resumed client's next input
    // overrides this immediately.
    m_sessions.SafeParkSilent(m_world, m_tick, Cfg::SESSION_PARK_TICKS);

    // I1: translate each unit's active order into a flight intent BEFORE StepAi (an
    // order is the player's "input" now that piloting is retired), then drive ordered
    // laser fire through the SAME lag-compensated, crime-attributing player-fire path
    // a held trigger used - so an Attack order fires exactly as manual fire did (heat
    // gating throttles the cadence). Resolved pre-Tick, like input fire.
    for (const ECS::EntityId shooter : GameLogic::StepOrders(m_world))
      m_bus.Publish(GameLogic::FireWeapon{ shooter, GameLogic::Weapon::Laser, Msg::NO_MISSILE_TARGET });
    m_bus.Dispatch();

    // scene.md: the Mine order flies its unit onto a rock and runs the beam cycle
    // (StepOrders skips Mine so the two don't fight over the FlightIntent). Each
    // extraction broadcasts a MiningTick cue and resends the miner's cargo manifest
    // (the hold already changed authoritatively). Runs pre-Tick, like StepOrders.
    for (const GameLogic::MiningEvent& ev : GameLogic::StepMining(m_world, m_tick))
    {
      m_sessions.Broadcast(Msg::MiningTick{ ev.unitIndex, ev.rockIndex, ev.commodity, ev.units });
      SendCargoTo(ev.unitIndex);
    }

    // NPC tactics decide their flight intents (pursue/break-off/flee + panic
    // missiles), then the simulation advances one tick - the same
    // intent->caps->flight path a client's input takes. Fled ships despawn
    // inside StepAi; their removal rides the despawn diff.
    GameLogic::StepAi(m_world, m_tick, m_aiRng, m_scratch);
    GameLogic::Tick(m_world);
    ++m_tick;

    // scene.md 3.7b: belt pools drift back toward baseline on a slow, staggered
    // cadence (self-gated on SCENE_REGEN_INTERVAL), repopulating mined-out rocks.
    GameLogic::StepSceneRegen(m_world, m_tick);

    // I1: a ship carrying a Dock order that has now reached dock range docks (via the
    // tested station path) and drops the order.
    CompleteDockOrders();
    m_spawner.Step(m_world, m_tick);
    m_spawner.StepTraders(m_world, m_tick);   // ambient station <-> planet traffic

    // Regenerate players' shields/energy on a slow cadence (before combat, so a
    // hit this tick lands on the freshly-regenerated shield).
    if (m_tick % Cfg::SHIELD_REGEN_INTERVAL == 0)
      GameLogic::StepShieldRegen(m_world);

    // G8: lasers cool and ECM units recharge, one step per tick.
    GameLogic::StepEquipment(m_world);
  }

  void GameServer::ResolveKills()
  {
    // Missiles (homing + detonation), realtime combat and collision grinding
    // (G6: ramming, station scrapes, planet impacts). Each kill is an
    // EntityKilled fact; the subscriber respawns a player or broadcasts the
    // death and destroys the wreck (whose removal also rides the despawn diff).
    // A victim reported by two systems in one tick is fine - the death handler
    // skips already-resolved entities.
    // G8: a missile homing on an ECM-fitted target can be jammed mid-flight;
    // every jam is broadcast as the classic ECM cue.
    std::vector<uint32_t>& ecmPulses = m_scratch.ecmPulses;
    ecmPulses.clear();   // StepMissiles only appends; a reused buffer must start empty
    std::vector<GameLogic::Kill> kills = GameLogic::StepMissiles(m_world, m_aiRng, ecmPulses, m_scratch);
    for (uint32_t defender : ecmPulses)
      m_sessions.Broadcast(Msg::EcmPulse{ defender });
    for (const GameLogic::Kill& k : GameLogic::StepCombat(m_world, &m_candidatePairsThisTick, m_scratch))
      kills.push_back(k);
    for (const GameLogic::Kill& k : GameLogic::StepCollisions(m_world, &m_candidatePairsThisTick, m_scratch))
      kills.push_back(k);
    // G4: sun-proximity cabin heat cooks a hull held at the maximum; a scoop-fitted
    // ship skimming the band tops its fuel instead. Deaths join the same pipeline.
    for (const GameLogic::Kill& k : GameLogic::StepCabinHeat(m_world))
      kills.push_back(k);
    for (const GameLogic::Kill& kill : kills)
      m_bus.Publish(GameLogic::EntityKilled{ kill.victim, kill.killer });
    m_bus.Dispatch();
  }

  void GameServer::LootAndScoop()
  {
    // Age cargo canisters (despawning the expired) and let players scoop the
    // ones they fly into; a player whose hold changed gets a fresh cargo
    // manifest. Both the expired and the scooped canisters ride the despawn diff.
    GameLogic::StepLoot(m_world, m_scratch);
    for (uint32_t scoopedBy : GameLogic::ScoopSystem(m_world, m_scratch))
      SendCargoTo(scoopedBy);
  }

  void GameServer::DecayWantedRecords()
  {
    // Periodically cool down wanted records; refresh the roster for anyone whose
    // legal status actually changed.
    if (m_tick % Cfg::WANTED_DECAY_INTERVAL != 0)
      return;
    for (uint32_t changedId : GameLogic::DecayWanted(m_world))
      BroadcastPlayerInfo(changedId);
  }

  void GameServer::ReapAndDespawn()
  {
    // Reap idle clients, then broadcast every despawn (reaped players + props)
    // as a reliable event to all remaining clients.
    m_sessions.Reap(m_world, m_tick, Cfg::SESSION_TIMEOUT_TICKS, Cfg::SESSION_GRACE_TICKS);
    CurrentIds(m_world, m_currentIdsScratch);
    for (uint32_t goneId : m_despawns.Update(m_currentIdsScratch))
      m_sessions.Broadcast(Msg::EntityDespawn{ goneId });
  }

  // --- send ---------------------------------------------------------------------

  void GameServer::PublishState()
  {
    m_aoi.Rebuild(m_world);
    for (auto& [key, s] : m_sessions.All())
    {
      // A pending (pre-hello) shell has no entity: don't stream it world state,
      // only flush its control lane (the HelloAck/HelloReject + acks) below.
      if (m_world.IsValid(s.entity))
      {
        const Math::Vector3i64 viewerPos = m_world.Get<GameLogic::WorldTransform>(s.entity).position;

        Net::WorldSnapshot snap =
            m_aoi.SnapshotFor(m_world, m_tick, viewerPos, Cfg::AOI_RADIUS_CELLS, s.entity.index);
        // Keep the local system's planet/station visible across the whole system,
        // not just the +/-1 ship cell, so the body you fly toward never pops out.
        AppendLandmarks(m_world, snap, viewerPos, m_landmarks, m_landmarkPresentScratch);
        // Send positions as int32 offsets from the viewer (E2): every entity is
        // within the AOI, so the offset always fits int32 while absolute positions
        // stay unbounded int64.
        snap.refX = viewerPos.x;
        snap.refY = viewerPos.y;
        snap.refZ = viewerPos.z;
        // Cap this viewer's per-tick state (E2c): if the AOI is overloaded, keep the
        // entities closest to the viewer and shed the farthest (they update on a
        // later tick). Trimmed BEFORE delta-encoding so baseline and current agree.
        m_droppedThisTick += Net::TrimSnapshotToBudget(
            snap.entities, viewerPos.x, viewerPos.y, viewerPos.z, Net::SnapshotEntityBudget());
        // Delta-encode against the baseline the client last acknowledged (E2b): a
        // small delta most ticks, a full keyframe periodically or when no baseline
        // is held. Falls back to a full for a crowded (multi-datagram) AOI.
        for (const std::vector<uint8_t>& datagram : s.snapshotEncoder.Encode(snap, s.ackedSnapshotTick))
        {
          m_socket.SendTo(s.endpoint, datagram.data(), datagram.size());
          m_bytesThisTick += datagram.size();   // D3 metrics
        }

        // The owner's private HUD vitals, on change only. Queued on the Gameplay
        // lane; flushed with the events below.
        Msg::PlayerStatus ps;
        if (const auto* c = m_world.TryGet<GameLogic::Combatant>(s.entity)) ps.energy = c->energy;
        if (const auto* sh = m_world.TryGet<GameLogic::Shields>(s.entity)) { ps.frontShield = sh->front; ps.aftShield = sh->aft; }
        if (const auto* fu = m_world.TryGet<GameLogic::Fuel>(s.entity)) ps.fuel = fu->tenths;
        if (const auto* wal = m_world.TryGet<GameLogic::Wallet>(s.entity)) ps.credits = wal->credits;
        if (const auto* eq = m_world.TryGet<GameLogic::Equipment>(s.entity)) ps.missiles = eq->missiles;
        if (const auto* h = m_world.TryGet<GameLogic::CargoHold>(s.entity)) ps.cargoUsed = GameLogic::TotalTonnage(*h);
        if (const auto* wnt = m_world.TryGet<GameLogic::Wanted>(s.entity)) ps.wantedLevel = wnt->level;
        ps.score = s.score;   // the session's per-player record (C2)
        if (const auto* g = m_world.TryGet<GameLogic::ShipGear>(s.entity)) ps.laserTemp = g->laserHeat;
        if (const auto* ch = m_world.TryGet<GameLogic::CabinHeat>(s.entity)) ps.cabinTemp = ch->temp;   // G4
        if (m_lastStatus.Changed(key, ps))
          s.events.Send(ps);

        // Strategic tier (E3): a slow per-system rollup for the chart, queued on the
        // reliable Gameplay lane at the strategic cadence and flushed with the events
        // below - decoupled from the tactical snapshot clock (§12).
        if (m_tick % Cfg::STRATEGIC_INTERVAL == 0)
          PublishStrategicFor(s);
      }

      for (const std::vector<uint8_t>& dg : s.events.WriteDatagrams())
      {
        m_socket.SendTo(s.endpoint, dg.data(), dg.size());
        m_bytesThisTick += dg.size();   // D3 metrics
      }
    }

    // Drop cached status for endpoints that are no longer sessions (reaped
    // clients), so the change-cache can't grow without bound over a long uptime.
    m_lastStatus.Prune([this](uint64_t _key) { return m_sessions.All().count(_key) != 0; });
  }

  void GameServer::PublishAdmin()
  {
    if (m_adminSocket == nullptr || !m_admin.Enabled())
      return;

    // Roster: build each real player's row, push it only when a displayed field
    // changed (the on-change cache), and fold join/leave into the same diff so those
    // feed lines never need a tap at the connect/reap sites.
    std::unordered_set<uint32_t> current;
    for (auto& [key, s] : m_sessions.All())
    {
      if (s.playerId == 0)
        continue;   // a pending/loading shell is not a roster player yet

      current.insert(s.playerId);
      const bool isNew = (m_adminRosterIds.count(s.playerId) == 0);
      if (isNew)
        m_adminJoinTick[s.playerId] = m_tick;

      Msg::AdminPlayerInfo row;
      row.playerId = s.playerId;
      row.entityId = s.entity.index;
      row.name = s.name;
      row.address = s.endpoint.address;
      row.port = s.endpoint.port;
      row.rttMs = s.rttMs;
      row.score = s.score;
      row.state = s.loading ? static_cast<uint8_t>(Msg::AdminPlayerState::Loading)
                : (s.Live()  ? static_cast<uint8_t>(Msg::AdminPlayerState::Live)
                             : static_cast<uint8_t>(Msg::AdminPlayerState::Pending));
      row.connectedTick = m_adminJoinTick[s.playerId];

      if (isNew)
        AdminNote(Msg::AdminEventKind::PlayerJoined, s.playerId,
                  std::string("commander ") + s.name + " joined");
      if (m_adminRoster.Changed(s.playerId, row))
        m_admin.PushRoster(row);
    }

    // Leaves: a playerId that was in the roster last time but isn't now has gone.
    for (uint32_t oldId : m_adminRosterIds)
      if (current.count(oldId) == 0)
      {
        m_admin.PushPlayerGone(oldId, Msg::AdminGoneReason::Disconnected);
        AdminNote(Msg::AdminEventKind::PlayerLeft, oldId, "a commander left");
        m_adminRoster.Forget(oldId);
        m_adminJoinTick.erase(oldId);
      }
    m_adminRosterIds.swap(current);

    // Health: a rolling sample at ~1 Hz. The window-so-far is read WITHOUT resetting
    // m_metrics (the D3 console line owns the reset), so avg/max smooth over the
    // current window and overruns are the lifetime count.
    if (m_tick % Cfg::ADMIN_HEALTH_INTERVAL == 0)
    {
      const double windowSec = (QpcMs() - m_metricsWindowStartMs) / 1000.0;
      const Server::TickSummary s = m_metrics.Snapshot(windowSec > 0.0 ? windowSec : 1.0);
      Msg::AdminHealth h;
      h.tick = m_tick;
      h.uptimeSeconds = static_cast<uint32_t>(static_cast<uint64_t>(m_tick) * Cfg::TICK_SLEEP_MS / 1000);
      h.avgTickMs = static_cast<float>(s.avgMs);
      h.maxTickMs = static_cast<float>(s.maxMs);
      h.overruns = static_cast<uint32_t>(m_lifetimeOverruns);
      h.entities = static_cast<uint32_t>(m_world.AliveCount());
      h.sessions = static_cast<uint32_t>(m_sessions.Count());
      h.bytesPerSecond = s.bytesPerSecond;
      h.droppedEntities = s.droppedEntities;
      m_admin.PushHealth(h);
    }

    // Flush the admin socket. Server datagrams carry token 0 (the manager trusts the
    // server by address). Reaping of silent managers happens inside WriteDatagrams.
    std::vector<std::pair<Net::Endpoint, std::vector<uint8_t>>> out;
    m_admin.WriteDatagrams(m_tick, out);
    for (const auto& [ep, dg] : out)
    {
      m_adminSocket->SendTo(ep, dg.data(), dg.size());
      m_bytesThisTick += dg.size();   // count admin bytes in the D3 total too
    }
  }

  void GameServer::PublishStrategicFor(GameLogic::Session& _s)
  {
    // The viewer's current system = the station nearest their ship (works docked or
    // in flight); its position anchors the per-system rollup. Chebyshev distance
    // avoids squaring huge absolute coordinates.
    const Math::Vector3i64 pos = m_world.Get<GameLogic::WorldTransform>(_s.entity).position;
    int systemId = -1;
    Math::Vector3i64 center = pos;
    int64_t best = -1;
    m_world.Each<GameLogic::ServerStation, GameLogic::WorldTransform>(
        [&](ECS::EntityId, GameLogic::ServerStation& _st, GameLogic::WorldTransform& _t)
    {
      const int64_t dx = _t.position.x - pos.x;
      const int64_t dy = _t.position.y - pos.y;
      const int64_t dz = _t.position.z - pos.z;
      const int64_t ax = dx < 0 ? -dx : dx;
      const int64_t ay = dy < 0 ? -dy : dy;
      const int64_t az = dz < 0 ? -dz : dz;
      int64_t cheb = ax;
      if (ay > cheb) cheb = ay;
      if (az > cheb) cheb = az;
      if (best < 0 || cheb < best)
      {
        best = cheb;
        systemId = _st.systemId;
        center = _t.position;
      }
    });

    const GameLogic::StrategicCounts counts = GameLogic::SummarizeStrategic(m_world, center);
    Msg::StrategicSummary summary;
    summary.systemId = static_cast<uint32_t>(systemId);
    summary.friendlyCount = counts.friendly;
    summary.hostileCount = counts.hostile;
    summary.alert = static_cast<uint8_t>(counts.hostile > 0 ? Msg::StrategicAlert::UnderAttack
                                                            : Msg::StrategicAlert::None);
    _s.events.Send(summary);   // reliable Gameplay lane
  }

  // --- combat subscribers ---------------------------------------------------------

  void GameServer::RegisterSubscribers()
  {
    // A FireWeapon command resolves against the authoritative world (laser
    // geometry / missile spawn unchanged), publishing the resulting facts.
    m_bus.Subscribe<GameLogic::FireWeapon>([this](const GameLogic::FireWeapon& _fw)
    {
      // Lag-compensate the laser (E1): rewind targets by the shooter's own latency
      // (rtt/2 + render interpolation delay), clamped to the history window. A
      // non-session shooter reports 0 rtt -> minimal (interp-delay) rewind.
      const uint32_t ticksBack = GameLogic::LagCompTicks(m_sessions.RttForEntity(_fw.shooter.index));
      GameLogic::ResolveFireWeapon(m_world, m_bus, _fw, Cfg::FIRE_RANGE, Cfg::AIM_CONE,
                                   &m_combatHistory, ticksBack);
    });

    m_bus.Subscribe<GameLogic::Crime>([this](const GameLogic::Crime& _c) { OnCrime(_c); });
    m_bus.Subscribe<GameLogic::EntityKilled>([this](const GameLogic::EntityKilled& _k) { OnEntityKilled(_k); });

    // G8 equipment facts -> wire cues. An ECM burst is a public event (everyone
    // hears the classic buzz); a pod ejection is the owner's business - it also
    // cleared their record and emptied their hold, so refresh both mirrors.
    m_bus.Subscribe<GameLogic::EcmFired>([this](const GameLogic::EcmFired& _e)
    {
      m_sessions.Broadcast(Msg::EcmPulse{ _e.ship });
    });
    m_bus.Subscribe<GameLogic::PodEjected>([this](const GameLogic::PodEjected& _p)
    {
      for (auto& entry : m_sessions.All())
        if (entry.second.entity.index == _p.ship)
        {
          entry.second.events.Send(Msg::EscapePodUsed{ _p.ship });
          break;
        }
      BroadcastPlayerInfo(_p.ship);   // the record was cleared
      SendCargoTo(_p.ship);           // the hold went down with the ship
      printf("[tick %u] player %u ejected (escape pod) -> docked\n", m_tick, _p.ship);
    });
  }

  void GameServer::OnCrime(const GameLogic::Crime& _c)
  {
    // The offender's wanted level just changed - refresh their roster entry.
    BroadcastPlayerInfo(_c.offender.index);

    if (!_c.firstOffence || !m_world.IsValid(_c.offender))
      return;
    AdminNote(Msg::AdminEventKind::Crime, _c.offender.index,
              "a commander turned wanted; police dispatched");
    const GameLogic::WorldTransform* t = m_world.TryGet<GameLogic::WorldTransform>(_c.offender);
    if (t == nullptr)
      return;
    // The law launches from the system's station when one is in reach (legacy
    // stations launched their own Vipers); deep-space crime still gets the
    // spawn-near-offender fallback. Either way the Vipers carry a warrant -
    // they are FIXED on this offender (stage 4 target memory).
    Math::Vector3i64 launchPos = t->position;
    const ECS::EntityId station = GameLogic::NearestStation(m_world, t->position, Cfg::LANDMARK_VIS_DIST);
    if (station.index != ECS::INVALID_INDEX)
      if (const auto* st = m_world.TryGet<GameLogic::WorldTransform>(station))
        launchPos = st->position + Math::Vector3i64{ 0, 0, GameLogic::LAUNCH_OFFSET };
    m_spawner.SpawnPolice(m_world, launchPos, 2, _c.offender.index);
    printf("[tick %u] CRIME: player %u fired on team %d -> police dispatched (%s)\n",
           m_tick, _c.offender.index, _c.victimTeam,
           station.index != ECS::INVALID_INDEX ? "station launch" : "deep-space response");
  }

  void GameServer::OnEntityKilled(const GameLogic::EntityKilled& _k)
  {
    if (!m_world.IsValid(_k.victim))
      return;   // already resolved this tick (e.g. two shots on one target)

    if (m_world.TryGet<GameLogic::PlayerTag>(_k.victim) != nullptr)
    {
      // Tell the dying player it died so its client plays the game-over
      // sequence; we still respawn it below. Sent only to that one session, so
      // other clients don't briefly drop the (still-alive, respawned) ship.
      for (auto& entry : m_sessions.All())
        if (entry.second.entity.index == _k.victim.index)
        {
          entry.second.events.Send(Msg::EntityDeath{ _k.victim.index, _k.killer });
          AdminNote(Msg::AdminEventKind::Kill, entry.second.playerId,
                    std::string("commander ") + entry.second.name + " was destroyed");
          break;
        }

      // Pay the killer a wanted-derived bounty (if the victim was a fugitive)
      // BEFORE the record is wiped by the respawn below; a clean-player kill
      // pays nothing but still scores. The score delta routes to the killer's
      // session record (C2: score lives on the player, not the hull).
      const GameLogic::KillCredit credit = GameLogic::CreditKill(m_world, _k.killer, _k.victim);
      if (credit.score != 0)
        m_sessions.AddScore(_k.killer, credit.score);

      if (GameLogic::Combatant* c = m_world.TryGet<GameLogic::Combatant>(_k.victim))
      {
        c->energy = GameLogic::MAX_ENERGY;
        c->invulnTicks = GameLogic::RESPAWN_GRACE_TICKS;
      }
      if (GameLogic::Shields* sh = m_world.TryGet<GameLogic::Shields>(_k.victim))
      {
        sh->front = GameLogic::MAX_SHIELD;   // respawn with full shields
        sh->aft = GameLogic::MAX_SHIELD;
      }
      if (GameLogic::Wanted* wnt = m_world.TryGet<GameLogic::Wanted>(_k.victim))
        wnt->level = 0;

      // Death rule: respawn DOCKED at the nearest station, minus cargo (rather
      // than in place). The cargo the player was carrying first spills as
      // scoopable canisters at the wreck, THEN the hold is emptied by the
      // respawn. If no station is reachable, RespawnAtNearestStation falls back
      // to leaving them put.
      if (const GameLogic::WorldTransform* pt = m_world.TryGet<GameLogic::WorldTransform>(_k.victim))
      {
        // G1: broadcast the kill VFX at the death spot BEFORE the respawn teleports
        // the hull away. The victim only got a private EntityDeath (so nobody drops
        // its respawned ship); this world-anchored pop is how the KILLER and
        // bystanders finally see the kill.
        m_sessions.Broadcast(Msg::ExplosionAt{ pt->position.x, pt->position.y, pt->position.z, /*scale*/ 2 });
        GameLogic::DropPlayerCargo(m_world, _k.victim, pt->position, m_lootRng);
      }
      GameLogic::RespawnAtNearestStation(m_world, _k.victim);
      m_world.Remove<GameLogic::ActiveOrder>(_k.victim);   // I1: respawn clean of any standing order

      // Death wipes the wanted record - and every grudge: the player respawns
      // clean, so police warrants and pirate locks on them are torn up (the
      // entity index survives the respawn, so stale memories must be swept).
      m_world.Each<GameLogic::Combatant>([&_k](ECS::EntityId, GameLogic::Combatant& _cbt)
      {
        if (_cbt.focus == _k.victim.index)
          _cbt.focus = ECS::INVALID_INDEX;
      });

      // Refresh the roster so a respawned player shows as clean again on
      // everyone's screen; the hold is now empty - push the zeroed manifest so
      // the HUD matches.
      BroadcastPlayerInfo(_k.victim.index);
      SendCargoTo(_k.victim.index);

      // Rate-limit the respawn log so a player parked in combat doesn't spam
      // the console.
      if (m_tick - m_lastRespawnLogTick >= Cfg::RESPAWN_LOG_WINDOW)
      {
        if (m_suppressedRespawns > 0)
          printf("[tick %u] player %u respawned (killer %u; +%d more respawn(s) since last log)\n",
                 m_tick, _k.victim.index, _k.killer, m_suppressedRespawns);
        else
          printf("[tick %u] player %u was killed by %u -> respawned\n",
                 m_tick, _k.victim.index, _k.killer);
        m_lastRespawnLogTick = m_tick;
        m_suppressedRespawns = 0;
      }
      else
      {
        ++m_suppressedRespawns;
      }
      return;
    }

    // Pay the killer the wreck's bounty and scatter its cargo - but only for a
    // real combatant kill. A detonated missile is also reported here (so its
    // explosion shows), yet it carries no Combatant, so it neither pays out,
    // counts as a score, nor sheds loot.
    if (m_world.Has<GameLogic::Combatant>(_k.victim))
    {
      const GameLogic::KillCredit credit = GameLogic::CreditKill(m_world, _k.killer, _k.victim);
      if (credit.score != 0)
        m_sessions.AddScore(_k.killer, credit.score);   // score is a player record (C2)
      GameLogic::DropLoot(m_world, _k.victim, m_lootRng);   // legacy launch_loot: alloy + cargo canisters
    }

    m_sessions.Broadcast(Msg::EntityDeath{ _k.victim.index, _k.killer });
    m_world.Destroy(_k.victim);
    printf("[tick %u] entity %u destroyed by %u\n", m_tick, _k.victim.index, _k.killer);
  }

  // --- helpers --------------------------------------------------------------------

  void GameServer::SendCargoTo(uint32_t _entityIndex)
  {
    // Push a player their full per-commodity cargo hold. The aggregate
    // PlayerStatus.cargoUsed can't convey the per-good breakdown the HUD tracks,
    // so a scoop (or a respawn that emptied the hold) resends the whole manifest
    // to just that one session on its Gameplay lane.
    for (auto& entry : m_sessions.All())
      if (entry.second.entity.index == _entityIndex)
      {
        if (const GameLogic::CargoHold* h = m_world.TryGet<GameLogic::CargoHold>(entry.second.entity))
        {
          Msg::CargoManifest cm;
          cm.units.assign(h->units, h->units + GameLogic::COMMODITY_COUNT);
          entry.second.events.Send(cm);
        }
        break;
      }
  }

  void GameServer::BroadcastPlayerInfo(uint32_t _entityIndex)
  {
    if (Msg::PlayerInfo pi; m_sessions.PlayerInfoFor(m_world, _entityIndex, pi))
      m_sessions.Broadcast(pi);
  }
}
