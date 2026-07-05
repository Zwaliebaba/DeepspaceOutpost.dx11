#include <gtest/gtest.h>

#include <memory>
#include <string>

#include "GameLogic.h"              // ServerSessions (spawns a full player entity), components
#include "PlayerPersistence.h"      // FromComponents / ApplyToComponents converters
#include "StationProtocol.h"        // Net::StationRequest / StationRequestKind (replay test)
#include "Messages/Serialize.h"     // Msg::Encode / Msg::Decode / Msg::Raw
#include "GalaxyGen.h"              // GenerateSystem / ToManifestEntry (v2 parity)
#include "GalaxyRows.h"             // BuildSystemRows / BaselineMarketRows / ManifestEntryFrom (Server-side, header-only)

#include "PlayerPersistState.h"
#include "PersistenceStore.h"
#include "PersistenceService.h"

using namespace Neuron;

namespace
{
  // Connect a player the production way (a valid hello spawns a full player entity
  // with every durable component) and return it.
  ECS::EntityId SpawnPlayer(ECS::Registry& _world, GameLogic::ServerSessions& _sessions,
                            uint16_t _port, const std::string& _name)
  {
    Msg::ClientHello h;
    h.protocolVersion = Msg::PROTOCOL_VERSION;
    h.commanderName = _name;
    return _sessions.OnHello(_world, Net::Endpoint{ 0x7F000001, _port }, h, /*tick*/ 1).entity;
  }

  Persist::PlayerPersistState State(const std::string& _name, int32_t _credits)
  {
    Persist::PlayerPersistState s;
    s.commanderName = _name;
    s.credits = _credits;
    return s;
  }
}

// --- converters ---------------------------------------------------------------

TEST(Persistence, ComponentRoundTripPreservesDurableState)
{
  ECS::Registry world;
  GameLogic::ServerSessions sessions;
  ECS::EntityId e = SpawnPlayer(world, sessions, 5001, "Jameson");
  ASSERT_TRUE(world.IsValid(e));

  // Give the player some earned state.
  world.Get<GameLogic::Wallet>(e).credits = 73210;
  world.Get<GameLogic::Fuel>(e).tenths = 55;
  world.Get<GameLogic::Wanted>(e).level = 3;
  world.Get<GameLogic::CargoHold>(e).capacity = 35;
  world.Get<GameLogic::CargoHold>(e).units[2] = 11;
  world.Get<GameLogic::CargoHold>(e).units[7] = 4;
  world.Get<GameLogic::Equipment>(e).missiles = 2;
  world.Get<GameLogic::Equipment>(e).largeCargoBay = true;
  world.Get<GameLogic::Equipment>(e).fuelScoop = true;
  world.Add<GameLogic::Witchspace>(e, GameLogic::Witchspace{});

  // Name and score are player-level records on the session (C2): the caller
  // passes them in; the converter packs only what the hull carries.
  const Persist::PlayerPersistState s =
      GameLogic::PlayerStateFromComponents(world, e, /*tick*/ 42, "Jameson", /*score*/ 4096);
  EXPECT_EQ(s.commanderName, "Jameson");
  EXPECT_EQ(s.credits, 73210);
  EXPECT_EQ(s.fuelTenths, 55);
  EXPECT_EQ(s.wantedLevel, 3);
  EXPECT_EQ(s.score, 4096);
  EXPECT_EQ(s.holdCapacity, 35);
  EXPECT_EQ(s.cargo[2], 11);
  EXPECT_EQ(s.cargo[7], 4);
  EXPECT_EQ(s.missiles, 2);
  EXPECT_TRUE((s.equipFlags & Persist::EQUIP_LARGE_CARGO_BAY) != 0);
  EXPECT_TRUE((s.equipFlags & Persist::EQUIP_FUEL_SCOOP) != 0);
  EXPECT_TRUE((s.equipFlags & Persist::EQUIP_ECM) == 0);
  EXPECT_TRUE(s.inWitchspace);
  EXPECT_EQ(s.updatedTick, 42u);

  // Trash the live components, then restore from the snapshot.
  world.Get<GameLogic::Wallet>(e).credits = 0;
  world.Get<GameLogic::CargoHold>(e).units[2] = 0;
  world.Get<GameLogic::Equipment>(e).largeCargoBay = false;
  world.Remove<GameLogic::Witchspace>(e);

  GameLogic::PlayerStateApplyToComponents(world, e, s);
  EXPECT_EQ(world.Get<GameLogic::Wallet>(e).credits, 73210);
  EXPECT_EQ(world.Get<GameLogic::Fuel>(e).tenths, 55);
  EXPECT_EQ(world.Get<GameLogic::CargoHold>(e).units[2], 11);
  EXPECT_EQ(world.Get<GameLogic::CargoHold>(e).capacity, 35);
  EXPECT_TRUE(world.Get<GameLogic::Equipment>(e).largeCargoBay);
  EXPECT_TRUE(world.Get<GameLogic::Equipment>(e).fuelScoop);
  EXPECT_EQ(world.Get<GameLogic::Equipment>(e).missiles, 2);
  EXPECT_TRUE(world.Has<GameLogic::Witchspace>(e));
}

