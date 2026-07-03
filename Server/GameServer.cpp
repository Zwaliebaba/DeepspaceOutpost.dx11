// GameServer - the dedicated server's orchestrator. See GameServer.h.

#include "GameServer.h"

#include <cstdio>
#include <ranges>

#include "SnapshotPacketizer.h"
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

using namespace Neuron;

namespace DSOServer
{
  GameServer::GameServer(Net::UdpSocket& _socket)
    : m_socket(_socket)
    , m_aoi(Cfg::AOI_CELL_SIZE)
    , m_spawner(Cfg::SPAWN_SEED, Cfg::PIRATE_SPAWN_INTERVAL, Cfg::MAX_NPCS)
    , m_lootRng(Cfg::LOOT_SEED)
    , m_aiRng(Cfg::AI_SEED)
    , m_hyperRng(Cfg::HYPER_SEED)
  {
    // The authoritative world: home system + procedural galaxy; every client
    // gets the chart manifest on connect.
    WorldSetup setup = BuildWorld(m_world);
    m_landmarks = std::move(setup.landmarks);
    m_sessions.SetManifest(std::move(setup.manifest));

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
  }

  void GameServer::RunTick()
  {
    // 1. Receive client input and acks. Input from an endpoint that hasn't
    //    completed the ClientHello handshake is ignored (the hello is the front
    //    door - see ProcessReliableRequests). Then resolve this tick's fire
    //    commands -> Crime / EntityKilled facts (police dispatch and death/destroy
    //    happen in their subscribers) before the simulation advances.
    ReceiveDatagrams();
    m_bus.Dispatch();

    // 1b. Process the reliable channel: the ClientHello handshake (which connects
    //     a client and broadcasts the refreshed roster), plus station/travel/chart
    //     requests from already-connected sessions.
    ProcessReliableRequests();

    // 2. NPC tactics + the simulation tick + dynamic spawning + shield regen.
    AdvanceSimulation();

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
  }

  // --- receive ---------------------------------------------------------------

