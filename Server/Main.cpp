#include "pch.h"

#include "GameLogic.h"
#include "NetLib.h"
#include "SnapshotPacketizer.h"
#include "GameEvents.h"
#include "ClientInput.h"
#include "StationProtocol.h"

using namespace winrt;
using namespace Neuron;

namespace
{
  constexpr uint16_t kServerPort = 40000;                 // clients send input here
  constexpr int64_t kAoiCellSize = 100000;                // interest-management cell size
  constexpr int kAoiRadiusCells = 1;                      // viewers see +/- 1 cell
  constexpr uint32_t kSessionTimeoutTicks = 300;          // reap a client idle this long
  constexpr int64_t kDockRange = 5000;                    // how close a player must be to dock
  constexpr int64_t kFireRange = 6000;                    // player front-laser reach
  constexpr double kAimCone = 0.9;                        // ~25deg aiming cone for a hit

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

  // The home system's celestial bodies, ahead of the spawn so a launching player
  // sees them (typed so the client draws the planet/station models, not a ship).
  const ECS::EntityId planet = world.Create();
  world.Add<GameLogic::WorldTransform>(planet, GameLogic::WorldTransform{ { 0, 0, 65536 } });
  world.Add<GameLogic::NetType>(planet, GameLogic::NetType{ GameLogic::ShipType::Planet });

  // One pirate ahead-right of the spawn, close enough to be clearly visible.
  const ECS::EntityId pirate = world.Create();
  world.Add<GameLogic::WorldTransform>(pirate, GameLogic::WorldTransform{ { 1000, 300, 4000 } });
  world.Add<GameLogic::Flight>(pirate, GameLogic::Flight{});
  world.Add<GameLogic::Combatant>(pirate, GameLogic::Combatant{ GameLogic::Team::Pirate, /*energy*/ 80, /*laser*/ 3, /*range*/ 8000, /*autoEngage*/ true });
  world.Add<GameLogic::NetType>(pirate, GameLogic::NetType{ GameLogic::ShipType::Viper });

  // Home system station, BEHIND the spawn (negative z) so a launching player
  // faces the planet with the station at their back (classic Elite launch);
  // within docking range of spawn, and carrying its own market.
  const ECS::EntityId station = world.Create();
  world.Add<GameLogic::WorldTransform>(station, GameLogic::WorldTransform{ { 0, 0, -3000 } });
  world.Add<GameLogic::NetType>(station, GameLogic::NetType{ GameLogic::ShipType::Coriolis });
  // The station is a (near-indestructible) combat target so firing on it is a
  // detectable crime; it never initiates fire (autoEngage = false).
  world.Add<GameLogic::Combatant>(station, GameLogic::Combatant{ GameLogic::Team::Station, 1000000, 0, 1, false });
  {
    const GameLogic::PlanetData home = GameLogic::GeneratePlanet(GameLogic::BASE_GALAXY_SEED);
    GameLogic::ServerStation ss;
    ss.systemId = -1;   // the hand-placed home system
    GameLogic::GenerateMarket(home.economy, GameLogic::BASE_GALAXY_SEED.f, ss.market);
    world.Add<GameLogic::ServerStation>(station, ss);
  }

  // The procedural galaxy: every system's planet + station, scattered far across
  // the int64 field (reachable by teleport, or a long flight). AOI keeps them off
  // the wire until a player is near one. Each station carries its own market.
  const GameLogic::GalaxyConfig galaxyCfg{};
  const std::vector<GameLogic::GalaxySystem> systems = GameLogic::GenerateGalaxy(galaxyCfg);
  for (const GameLogic::GalaxySystem& sys : systems)
  {
    const ECS::EntityId pl = world.Create();
    world.Add<GameLogic::WorldTransform>(pl, GameLogic::WorldTransform{ sys.planetPos });
    world.Add<GameLogic::NetType>(pl, GameLogic::NetType{ GameLogic::ShipType::Planet });

    const ECS::EntityId stn = world.Create();
    world.Add<GameLogic::WorldTransform>(stn, GameLogic::WorldTransform{ sys.stationPos });
    world.Add<GameLogic::NetType>(stn, GameLogic::NetType{ GameLogic::ShipType::Coriolis });
    world.Add<GameLogic::Combatant>(stn, GameLogic::Combatant{ GameLogic::Team::Station, 1000000, 0, 1, false });
    GameLogic::ServerStation ss;
    ss.systemId = static_cast<int>(sys.id);
    GameLogic::GenerateMarket(sys.planet.economy, sys.marketSeed, ss.market);
    world.Add<GameLogic::ServerStation>(stn, ss);
  }
  printf("Galaxy: %d systems generated.\n", galaxyCfg.planetCount);

  // The chart manifest shipped to every client on connect: the procedural systems
  // plus the hand-placed home system (id -1) so players can always teleport back.
  std::vector<Net::GalaxySystemInfo> manifest = GameLogic::BuildManifest(systems);
  {
    const GameLogic::PlanetData home = GameLogic::GeneratePlanet(GameLogic::BASE_GALAXY_SEED);
    Net::GalaxySystemInfo h;
    h.id = static_cast<uint32_t>(-1);   // matches the home station's systemId (-1)
    h.x = 0; h.y = 0; h.z = 65536;      // the home planet
    const char* kHomeName = "HOME";
    for (std::size_t i = 0; kHomeName[i] != '\0' && i < Net::GALAXY_NAME_MAX - 1; ++i)
      h.name[i] = kHomeName[i];
    h.government = static_cast<uint8_t>(home.government);
    h.economy = static_cast<uint8_t>(home.economy);
    h.techLevel = static_cast<uint8_t>(home.techLevel);
    h.population = static_cast<uint16_t>(home.population);
    h.productivity = static_cast<uint16_t>(home.productivity);
    manifest.push_back(h);
  }

