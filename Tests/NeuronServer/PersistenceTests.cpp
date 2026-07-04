#include <gtest/gtest.h>

#include <memory>
#include <string>

#include "GameLogic.h"              // ServerSessions (spawns a full player entity), components
#include "PlayerPersistence.h"      // FromComponents / ApplyToComponents converters

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
  world.Get<GameLogic::PlayerRecord>(e).score = 4096;
  world.Get<GameLogic::CargoHold>(e).capacity = 35;
  world.Get<GameLogic::CargoHold>(e).units[2] = 11;
  world.Get<GameLogic::CargoHold>(e).units[7] = 4;
  world.Get<GameLogic::Equipment>(e).missiles = 2;
  world.Get<GameLogic::Equipment>(e).largeCargoBay = true;
  world.Get<GameLogic::Equipment>(e).fuelScoop = true;
  world.Add<GameLogic::Witchspace>(e, GameLogic::Witchspace{});

  const Persist::PlayerPersistState s = GameLogic::PlayerStateFromComponents(world, e, /*tick*/ 42);
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
  EXPECT_EQ(GameLogic::PlayerStateFromComponents(world, e, 1).lastSystemId, -1);

  // Dock at a station belonging to system 5.
  const ECS::EntityId station = world.Create();
  world.Add<GameLogic::ServerStation>(station, GameLogic::ServerStation{ /*systemId*/ 5, {} });
  world.Get<GameLogic::DockState>(e).docked = true;
  world.Get<GameLogic::DockState>(e).stationId = station.index;

  EXPECT_EQ(GameLogic::PlayerStateFromComponents(world, e, 1).lastSystemId, 5);
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
    world.Get<GameLogic::PlayerRecord>(e).score = 9001;
    world.Get<GameLogic::CargoHold>(e).units[4] = 9;
    world.Get<GameLogic::Equipment>(e).missiles = 4;
    world.Get<GameLogic::Equipment>(e).ecm = true;
    store.UpsertPlayer(GameLogic::PlayerStateFromComponents(world, e, /*tick*/ 500));
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
    EXPECT_EQ(world.Get<GameLogic::PlayerRecord>(e).score, 9001);
    EXPECT_EQ(world.Get<GameLogic::CargoHold>(e).units[4], 9);
    EXPECT_EQ(world.Get<GameLogic::Equipment>(e).missiles, 4);
    EXPECT_TRUE(world.Get<GameLogic::Equipment>(e).ecm);
  }
}
