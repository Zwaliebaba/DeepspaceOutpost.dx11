#include "pch.h"

#include "GameLogic.h"
#include "NetLib.h"
#include "SnapshotPacketizer.h"
#include "ClientInput.h"
#include "StationProtocol.h"
#include "CombatMessages.h"
#include "Messages/MessageBus.h"
#include "Messages/Framing.h"
#include "Messages/Reliable.h"
#include "Messages/MessageEndpoint.h"
#include "Messages/Defs/CoreEvents.h"
#include "Messages/Defs/PlayerSession.h"

#include <unordered_map>
#include <unordered_set>
#include <vector>

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

  // How far a system's landmarks (planet + station) stay visible, independent of the
  // per-ship AOI radius. A planet/station is a huge, static body you fly toward for a
  // long time, so culling it at the +/-1 ship cell (~100k-300k units) makes it pop in and
  // out as you cross the system. ~20 AOI cells keeps the local system on the wire while
  // distant systems (scattered tens of millions of units apart) stay culled.
  constexpr int64_t LANDMARK_VIS_DIST = 2'000'000;

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

  // Add landmark entities (planets/stations) within LANDMARK_VIS_DIST of the viewer to an
  // already-built AOI snapshot, skipping any the AOI pass already included. Keeps the
  // planet/station the player is flying around from popping out at the ship-AOI boundary,
  // without widening interest for ordinary ships.
  void AppendLandmarks(ECS::Registry& _world, Net::WorldSnapshot& _snap, const Math::Vector3i64& _viewerPos,
                       const std::vector<ECS::EntityId>& _landmarks)
  {
    std::unordered_set<uint32_t> present;
    present.reserve(_snap.entities.size() * 2);
    for (const Net::EntitySnapshot& e : _snap.entities)
      present.insert(e.id);

    for (ECS::EntityId lm : _landmarks)
    {
      if (present.count(lm.index))
        continue;
      if (!_world.IsValid(lm))
        continue;   // a station can (just) be destroyed; its id then drops out
      const GameLogic::WorldTransform* t = _world.TryGet<GameLogic::WorldTransform>(lm);
      if (t == nullptr)
        continue;

      // Galaxy extent is +/-1e8, so each delta squared (<=4e16) and their sum (<=1.2e17)
      // stay well within int64; LANDMARK_VIS_DIST^2 is 4e12.
      const int64_t dx = t->position.x - _viewerPos.x;
      const int64_t dy = t->position.y - _viewerPos.y;
      const int64_t dz = t->position.z - _viewerPos.z;
      if (dx * dx + dy * dy + dz * dz <= LANDMARK_VIS_DIST * LANDMARK_VIS_DIST)
        _snap.entities.push_back(GameLogic::MakeEntitySnapshot(_world, lm, *t));
    }
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

  // Static landmarks (planets + stations). They stay visible to nearby viewers beyond the
  // ship AOI radius (see AppendLandmarks); collected here as they are created. Never moved
  // or (planets) destroyed, so the ids stay valid for the process lifetime.
  std::vector<ECS::EntityId> landmarks;

  // The home system's celestial bodies, ahead of the spawn so a launching player
  // sees them (typed so the client draws the planet/station models, not a ship).
  const ECS::EntityId planet = world.Create();
  world.Add<GameLogic::WorldTransform>(planet, GameLogic::WorldTransform{ { 0, 0, 65536 } });
  world.Add<GameLogic::NetType>(planet, GameLogic::NetType{ GameLogic::ShipType::Planet });
  landmarks.push_back(planet);

  // One pirate ahead-right, out toward the planet. Placed well beyond its own engage
  // range from the spawn (and station behind), so a fresh launch isn't sniped/farmed at
  // the spawn point; you meet it as an opt-in fight on the way to the planet. Its range
  // is shortened too, so it only opens fire at close quarters rather than from afar.
  const ECS::EntityId pirate = world.Create();
  world.Add<GameLogic::WorldTransform>(pirate, GameLogic::WorldTransform{ { 1500, 400, 14000 } });
  world.Add<GameLogic::Flight>(pirate, GameLogic::Flight{});
  world.Add<GameLogic::Combatant>(pirate, GameLogic::Combatant{ GameLogic::Team::Pirate, /*energy*/ 80, /*laser*/ 3, /*range*/ 3000, /*autoEngage*/ true });
  world.Add<GameLogic::NetType>(pirate, GameLogic::NetType{ GameLogic::ShipType::Viper });
  world.Add<GameLogic::Bounty>(pirate, GameLogic::Bounty{ GameLogic::PIRATE_BOUNTY });   // killing it pays out

  // Home system station, BEHIND the spawn (negative z) so a launching player
  // faces the planet with the station at their back (classic Elite launch);
  // within docking range of spawn, and carrying its own market.
  const ECS::EntityId station = world.Create();
  world.Add<GameLogic::WorldTransform>(station, GameLogic::WorldTransform{ { 0, 0, -3000 } });
  world.Add<GameLogic::NetType>(station, GameLogic::NetType{ GameLogic::ShipType::Coriolis });
  landmarks.push_back(station);
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
    landmarks.push_back(pl);

    const ECS::EntityId stn = world.Create();
    world.Add<GameLogic::WorldTransform>(stn, GameLogic::WorldTransform{ sys.stationPos });
    world.Add<GameLogic::NetType>(stn, GameLogic::NetType{ GameLogic::ShipType::Coriolis });
    world.Add<GameLogic::Combatant>(stn, GameLogic::Combatant{ GameLogic::Team::Station, 1000000, 0, 1, false });
    landmarks.push_back(stn);
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

  // How often a wanted level cools down by one (in ticks; ~20s at 30 Hz).
  constexpr uint32_t WANTED_DECAY_INTERVAL = 600;

  // How often players' shields/energy regenerate (legacy regenerated ~every 8
  // frames). A slow cadence also keeps the on-change PlayerStatus from firing every
  // tick while a shield refills.
  constexpr uint32_t SHIELD_REGEN_INTERVAL = 8;

  // Last PlayerStatus sent to each session (by endpoint key), so we resend the
  // owner's private vitals only when they change rather than every tick.
  std::unordered_map<uint64_t, Msg::PlayerStatus> lastStatus;

  // Rate-limit the player-respawn log so a player parked in combat doesn't spam
  // the console: print at most once per window, with a count of any suppressed
  // respawns since the last line.
  constexpr uint32_t kRespawnLogWindow = 300;   // ~10s at 30 Hz
  uint32_t lastRespawnLogTick = 0;
  int      suppressedRespawns = 0;

  // Combat effects are driven by in-process messages instead of inline tangled
  // logic: a FireWeapon command resolves to facts (Crime / EntityKilled) that
  // independent subscribers act on. The combat math itself is unchanged.
  Msg::MessageBus bus;

  // A FireWeapon command resolves against the authoritative world (laser geometry
  // / missile spawn unchanged), publishing the resulting facts.
  bus.Subscribe<GameLogic::FireWeapon>([&](const GameLogic::FireWeapon& _fw)
  {
    GameLogic::ResolveFireWeapon(world, bus, _fw, FIRE_RANGE, AIM_CONE);
  });

  // A crime dispatches police to the offender, once, on the first offence. The
  // offender's wanted level just changed, so refresh their roster entry for everyone.
  bus.Subscribe<GameLogic::Crime>([&](const GameLogic::Crime& _c)
  {
    Msg::PlayerInfo pi;
    if (sessions.PlayerInfoFor(world, _c.offender.index, pi))
      sessions.Broadcast(pi);

    if (!_c.firstOffence || !world.IsValid(_c.offender))
      return;
    const GameLogic::WorldTransform* t = world.TryGet<GameLogic::WorldTransform>(_c.offender);
    if (t == nullptr)
      return;
    spawner.SpawnPolice(world, t->position, 2);
    printf("[tick %u] CRIME: player %u fired on team %d -> police dispatched\n",
           tick, _c.offender.index, _c.victimTeam);
  });

  // A death: respawn a player in place (restore hull, clear record, brief grace),
  // or broadcast the death and destroy a wreck. Unifies the old laser-kill and
  // tick-kill-loop paths into one subscriber.
  bus.Subscribe<GameLogic::EntityKilled>([&](const GameLogic::EntityKilled& _k)
  {
    if (!world.IsValid(_k.victim))
      return;   // already resolved this tick (e.g. two shots on one target)

    if (world.TryGet<GameLogic::PlayerTag>(_k.victim) != nullptr)
    {
      // Tell the dying player it died so its client plays the game-over sequence; we
      // still respawn it in place below. Sent only to that one session, so other
      // clients don't briefly drop the (still-alive, respawned) ship from their view.
      for (auto& entry : sessions.All())
        if (entry.second.entity.index == _k.victim.index)
        {
          entry.second.events.Send(Msg::EntityDeath{ _k.victim.index, _k.killer });
          break;
        }

      // Pay the killer a wanted-derived bounty (if the victim was a fugitive)
      // BEFORE the record is wiped by the respawn below; a clean-player kill pays
      // nothing but still bumps the killer's score.
      GameLogic::CreditKill(world, _k.killer, _k.victim);

      if (GameLogic::Combatant* c = world.TryGet<GameLogic::Combatant>(_k.victim))
      {
        c->energy = GameLogic::MAX_ENERGY;
        c->invulnTicks = GameLogic::RESPAWN_GRACE_TICKS;
      }
      if (GameLogic::Shields* sh = world.TryGet<GameLogic::Shields>(_k.victim))
      {
        sh->front = GameLogic::MAX_SHIELD;   // respawn with full shields
        sh->aft = GameLogic::MAX_SHIELD;
      }
      if (GameLogic::Wanted* wnt = world.TryGet<GameLogic::Wanted>(_k.victim))
        wnt->level = 0;

      // Death wipes the wanted record - refresh the roster so a respawned player
      // shows as clean again on everyone's screen.
      if (Msg::PlayerInfo pi; sessions.PlayerInfoFor(world, _k.victim.index, pi))
        sessions.Broadcast(pi);

      if (tick - lastRespawnLogTick >= kRespawnLogWindow)
      {
        if (suppressedRespawns > 0)
          printf("[tick %u] player %u respawned (killer %u; +%d more respawn(s) since last log)\n",
                 tick, _k.victim.index, _k.killer, suppressedRespawns);
        else
          printf("[tick %u] player %u was killed by %u -> respawned in place\n",
                 tick, _k.victim.index, _k.killer);
        lastRespawnLogTick = tick;
        suppressedRespawns = 0;
      }
      else
      {
        ++suppressedRespawns;
      }
      return;
    }

    // Pay the killer the wreck's bounty - but only for a real combatant kill. A
    // detonated missile is also reported here (so its explosion shows), yet it
    // carries no Combatant, so it neither pays out nor counts as a score.
    if (world.Has<GameLogic::Combatant>(_k.victim))
      GameLogic::CreditKill(world, _k.killer, _k.victim);

    sessions.Broadcast(Msg::EntityDeath{ _k.victim.index, _k.killer });
    world.Destroy(_k.victim);
    printf("[tick %u] entity %u destroyed by %u\n", tick, _k.victim.index, _k.killer);
  });

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
        case Msg::MESSAGE_MAGIC:
        {
          // Unified 'NMSG' framing. Today the client uses the UNRELIABLE lane for
          // its InputCommand; other lanes fold in as later phases land.
          Msg::PacketHeader hdr;
          std::vector<Msg::Record> records;
          if (!Msg::ReadPacket(recv, size, hdr, records) || hdr.lane != Msg::MessageLane::Unreliable)
            break;

          for (const Msg::Record& rec : records)
          {
            // Inbound validation: only the expected command id on this lane, and
            // only if it decodes (direction is guaranteed by the message type:
            // InputCommand is ClientToServer). Malformed/unknown records are
            // dropped; stale/duplicate inputs are rejected by OnInput's sequence.
            static_assert(Net::ClientInput::Dir == Msg::Direction::ClientToServer);
            if (rec.id != Net::ClientInput::Id)
              continue;
            Net::ClientInput in;
            if (!Msg::DecodeRecord(rec, in))
              continue;

            const ECS::EntityId player = sessions.OnInput(world, from, in, tick);

            // Player weapon intent becomes a FireWeapon command on the bus; the
            // combat subscriber resolves it to facts after the receive loop.
            if (in.fire && world.IsValid(player))
              bus.Publish(GameLogic::FireWeapon{ player, GameLogic::Weapon::Laser, Net::NO_MISSILE_TARGET });
            if (in.fireMissile && world.IsValid(player))
              bus.Publish(GameLogic::FireWeapon{ player, GameLogic::Weapon::Missile, in.missileTarget });
          }
          break;
        }
        case Msg::RELIABLE_MAGIC:
          sessions.OnReliable(from, recv, size);
          break;
        default:
          break;
      }
    }

    // Resolve this tick's player fire commands -> Crime / EntityKilled facts
    // (police dispatch and death/destroy happen in their subscribers) before the
    // simulation advances.
    bus.Dispatch();

    if (sessions.Count() != lastSessions)
    {
      lastSessions = sessions.Count();
      printf("Clients connected: %zu\n", lastSessions);
      // Membership changed: replay the full roster to everyone, so a joiner learns
      // the others and the others learn the joiner. (A leaver's ship is removed via
      // EntityDespawn; the client drops its roster entry there.)
      for (const Msg::PlayerInfo& pi : sessions.Roster(world))
        sessions.Broadcast(pi);
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
        Msg::ClientHello hello;
        if (Msg::TryDecode(msg, req))
        {
          const Net::StationResponse resp =
              GameLogic::ProcessStationRequest(world, s.entity, DOCK_RANGE, req);
          s.events.Send(resp);   // Gameplay lane
        }
        else if (Msg::TryDecode(msg, hello))
        {
          // Adopt the client's commander name (sanitized + de-duplicated). If it
          // actually changed, tell everyone via the roster.
          if (sessions.ApplyName(world, s.endpoint, hello.commanderName))
            if (Msg::PlayerInfo pi; sessions.PlayerInfoFor(world, s.entity.index, pi))
              sessions.Broadcast(pi);
        }
      }
    }

    // 2. Advance the authoritative simulation one tick, then run dynamic spawning
    //    (periodic pirate encounters near players).
    GameLogic::Tick(world);
    ++tick;
    spawner.Step(world, tick);

    // Regenerate players' shields/energy on a slow cadence (before combat, so a hit
    // this tick lands on the freshly-regenerated shield).
    if (tick % SHIELD_REGEN_INTERVAL == 0)
      GameLogic::StepShieldRegen(world);

    // 2b. Advance in-flight missiles (homing + detonation) and realtime combat,
    //     then resolve every resulting kill: broadcast a death event and destroy
    //     the wreck (its removal also rides the despawn diff below).
    std::vector<GameLogic::Kill> kills = GameLogic::StepMissiles(world);
    for (const GameLogic::Kill& k : GameLogic::StepCombat(world))
      kills.push_back(k);
    // Each kill is an EntityKilled fact; the subscriber respawns a player in place
    // or broadcasts the death and destroys the wreck (whose removal also rides the
    // despawn diff below).
    for (const GameLogic::Kill& kill : kills)
      bus.Publish(GameLogic::EntityKilled{ kill.victim, kill.killer });
    bus.Dispatch();

    // 2c. Periodically cool down wanted records; refresh the roster for anyone whose
    //     legal status actually changed.
    if (tick % WANTED_DECAY_INTERVAL == 0)
      for (uint32_t changedId : GameLogic::DecayWanted(world))
        if (Msg::PlayerInfo pi; sessions.PlayerInfoFor(world, changedId, pi))
          sessions.Broadcast(pi);

    // 3. Reap idle clients, then broadcast every despawn (reaped players + props)
    //    as a reliable event to all remaining clients.
    sessions.Reap(world, tick, SESSION_TIMEOUT_TICKS);
    for (uint32_t goneId : despawns.Update(CurrentIds(world)))
      sessions.Broadcast(Msg::EntityDespawn{ goneId });

    // 4. Send each client its own area-of-interest snapshot + reliable event packet.
    aoi.Rebuild(world);
    for (auto& [key, s] : sessions.All())
    {
      Math::Vector3i64 viewerPos{ 0, 0, 0 };
      if (world.IsValid(s.entity))
        viewerPos = world.Get<GameLogic::WorldTransform>(s.entity).position;

      Net::WorldSnapshot snap = aoi.SnapshotFor(world, tick, viewerPos, AOI_RADIUS_CELLS, s.entity.index);
      // Keep the local system's planet/station visible across the whole system, not just
      // the +/-1 ship cell, so the body you are flying toward never pops out.
      AppendLandmarks(world, snap, viewerPos, landmarks);
      for (const std::vector<uint8_t>& datagram : Net::PacketizeSnapshot(snap))
        socket.SendTo(s.endpoint, datagram.data(), datagram.size());

      // The owner's private HUD vitals, on change only (fields for systems not built
      // yet stay zero). Queued on the Gameplay lane; flushed with the events below.
      Msg::PlayerStatus ps;
      if (world.IsValid(s.entity))
      {
        if (const auto* c = world.TryGet<GameLogic::Combatant>(s.entity)) ps.energy = c->energy;
        if (const auto* sh = world.TryGet<GameLogic::Shields>(s.entity)) { ps.frontShield = sh->front; ps.aftShield = sh->aft; }
        if (const auto* wal = world.TryGet<GameLogic::Wallet>(s.entity)) ps.credits = wal->credits;
        if (const auto* eq = world.TryGet<GameLogic::Equipment>(s.entity)) ps.missiles = eq->missiles;
        if (const auto* h = world.TryGet<GameLogic::CargoHold>(s.entity)) ps.cargoUsed = GameLogic::TotalTonnage(*h);
        if (const auto* wnt = world.TryGet<GameLogic::Wanted>(s.entity)) ps.wantedLevel = wnt->level;
        if (const auto* pr = world.TryGet<GameLogic::PlayerRecord>(s.entity)) ps.score = pr->score;
      }
      auto lastIt = lastStatus.find(key);
      if (lastIt == lastStatus.end() || lastIt->second.Fields() != ps.Fields())
      {
        s.events.Send(ps);
        lastStatus[key] = ps;
      }

      for (const std::vector<uint8_t>& dg : s.events.WriteDatagrams())
        socket.SendTo(s.endpoint, dg.data(), dg.size());
    }

    // Drop cached status for endpoints that are no longer sessions (reaped clients),
    // so the change-cache can't grow without bound over a long uptime.
    std::erase_if(lastStatus, [&](const auto& _kv) { return sessions.All().count(_kv.first) == 0; });

    Sleep(33);   // ~30 Hz tick
  }
}