  GameLogic::AreaOfInterest aoi(kAoiCellSize);
  GameLogic::ServerSessions sessions;
  sessions.SetManifest(std::move(manifest));   // every client gets the chart on connect
  GameLogic::DespawnTracker despawns;
  GameLogic::SpawnDirector spawner(/*seed*/ 0x51EEDu, /*intervalTicks*/ 600, /*maxNpcs*/ 12);

  uint8_t recv[2048];
  uint32_t tick = 0;
  std::size_t lastSessions = 0;
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
          {
            const ECS::EntityId player = sessions.OnInput(world, from, in, tick);

            // Player fired: resolve a forward shot. Hitting the station or police
            // is a crime - the first offence summons police to hunt the player.
            if (in.fire && world.IsValid(player))
            {
              const GameLogic::FireOutcome shot = GameLogic::ResolvePlayerFire(world, player, kFireRange, kAimCone);
              if (shot.hit)
              {
                if (shot.targetTeam == GameLogic::Team::Station || shot.targetTeam == GameLogic::Team::Police)
                {
                  GameLogic::Wanted* wnt = world.TryGet<GameLogic::Wanted>(player);
                  if (wnt != nullptr && wnt->level == 0)
                    spawner.SpawnPolice(world, world.Get<GameLogic::WorldTransform>(player).position, 2);
                  if (wnt != nullptr)
                    wnt->level++;
                }
                if (shot.destroyed)
                {
                  sessions.Broadcast(static_cast<uint16_t>(Net::EventType::EntityDeath),
                                     Net::EncodeDeath(shot.target.index, player.index));
                  world.Destroy(shot.target);
                }
              }
            }
          }
          break;
        }
        case Net::EVENT_MAGIC:
          sessions.OnReliable(from, recv, size);
          break;
        default:
          break;
      }
    }

    if (sessions.Count() != lastSessions)
    {
      lastSessions = sessions.Count();
      printf("Clients connected: %zu\n", lastSessions);
    }

    // 1b. Process station requests (dock/buy/sell/equip) delivered on each
    //     session's reliable channel; dock attaches to the nearest station and
    //     trades hit that station's own market.
    for (auto& entry : sessions.All())
    {
      GameLogic::Session& s = entry.second;
      Net::ReliableMessage msg;
      while (s.events.Receive(msg))
      {
        Net::StationRequest req;
        if (Net::DecodeStationRequest(msg, req))
        {
          const Net::StationResponse resp =
              GameLogic::ProcessStationRequest(world, s.entity, kDockRange, req);
          Net::SendStationResponse(s.events, resp);
        }
      }
    }

    // 2. Advance the authoritative simulation one tick, then run dynamic spawning
    //    (periodic pirate encounters near players).
    GameLogic::Tick(world);
    ++tick;
    spawner.Step(world, tick);

    // 2a. Heartbeat: roughly every two seconds, log the first player's position
    //     and flight so a launching client can be confirmed to actually move
    //     through the world (and steer) server-side.
    if (tick % 60 == 0 && !sessions.All().empty())
    {
      const GameLogic::Session& s = sessions.All().begin()->second;
      if (world.IsValid(s.entity))
      {
        const GameLogic::WorldTransform& t = world.Get<GameLogic::WorldTransform>(s.entity);
        const GameLogic::Flight& f = world.Get<GameLogic::Flight>(s.entity);
        printf("Player @ (%lld, %lld, %lld)  speed=%.2f roll=%.3f pitch=%.3f\n",
               static_cast<long long>(t.position.x), static_cast<long long>(t.position.y),
               static_cast<long long>(t.position.z), f.speed, f.roll, f.pitch);
      }
    }

    // 2b. Realtime combat: resolve hits, broadcast reliable death events, and
    //     destroy the wrecks (their removal also rides the despawn diff below).
    for (const GameLogic::Kill& kill : GameLogic::StepCombat(world))
    {
      // A player "death" is a simple in-place respawn (restore hull, clear record)
      // until persistence/respawn points exist; NPCs are destroyed as wrecks.
      if (world.IsValid(kill.victim) && world.TryGet<GameLogic::PlayerTag>(kill.victim) != nullptr)
      {
        if (GameLogic::Combatant* c = world.TryGet<GameLogic::Combatant>(kill.victim))
          c->energy = 255;
        if (GameLogic::Wanted* wnt = world.TryGet<GameLogic::Wanted>(kill.victim))
          wnt->level = 0;
        continue;
      }

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

      const Net::WorldSnapshot snap = aoi.SnapshotFor(world, tick, viewerPos, kAoiRadiusCells, s.entity.index);
      for (const std::vector<uint8_t>& datagram : Net::PacketizeSnapshot(snap))
        socket.SendTo(s.endpoint, datagram.data(), datagram.size());

      const std::vector<uint8_t> eventPacket = s.events.WritePacket();
      socket.SendTo(s.endpoint, eventPacket.data(), eventPacket.size());
    }

    Sleep(33);   // ~30 Hz tick
  }
}
