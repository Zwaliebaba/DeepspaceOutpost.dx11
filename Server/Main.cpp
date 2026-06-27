#include "pch.h"

#include "GameLogic.h"
#include "NetLib.h"
#include "SnapshotPacketizer.h"
#include "GameEvents.h"
#include "ClientInput.h"

using namespace winrt;
using namespace Neuron;

namespace
{
  constexpr uint16_t kServerPort = 40000;                 // clients send input here
  constexpr int64_t kAoiCellSize = 100000;                // interest-management cell size
  constexpr int kAoiRadiusCells = 1;                      // viewers see +/- 1 cell
  constexpr uint32_t kSessionTimeoutTicks = 300;          // reap a client idle this long

  // Indices of every entity currently in the world (for the despawn diff).
  std::vector<uint32_t> CurrentIds(ECS::Registry& _world)
  {
    std::vector<uint32_t> ids;
    _world.Each<GameLogic::WorldTransform>([&ids](ECS::EntityId _id, GameLogic::WorldTransform&)
    {
      ids.push_back(_id.index);
    });
    return ids;
  }
}

int main()
{
  printf("Starting DSOServer (GameLogic v%u) on UDP %u...\n",
         GameLogic::Version(), static_cast<unsigned>(kServerPort));
  CoreEngine::Startup();

  Net::NetStartup();
  Net::UdpSocket socket;
  if (!socket.Open(kServerPort))
  {
    printf("Failed to bind UDP %u\n", static_cast<unsigned>(kServerPort));
    return 1;
  }

  // The authoritative world. A couple of static props so a lone player still sees
  // something; players are spawned on demand as clients connect.
  ECS::Registry world;
  const ECS::EntityId propA = world.Create();
  world.Add<GameLogic::WorldTransform>(propA, GameLogic::WorldTransform{ { 3000, 0, 0 } });
  ECS::EntityId propB = world.Create();
  world.Add<GameLogic::WorldTransform>(propB, GameLogic::WorldTransform{ { 0, 3000, 0 } });

  // Two opposing NPC combatants so the authoritative combat system is live.
  const ECS::EntityId red = world.Create();
  world.Add<GameLogic::WorldTransform>(red, GameLogic::WorldTransform{ { 1000, 0, 0 } });
  world.Add<GameLogic::Combatant>(red, GameLogic::Combatant{ /*team*/ 0, /*energy*/ 100, /*laser*/ 2, /*range*/ 8000 });
  const ECS::EntityId blue = world.Create();
  world.Add<GameLogic::WorldTransform>(blue, GameLogic::WorldTransform{ { 2000, 0, 0 } });
  world.Add<GameLogic::Combatant>(blue, GameLogic::Combatant{ /*team*/ 1, /*energy*/ 100, /*laser*/ 2, /*range*/ 8000 });

  // The ported galaxy/economy systems, live on the server: generate the home
  // system from the canonical seed and its market.
  {
    const GameLogic::PlanetData home = GameLogic::GeneratePlanet(GameLogic::BASE_GALAXY_SEED);
    const std::string homeName = GameLogic::NamePlanet(GameLogic::BASE_GALAXY_SEED);
    GameLogic::MarketEntry market[GameLogic::COMMODITY_COUNT];
    GameLogic::GenerateMarket(home.economy, GameLogic::BASE_GALAXY_SEED.f, market);
    printf("Home system %s: economy %d, tech %d, gov %d; food price %d\n",
           homeName.c_str(), home.economy, home.techLevel, home.government, market[0].price);
  }

  GameLogic::AreaOfInterest aoi(kAoiCellSize);
  GameLogic::ServerSessions sessions;
  GameLogic::DespawnTracker despawns;

  uint8_t recv[2048];
  uint32_t tick = 0;
  for (;;)
  {
    Timer::Core::Update();

    // 1. Receive client input and acks; new endpoints connect on their first input.
    Net::Endpoint from;
    for (int i = 0; i < 256; ++i)
    {
      const int got = socket.RecvFrom(recv, sizeof(recv), from);
      if (got <= 0)
        break;

      const std::size_t size = static_cast<std::size_t>(got);
      switch (Net::PeekMagic(recv, size))
      {
        case Net::INPUT_MAGIC:
        {
          Net::DataReader reader(recv, size);
          Net::ClientInput in;
          if (Net::ReadInput(reader, in))
            sessions.OnInput(world, from, in, tick);
          break;
        }
        case Net::EVENT_MAGIC:
          sessions.OnReliable(from, recv, size);
          break;
        default:
          break;
      }
    }

    // 2. Advance the authoritative simulation one tick.
    GameLogic::Tick(world);
    ++tick;

    // Demonstrate a despawn mid-run.
    if (tick == 300 && world.IsValid(propB))
      world.Destroy(propB);

    // 2b. Realtime combat: resolve hits, broadcast reliable death events, and
    //     destroy the wrecks (their removal also rides the despawn diff below).
    for (const GameLogic::Kill& kill : GameLogic::StepCombat(world))
    {
      sessions.Broadcast(static_cast<uint16_t>(Net::EventType::EntityDeath),
                         Net::EncodeDeath(kill.victim.index, kill.killer));
      world.Destroy(kill.victim);
    }

    // 3. Reap idle clients, then broadcast every despawn (reaped players + props)
    //    as a reliable event to all remaining clients.
    sessions.Reap(world, tick, kSessionTimeoutTicks);
    for (uint32_t goneId : despawns.Update(CurrentIds(world)))
      sessions.Broadcast(static_cast<uint16_t>(Net::EventType::EntityDespawn), Net::EncodeDespawn(goneId));

    // 4. Send each client its own area-of-interest snapshot + reliable event packet.
    aoi.Rebuild(world);
    for (auto& entry : sessions.All())
    {
      GameLogic::Session& s = entry.second;

      Math::Vector3i64 viewerPos{ 0, 0, 0 };
      if (world.IsValid(s.entity))
        viewerPos = world.Get<GameLogic::WorldTransform>(s.entity).position;

      const Net::WorldSnapshot snap = aoi.SnapshotFor(world, tick, viewerPos, kAoiRadiusCells);
      for (const std::vector<uint8_t>& datagram : Net::PacketizeSnapshot(snap))
        socket.SendTo(s.endpoint, datagram.data(), datagram.size());

      const std::vector<uint8_t> eventPacket = s.events.WritePacket();
      socket.SendTo(s.endpoint, eventPacket.data(), eventPacket.size());
    }

    Sleep(33);   // ~30 Hz tick
  }
}