  void GameServer::ReceiveDatagrams()
  {
    Server::PumpDatagrams(m_socket, m_recv, sizeof(m_recv), Cfg::RECV_BUDGET, m_routes);
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

      const ECS::EntityId player = m_sessions.OnInput(m_world, _from, in, m_tick);

      // Player weapon/equipment intent becomes FireWeapon commands on the bus;
      // the combat subscriber resolves them to facts after the receive loop.
      if (m_world.IsValid(player))
      {
        if (in.fire)
          m_bus.Publish(GameLogic::FireWeapon{ player, GameLogic::Weapon::Laser, Msg::NO_MISSILE_TARGET });
        if (in.fireMissile)
          m_bus.Publish(GameLogic::FireWeapon{ player, GameLogic::Weapon::Missile, in.missileTarget });
        if (in.ecm)
          m_bus.Publish(GameLogic::FireWeapon{ player, GameLogic::Weapon::Ecm, Msg::NO_MISSILE_TARGET });
        if (in.energyBomb)
          m_bus.Publish(GameLogic::FireWeapon{ player, GameLogic::Weapon::EnergyBomb, Msg::NO_MISSILE_TARGET });
        if (in.escapePod)
          m_bus.Publish(GameLogic::FireWeapon{ player, GameLogic::Weapon::EscapePod, Msg::NO_MISSILE_TARGET });
      }
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
        // one connects a pending client (spawns its entity, queues HelloAck) or
        // renames a live one. A version mismatch is rejected inside OnHello.
        Msg::ClientHello hello;
        if (Msg::TryDecode(msg, hello))
        {
          const GameLogic::HelloOutcome out = m_sessions.OnHello(m_world, s.endpoint, hello, m_tick);
          if (out.result == GameLogic::HelloResult::Accepted)
          {
            printf("Client connected: entity %u (\"%s\")\n", out.entity.index, s.name.c_str());
            // Replay the full roster: the joiner learns everyone, everyone learns
            // the joiner. (A leaver's ship goes out as EntityDespawn on reap.)
            for (const Msg::PlayerInfo& pi : m_sessions.Roster(m_world))
              m_sessions.Broadcast(pi);
          }
          else if (out.result == GameLogic::HelloResult::NameChanged && out.nameChanged)
          {
            BroadcastPlayerInfo(out.entity.index);
          }
          continue;
        }

        // Everything else is gameplay: a pending (pre-hello) shell can't do it, so
        // ignore until the session is live.
        if (!s.Live())
          continue;

        Net::StationRequest req;
        Msg::TravelRequest travel;
        Msg::GalaxyChunkRequest chunkReq;
        if (Msg::TryDecode(msg, req))
          HandleStationRequest(s, req);
        else if (Msg::TryDecode(msg, travel))
          HandleTravelRequest(s, travel);
        else if (Msg::TryDecode(msg, chunkReq))
          m_sessions.SendGalaxyChunks(s, chunkReq.baseIndex, chunkReq.count);   // Bulk lane
      }
    }
  }

  void GameServer::HandleStationRequest(GameLogic::Session& _session, const Net::StationRequest& _req)
  {
    // Docking + commerce only. The retired travel kinds (Teleport/JumpDrive)
    // fall through to the station dispatcher, which rejects them - travel rides
    // TravelRequest (HandleTravelRequest) since the protocol split.
    const Net::StationResponse resp =
        GameLogic::ProcessStationRequest(m_world, _session.entity, Cfg::DOCK_RANGE, _req);
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

    _session.events.Send(resp);   // Gameplay lane
  }

  // --- simulation --------------------------------------------------------------

  void GameServer::AdvanceSimulation()
  {
    // NPC tactics decide their flight intents (pursue/break-off/flee + panic
    // missiles), then the simulation advances one tick - the same
    // intent->caps->flight path a client's input takes. Fled ships despawn
    // inside StepAi; their removal rides the despawn diff.
    GameLogic::StepAi(m_world, m_tick, m_aiRng);
    GameLogic::Tick(m_world);
    ++m_tick;
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
    std::vector<uint32_t> ecmPulses;
    std::vector<GameLogic::Kill> kills = GameLogic::StepMissiles(m_world, m_aiRng, ecmPulses);
    for (uint32_t defender : ecmPulses)
      m_sessions.Broadcast(Msg::EcmPulse{ defender });
    for (const GameLogic::Kill& k : GameLogic::StepCombat(m_world))
      kills.push_back(k);
    for (const GameLogic::Kill& k : GameLogic::StepCollisions(m_world))
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
    GameLogic::StepLoot(m_world);
    for (uint32_t scoopedBy : GameLogic::ScoopSystem(m_world))
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
    m_sessions.Reap(m_world, m_tick, Cfg::SESSION_TIMEOUT_TICKS);
    for (uint32_t goneId : m_despawns.Update(CurrentIds(m_world)))
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
        AppendLandmarks(m_world, snap, viewerPos, m_landmarks);
        for (const std::vector<uint8_t>& datagram : Net::PacketizeSnapshot(snap))
          m_socket.SendTo(s.endpoint, datagram.data(), datagram.size());

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
        if (const auto* pr = m_world.TryGet<GameLogic::PlayerRecord>(s.entity)) ps.score = pr->score;
        if (const auto* g = m_world.TryGet<GameLogic::ShipGear>(s.entity)) ps.laserTemp = g->laserHeat;
        if (m_lastStatus.Changed(key, ps))
          s.events.Send(ps);
      }

      for (const std::vector<uint8_t>& dg : s.events.WriteDatagrams())
        m_socket.SendTo(s.endpoint, dg.data(), dg.size());
    }

    // Drop cached status for endpoints that are no longer sessions (reaped
    // clients), so the change-cache can't grow without bound over a long uptime.
    m_lastStatus.Prune([this](uint64_t _key) { return m_sessions.All().count(_key) != 0; });
  }

  // --- combat subscribers ---------------------------------------------------------

  void GameServer::RegisterSubscribers()
  {
    // A FireWeapon command resolves against the authoritative world (laser
    // geometry / missile spawn unchanged), publishing the resulting facts.
    m_bus.Subscribe<GameLogic::FireWeapon>([this](const GameLogic::FireWeapon& _fw)
    {
      GameLogic::ResolveFireWeapon(m_world, m_bus, _fw, Cfg::FIRE_RANGE, Cfg::AIM_CONE);
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
          break;
        }

      // Pay the killer a wanted-derived bounty (if the victim was a fugitive)
      // BEFORE the record is wiped by the respawn below; a clean-player kill
      // pays nothing but still bumps the killer's score.
      GameLogic::CreditKill(m_world, _k.killer, _k.victim);

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
        GameLogic::DropPlayerCargo(m_world, _k.victim, pt->position, m_lootRng);
      GameLogic::RespawnAtNearestStation(m_world, _k.victim);

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
      GameLogic::CreditKill(m_world, _k.killer, _k.victim);
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