TEST(Persistence, DockedLastSystemIsCapturedAndUndockedIsHome)
{
  ECS::Registry world;
  GameLogic::ServerSessions sessions;
  ECS::EntityId e = SpawnPlayer(world, sessions, 5002, "Docker");

  // Undocked (fresh spawn) -> home (-1).
  EXPECT_EQ(GameLogic::PlayerStateFromComponents(world, e, 1, "Docker", 0).lastSystemId, -1);

  // Dock at a station belonging to system 5.
  const ECS::EntityId station = world.Create();
  world.Add<GameLogic::ServerStation>(station, GameLogic::ServerStation{ /*systemId*/ 5, {} });
  world.Get<GameLogic::DockState>(e).docked = true;
  world.Get<GameLogic::DockState>(e).stationId = station.index;

  EXPECT_EQ(GameLogic::PlayerStateFromComponents(world, e, 1, "Docker", 0).lastSystemId, 5);
}

// --- the service (deterministic: no thread, manual FlushPendingOnce) ----------

TEST(Persistence, SnapshotsCoalesceLatestWins)
{
  auto store = std::make_unique<Persist::InMemoryStore>();
  Persist::InMemoryStore* raw = store.get();
  Persist::PersistenceService svc(std::move(store), /*startThread*/ false);

  svc.QueuePlayerSnapshot(State("X", 100));
  svc.QueuePlayerSnapshot(State("X", 200));
  svc.QueuePlayerSnapshot(State("X", 300));   // three queued, one pending (coalesced)

  const std::size_t upserts = svc.FlushPendingOnce();
  EXPECT_EQ(upserts, 1u);                      // exactly one write, not three
  EXPECT_EQ(raw->PlayerCount(), 1u);
  ASSERT_TRUE(raw->LoadPlayer("X").has_value());
  EXPECT_EQ(raw->LoadPlayer("X")->credits, 300);   // latest value survived
}

TEST(Persistence, CommandRingDropsOldestAndCountsDrops)
{
  auto store = std::make_unique<Persist::InMemoryStore>();
  Persist::InMemoryStore* raw = store.get();
  Persist::PersistenceService svc(std::move(store), /*startThread*/ false, /*ringCap*/ 3);

  for (uint64_t i = 1; i <= 5; ++i)
  {
    Persist::CommandLogEntry e;
    e.worldTick = i;
    e.playerId = 1;
    svc.QueueCommand(e);
  }
  svc.FlushPendingOnce();

  EXPECT_EQ(svc.DroppedCommands(), 2u);            // 5 queued, cap 3 -> 2 dropped
  ASSERT_EQ(raw->Commands().size(), 3u);
  EXPECT_EQ(raw->Commands()[0].worldTick, 3u);     // oldest kept is #3 (1,2 dropped)
  EXPECT_EQ(raw->Commands()[2].worldTick, 5u);
}

