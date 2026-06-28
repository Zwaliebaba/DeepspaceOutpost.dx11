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
  constexpr uint16_t SERVER_PORT = 40000;            // clients send input here
  constexpr int64_t AOI_CELL_SIZE = 100000;          // interest-management cell size
  constexpr int AOI_RADIUS_CELLS = 1;                // viewers see +/- 1 cell
  constexpr uint32_t SESSION_TIMEOUT_TICKS = 300;    // reap a client idle this long
  constexpr int64_t DOCK_RANGE = 5000;               // how close a player must be to dock
  constexpr int64_t FIRE_RANGE = 6000;               // player front-laser reach
  constexpr double AIM_CONE = 0.9;                   // ~25deg aiming cone for a hit

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
         GameLogic::Version(), static_cast<unsigned>(SERVER_PORT));
  CoreEngine::Startup();

  Net::NetStartup();
  Net::UdpSocket socket;
  if (!socket.Open(SERVER_PORT))
  {
    printf("Failed to bind UDP %u\n", static_cast<unsigned>(SERVER_PORT));
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
  world.Add<GameLogic::Combatant>(pirate, GameLogic::Combatant{ GameLogic::Team::Pirate, /*energy*/ 80, /*laser*/ 3, /*range*/ 5000, /*autoEngage*/ true });
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
  constexpr GameLogic::GalaxyConfig GALAXY_CFG{};
  const std::vector<GameLogic::GalaxySystem> systems = GameLogic::GenerateGalaxy(GALAXY_CFG);

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
  printf("Galaxy: %d systems generated.\n", GALAXY_CFG.planetCount);

  // The chart manifest shipped to every client on connect: the procedural systems
  // plus the hand-placed home system (id -1) so players can always teleport back.
  std::vector<Net::GalaxySystemInfo> manifest = GameLogic::BuildManifest(systems);
  {
    const GameLogic::PlanetData home = GameLogic::GeneratePlanet(GameLogic::BASE_GALAXY_SEED);
    Net::GalaxySystemInfo h;
    h.id = static_cast<uint32_t>(-1);   // matches the home station's systemId (-1)
    h.x = 0; h.y = 0; h.z = 65536;      // the home planet

    const char* HOME_NAME = "HOME";
    for (std::size_t i = 0; HOME_NAME[i] != '\0' && i < Net::GALAXY_NAME_MAX - 1; ++i)
      h.name[i] = HOME_NAME[i];
    h.government = static_cast<uint8_t>(home.government);
    h.economy = static_cast<uint8_t>(home.economy);
    h.techLevel = static_cast<uint8_t>(home.techLevel);
    h.population = static_cast<uint16_t>(home.population);
    h.productivity = static_cast<uint16_t>(home.productivity);
    manifest.push_back(h);
  }

  GameLogic::AreaOfInterest aoi(AOI_CELL_SIZE);
  GameLogic::ServerSessions sessions;
  sessions.SetManifest(std::move(manifest));   // every client gets the chart on connect
  GameLogic::DespawnTracker despawns;
  GameLogic::SpawnDirector spawner(/*seed*/ 0x51EEDu, /*intervalTicks*/ 600, /*maxNpcs*/ 12);

  uint8_t recv[2048];
  uint32_t tick = 0;
  std::size_t lastSessions = 0;

  // Rate-limit the player-respawn log so a player parked in combat doesn't spam
  // the console: print at most once per window, with a count of any suppressed
  // respawns since the last line.
  constexpr uint32_t kRespawnLogWindow = 300;   // ~10s at 30 Hz
  uint32_t lastRespawnLogTick = 0;
  int      suppressedRespawns = 0;

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

            // Apply a resolved player shot (laser or missile): firing on the
            // station or police is a crime - the first offence summons police to
            // hunt the player - and a target driven to zero energy dies.
            auto applyShot = [&](const GameLogic::FireOutcome& shot)
            {
              if (!shot.hit)
                return;
              if (shot.targetTeam == GameLogic::Team::Station || shot.targetTeam == GameLogic::Team::Police)
              {
                GameLogic::Wanted* wnt = world.TryGet<GameLogic::Wanted>(player);
                if (wnt != nullptr && wnt->level == 0)
                {
                  spawner.SpawnPolice(world, world.Get<GameLogic::WorldTransform>(player).position, 2);
                  printf("[tick %u] CRIME: player %u fired on team %d -> police dispatched\n",
                         tick, player.index, shot.targetTeam);
                }
                if (wnt != nullptr)
                  wnt->level++;
              }
              if (shot.destroyed)
              {
                sessions.Broadcast(static_cast<uint16_t>(Net::EventType::EntityDeath),
                                   Net::EncodeDeath(shot.target.index, player.index));
                world.Destroy(shot.target);
                printf("[tick %u] player %u destroyed entity %u with the laser\n",
                       tick, player.index, shot.target.index);
              }
            };

            // Player fired the front laser: resolve a forward shot.
            if (in.fire && world.IsValid(player))
              applyShot(GameLogic::ResolvePlayerFire(world, player, FIRE_RANGE, AIM_CONE));

            // Player launched a missile: spawn a homing projectile that chases the
            // target the player locked (in.missileTarget) and detonates on contact
            // (its kill is resolved later in the tick loop by StepMissiles). Firing
            // on the station or police is a crime, exactly like the laser.
            if (in.fireMissile && world.IsValid(player))
            {
              const ECS::EntityId missile = GameLogic::SpawnMissile(world, player, in.missileTarget);
              const GameLogic::Missile* mc =
                  world.IsValid(missile) ? world.TryGet<GameLogic::Missile>(missile) : nullptr;
              if (mc == nullptr)
              {
                printf("[tick %u] player %u MISSILE LAUNCH FAILED (missing transform/flight/combatant)\n",
                       tick, player.index);
              }
              else if (world.IsValid(mc->target))
              {
                const GameLogic::Combatant* tc = world.TryGet<GameLogic::Combatant>(mc->target);
                printf("[tick %u] player %u LAUNCHED missile %u -> locked target %u (team %d)\n",
                       tick, player.index, missile.index, mc->target.index, tc != nullptr ? tc->team : -1);

                GameLogic::FireOutcome lock;
                lock.hit = true;
                lock.target = mc->target;
                lock.targetTeam = tc != nullptr ? tc->team : -1;
                lock.destroyed = false;   // detonation/kill happens later in StepMissiles
                applyShot(lock);
              }
              else
              {
                printf("[tick %u] player %u LAUNCHED missile %u (no target lock - flying straight)\n",
                       tick, player.index, missile.index);
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
    for (auto& s : sessions.All() | std::views::values)
    {
      Net::ReliableMessage msg;
      while (s.events.Receive(msg))
      {
        Net::StationRequest req;
        if (Net::DecodeStationRequest(msg, req))
        {
          const Net::StationResponse resp =
              GameLogic::ProcessStationRequest(world, s.entity, DOCK_RANGE, req);
          Net::SendStationResponse(s.events, resp);
        }
      }
    }

    // 2. Advance the authoritative simulation one tick, then run dynamic spawning
    //    (periodic pirate encounters near players).
    GameLogic::Tick(world);
    ++tick;
    spawner.Step(world, tick);

    // 2b. Advance in-flight missiles (homing + detonation) and realtime combat,
    //     then resolve every resulting kill: broadcast a death event and destroy
    //     the wreck (its removal also rides the despawn diff below).
    std::vector<GameLogic::Kill> kills = GameLogic::StepMissiles(world);
    for (const GameLogic::Kill& k : GameLogic::StepCombat(world))
      kills.push_back(k);
    for (const GameLogic::Kill& kill : kills)
    {
      // A player "death" is a simple in-place respawn (restore hull, clear record)
      // until persistence/respawn points exist; NPCs are destroyed as wrecks.
      if (world.IsValid(kill.victim) && world.TryGet<GameLogic::PlayerTag>(kill.victim) != nullptr)
      {
        if (GameLogic::Combatant* c = world.TryGet<GameLogic::Combatant>(kill.victim))
        {
          c->energy = 255;
          // Brief immunity so the in-place respawn isn't instantly re-killed by
          // hostiles still in range (gives time to flee or fight back).
          c->invulnTicks = GameLogic::RESPAWN_GRACE_TICKS;
        }
        if (GameLogic::Wanted* wnt = world.TryGet<GameLogic::Wanted>(kill.victim))
          wnt->level = 0;

        if (tick - lastRespawnLogTick >= kRespawnLogWindow)
        {
          if (suppressedRespawns > 0)
            printf("[tick %u] player %u respawned (killer %u; +%d more respawn(s) since last log)\n",
                   tick, kill.victim.index, kill.killer, suppressedRespawns);
          else
            printf("[tick %u] player %u was killed by %u -> respawned in place\n",
                   tick, kill.victim.index, kill.killer);
          lastRespawnLogTick = tick;
          suppressedRespawns = 0;
        }
        else
        {
          ++suppressedRespawns;
        }
        continue;
      }

      sessions.Broadcast(static_cast<uint16_t>(Net::EventType::EntityDeath),
                         Net::EncodeDeath(kill.victim.index, kill.killer));
      world.Destroy(kill.victim);
      printf("[tick %u] entity %u destroyed by %u\n", tick, kill.victim.index, kill.killer);
    }

    // 3. Reap idle clients, then broadcast every despawn (reaped players + props)
    //    as a reliable event to all remaining clients.
    sessions.Reap(world, tick, SESSION_TIMEOUT_TICKS);
    for (uint32_t goneId : despawns.Update(CurrentIds(world)))
      sessions.Broadcast(static_cast<uint16_t>(Net::EventType::EntityDespawn), Net::EncodeDespawn(goneId));

    // 4. Send each client its own area-of-interest snapshot + reliable event packet.
    aoi.Rebuild(world);
    for (auto& s : sessions.All() | std::views::values)
    {
      Math::Vector3i64 viewerPos{ 0, 0, 0 };
      if (world.IsValid(s.entity))
        viewerPos = world.Get<GameLogic::WorldTransform>(s.entity).position;

      const Net::WorldSnapshot snap = aoi.SnapshotFor(world, tick, viewerPos, AOI_RADIUS_CELLS, s.entity.index);
      for (const std::vector<uint8_t>& datagram : Net::PacketizeSnapshot(snap))
        socket.SendTo(s.endpoint, datagram.data(), datagram.size());

      const std::vector<uint8_t> eventPacket = s.events.WritePacket();
      socket.SendTo(s.endpoint, eventPacket.data(), eventPacket.size());
    }

    Sleep(33);   // ~30 Hz tick
  }
}