TEST(Persistence, LoadRequestsReturnStateOrNullopt)
{
  auto store = std::make_unique<Persist::InMemoryStore>();
  Persist::InMemoryStore* raw = store.get();
  raw->UpsertPlayer(State("Known", 999));
  Persist::PersistenceService svc(std::move(store), /*startThread*/ false);

  svc.RequestLoad("Known");
  svc.RequestLoad("Nobody");
  svc.FlushPendingOnce();

  std::vector<Persist::PlayerLoadResult> loads = svc.DrainLoads();
  ASSERT_EQ(loads.size(), 2u);
  EXPECT_EQ(loads[0].commanderName, "Known");
  ASSERT_TRUE(loads[0].state.has_value());
  EXPECT_EQ(loads[0].state->credits, 999);
  EXPECT_EQ(loads[1].commanderName, "Nobody");
  EXPECT_FALSE(loads[1].state.has_value());         // unknown -> fresh spawn upstream

  EXPECT_TRUE(svc.DrainLoads().empty());            // drained once
}

// --- end-to-end restart -------------------------------------------------------

TEST(Persistence, RestartRebuildsCommanderFromTheStore)
{
  Persist::InMemoryStore store;

  // Session 1: a commander earns some state, which is snapshotted to the store.
  {
    ECS::Registry world;
    GameLogic::ServerSessions sessions;
    ECS::EntityId e = SpawnPlayer(world, sessions, 6001, "Elite");
    world.Get<GameLogic::Wallet>(e).credits = 250000;
    world.Get<GameLogic::Fuel>(e).tenths = 12;
    world.Get<GameLogic::Wanted>(e).level = 6;
    world.Get<GameLogic::CargoHold>(e).units[4] = 9;
    world.Get<GameLogic::Equipment>(e).missiles = 4;
    world.Get<GameLogic::Equipment>(e).ecm = true;
    // Score is a session record (C2): the server passes it alongside the name.
    store.UpsertPlayer(GameLogic::PlayerStateFromComponents(world, e, /*tick*/ 500, "Elite", /*score*/ 9001));
  }

  // Session 2 ("server restart"): a fresh world + fresh spawn, restored from store.
  {
    ECS::Registry world;
    GameLogic::ServerSessions sessions;
    ECS::EntityId e = SpawnPlayer(world, sessions, 6001, "Elite");
    // Fresh spawn defaults differ from the saved state...
    EXPECT_NE(world.Get<GameLogic::Wallet>(e).credits, 250000);

    std::optional<Persist::PlayerPersistState> loaded = store.LoadPlayer("Elite");
    ASSERT_TRUE(loaded.has_value());
    GameLogic::PlayerStateApplyToComponents(world, e, *loaded);

    EXPECT_EQ(world.Get<GameLogic::Wallet>(e).credits, 250000);
    EXPECT_EQ(world.Get<GameLogic::Fuel>(e).tenths, 12);
    EXPECT_EQ(world.Get<GameLogic::Wanted>(e).level, 6);
    EXPECT_EQ(loaded->score, 9001);   // score rides the snapshot; the server stamps the session
    EXPECT_EQ(world.Get<GameLogic::CargoHold>(e).units[4], 9);
    EXPECT_EQ(world.Get<GameLogic::Equipment>(e).missiles, 4);
    EXPECT_TRUE(world.Get<GameLogic::Equipment>(e).ecm);
  }
}

// --- command log replay -------------------------------------------------------

namespace
{
  // A player near a stocked station (food at index 0), close enough to dock.
  ECS::EntityId BuildTradeWorld(ECS::Registry& _world)
  {
    const ECS::EntityId station = _world.Create();
    _world.Add<GameLogic::WorldTransform>(station, GameLogic::WorldTransform{ { 0, 0, 0 } });
    GameLogic::ServerStation st;
    st.market[0].price = 10;
    st.market[0].quantity = 50;
    _world.Add<GameLogic::ServerStation>(station, st);

    const ECS::EntityId player = _world.Create();
    _world.Add<GameLogic::WorldTransform>(player, GameLogic::WorldTransform{ { 100, 0, 0 } });
    _world.Add<GameLogic::Wallet>(player, GameLogic::Wallet{ 1000 });
    _world.Add<GameLogic::CargoHold>(player, GameLogic::CargoHold{});
    _world.Add<GameLogic::DockState>(player, GameLogic::DockState{});
    return player;
  }
}

TEST(Persistence, CommandLogReplayReproducesWalletOutcomes)
{
  // The command-log payloads are a message's own encoding; replaying them against
  // a fresh identical world reproduces the same authoritative wallet outcome.
  const Net::StationRequest requests[] = {
    Net::StationRequest{ Net::StationRequestKind::Dock, 0, 0, 0 },
    Net::StationRequest{ Net::StationRequestKind::Buy,  0, 5, 0 },
    Net::StationRequest{ Net::StationRequestKind::Buy,  0, 3, 0 },
  };

  // World A: apply the requests, logging each as the server would.
  Persist::InMemoryStore store;
  int creditsA = 0;
  {
    ECS::Registry world;
    const ECS::EntityId player = BuildTradeWorld(world);
    std::vector<Persist::CommandLogEntry> log;
    for (const Net::StationRequest& r : requests)
    {
      (void)GameLogic::ProcessStationRequest(world, player, /*dockRange*/ 5000, r);
      Persist::CommandLogEntry e;
      e.messageId = static_cast<int32_t>(Msg::Raw(Net::StationRequest::Id));
      e.payload = Msg::Encode(r);
      log.push_back(std::move(e));
    }
    store.AppendCommands(log);
    creditsA = world.Get<GameLogic::Wallet>(player).credits;
  }
  EXPECT_EQ(creditsA, 1000 - (5 + 3) * 10);   // bought 8 food @ 10

  // World B ("restart / audit"): replay the log from the store; same wallet.
  int creditsB = 0;
  {
    ECS::Registry world;
    const ECS::EntityId player = BuildTradeWorld(world);
    for (const Persist::CommandLogEntry& e : store.Commands())
    {
      ASSERT_EQ(e.messageId, static_cast<int32_t>(Msg::Raw(Net::StationRequest::Id)));
      Net::StationRequest r;
      ASSERT_TRUE(Msg::Decode(e.payload, r));
      (void)GameLogic::ProcessStationRequest(world, player, 5000, r);
    }
    creditsB = world.Get<GameLogic::Wallet>(player).credits;
  }

  EXPECT_EQ(creditsB, creditsA);
}

// --- v2: durable galaxy layout + persistent markets ---------------------------

TEST(GalaxyPersistence, BuildSystemRowsCoversTheWholeGalaxyPlusHome)
{
  constexpr GameLogic::GalaxyConfig cfg{};
  const std::vector<Persist::SystemRow> rows = DSOServer::BuildSystemRows(cfg);

  // Every procedural system plus the hand-placed home (id -1) at the last index.
  ASSERT_EQ(rows.size(), static_cast<std::size_t>(cfg.planetCount) + 1);
  const Persist::SystemRow& home = rows.back();
  EXPECT_EQ(home.systemId, -1);
  EXPECT_EQ(home.name, "HOME");
  EXPECT_EQ(home.planetX, 0);
  EXPECT_EQ(home.planetZ, 65536);
  EXPECT_EQ(home.stationZ, -3000);
}

TEST(GalaxyPersistence, RowsMatchTheGeneratorSoTheUnseededFallbackIsUnchanged)
{
  // The seeded rows must equal what the old direct generator/manifest produced,
  // so a server with no DB (which regenerates) lays out the identical world.
  constexpr GameLogic::GalaxyConfig cfg{};
  const std::vector<Persist::SystemRow> rows = DSOServer::BuildSystemRows(cfg);

  for (uint32_t i = 0; i < 16; ++i)
  {
    const GameLogic::GalaxySystem s = GameLogic::GenerateSystem(cfg, i);
    const Persist::SystemRow& r = rows[i];
    EXPECT_EQ(r.systemId, static_cast<int32_t>(s.id));
    EXPECT_EQ(r.planetX, s.planetPos.x);
    EXPECT_EQ(r.planetY, s.planetPos.y);
    EXPECT_EQ(r.planetZ, s.planetPos.z);
    EXPECT_EQ(r.stationX, s.stationPos.x);
    EXPECT_EQ(r.economy, s.planet.economy);
    EXPECT_EQ(r.marketSeed, s.marketSeed);
    EXPECT_EQ(r.name, s.name);

    const Net::GalaxySystemInfo want = GameLogic::ToManifestEntry(s);
    const Net::GalaxySystemInfo got = DSOServer::ManifestEntryFrom(r);
    EXPECT_EQ(got.id, want.id);
    EXPECT_EQ(got.x, want.x);
    EXPECT_EQ(got.z, want.z);
    EXPECT_EQ(got.economy, want.economy);
    EXPECT_EQ(got.population, want.population);
    EXPECT_EQ(std::string(got.name), std::string(want.name));
  }
}

TEST(GalaxyPersistence, SystemRowsAndBaselineMarketsRoundTripThroughTheStore)
{
  constexpr GameLogic::GalaxyConfig cfg{};
  const std::vector<Persist::SystemRow> rows = DSOServer::BuildSystemRows(cfg);

  Persist::InMemoryStore store;
  store.UpsertSystems(rows);

  std::vector<Persist::MarketRow> baseline;
  for (const Persist::SystemRow& s : rows)
  {
    const auto m = DSOServer::BaselineMarketRows(s, /*tick*/ 0);
    baseline.insert(baseline.end(), m.begin(), m.end());
  }
  store.UpsertMarketRows(baseline);

  EXPECT_EQ(store.SystemCount(), rows.size());
  EXPECT_EQ(store.MarketRowCount(), rows.size() * GameLogic::COMMODITY_COUNT);
  EXPECT_EQ(store.LoadSystems().size(), rows.size());

  // Alien Items (index 16) is never stocked in a baseline market.
  for (const Persist::MarketRow& mr : store.LoadMarketRows())
    if (mr.commodity == GameLogic::ALIEN_ITEMS_INDEX)
      EXPECT_EQ(mr.stock, 0);

  // Idempotent: re-seeding the same rows doesn't multiply them.
  store.UpsertSystems(rows);
  EXPECT_EQ(store.SystemCount(), rows.size());
}

TEST(GalaxyPersistence, ServiceBootLoadsSystemsAndDrainsMarketDrift)
{
  constexpr GameLogic::GalaxyConfig cfg{};
  const std::vector<Persist::SystemRow> rows = DSOServer::BuildSystemRows(cfg);

  auto store = std::make_unique<Persist::InMemoryStore>();
  store->UpsertSystems(rows);
  Persist::InMemoryStore* raw = store.get();
  Persist::PersistenceService svc(std::move(store), /*startThread*/ false);

  // Boot-time bulk reads.
  EXPECT_EQ(svc.LoadSystems().size(), rows.size());
  EXPECT_TRUE(svc.LoadMarkets().empty());

  // Drifted markets coalesce per (system,commodity), latest wins, one write.
  svc.QueueMarketRows({
    { 0, 1, 42, 1000, 900 },
    { 0, 1, 43, 1004, 950 },   // same key -> coalesced
    { 5, 2, 10,  500, 900 },
  });
  svc.FlushPendingOnce();

  EXPECT_EQ(raw->MarketRowCount(), 2u);   // (0,1) coalesced + (5,2)
  for (const Persist::MarketRow& mr : raw->LoadMarketRows())
    if (mr.systemId == 0 && mr.commodity == 1)
    {
      EXPECT_EQ(mr.stock, 43);            // latest value survived
      EXPECT_EQ(mr.price, 1004);
    }
}
