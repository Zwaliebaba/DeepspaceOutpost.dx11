#pragma once

// ServerSessions - the server's per-client connection table (GameLogic).
//
// Replaces the single hardcoded client with a real multi-client model: each
// connected client is a Session keyed by its UDP endpoint, owning the entity it
// controls, its own reliable event channel, and its input/liveness bookkeeping.
// Since B1 a client connects through the FRONT DOOR: a valid, version-checked
// ClientHello (OnHello) spawns its entity and replies with HelloAck. An unknown
// endpoint that sends input is ignored; a reliable datagram from one gets only a
// pending (entity-less) shell so its hello can be received. Idle clients (and
// shells that never hello) are reaped.
//
// Pure apart from the world it mutates (spawns/destroys entities) - no sockets -
// so connection handling, input application and reaping are all unit-tested
// headlessly; the server loop wires the socket I/O around it.

#include <algorithm>
#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

#include "ECS.h"
#include "NetLib.h"            // Net::Endpoint (winsock-free)
#include "Messages/Defs/InputCommand.h"
#include "ReliableChannel.h"
#include "Messages/Framing.h"        // Msg::PROTOCOL_VERSION (the hello version check)
#include "Messages/Defs/GalaxyChunks.h"   // Net::GalaxySystemInfo / GalaxyChunk pull protocol
#include "Messages/MessageEndpoint.h" // Msg::MessageEndpoint (Control/Gameplay/Bulk lanes)
#include "Messages/Defs/CoreEvents.h"   // lifecycle events (kept for consumers' transitive use)
#include "Messages/Defs/PlayerSession.h" // Msg::ClientHello / HelloAck / HelloReject / PlayerInfo

#include "SimComponents.h"
#include "FlightInput.h"       // FlightIntent / FlightCaps (applied in OnInput/SpawnPlayer)
#include "StationServices.h"
#include "CombatSystem.h"
#include "EquipmentSystem.h"   // ShipGear (laser heat + ECM recharge, G8)

namespace Neuron::GameLogic
{
  // Stable key for a client endpoint (address in the high bits, port in the low).
  [[nodiscard]] inline uint64_t EndpointKey(const Net::Endpoint& _ep)
  {
    return (static_cast<uint64_t>(_ep.address) << 16) | static_cast<uint64_t>(_ep.port);
  }

  // Longest commander name the server stores/replicates (client input is capped and
  // sanitized to this before de-duplication).
  inline constexpr std::size_t MAX_NAME_LEN = 20;

  struct Session
  {
    Net::Endpoint endpoint;
    ECS::EntityId entity;               // INVALID until a valid ClientHello spawns it (pending shell)
    Msg::MessageEndpoint events;       // reliable lanes (Control/Gameplay/Bulk) to THIS client
    std::string name;                  // display name (from ClientHello; placeholder if blank)
    uint32_t lastInputSeq = 0;         // newest input applied (drops stale)
    uint32_t lastSeenTick = 0;         // for idle reaping

    // A session is LIVE once its ClientHello spawned an entity; before that it is
    // a pending shell that exists only to receive that reliable hello.
    [[nodiscard]] bool Live() const { return entity.index != ECS::INVALID_INDEX; }
  };

  // What OnHello did with a ClientHello.
  enum class HelloResult : uint8_t
  {
    Rejected,     // version mismatch: a HelloReject was queued, no entity spawned
    Accepted,     // the front door: spawned the entity, queued HelloAck
    NameChanged,  // a hello on an already-live session: just a rename
  };

  struct HelloOutcome
  {
    HelloResult result = HelloResult::Rejected;
    ECS::EntityId entity{};
    bool nameChanged = false;
  };

  class ServerSessions
  {
  public:
    // Handle an input datagram from `_ep`. Since B1 input NEVER connects a client:
    // an unknown endpoint is ignored (the ClientHello is the front door - see
    // OnHello). For a live session the latest intent is applied. Returns the
    // session's entity, or an INVALID id for an unknown/pending endpoint.
    ECS::EntityId OnInput(ECS::Registry& _world, const Net::Endpoint& _ep,
                          const Msg::InputCommand& _in, uint32_t _tick)
    {
      auto it = m_sessions.find(EndpointKey(_ep));
      if (it == m_sessions.end())
        return ECS::EntityId{};   // ignore unknown endpoints (no spawn-on-input)

      Session& session = it->second;
      session.lastSeenTick = _tick;
      if (_in.sequence > session.lastInputSeq && _world.IsValid(session.entity))
      {
        session.lastInputSeq = _in.sequence;
        FlightIntent& fi = _world.Get<FlightIntent>(session.entity);
        fi.rollAxis = _in.rollAxis;
        fi.pitchAxis = _in.pitchAxis;
        fi.throttle = _in.throttle;
      }
      return session.entity;
    }

    // Route an inbound reliable datagram (a client ack, or a ClientHello from a
    // brand-new endpoint) to its session. An unknown endpoint gets a PENDING shell
    // (a reliable receive endpoint, no entity) so its hello can be received and
    // version-checked; the entity spawns only on a valid hello (OnHello). A shell
    // that never sends one is reaped on the idle timeout. (A reliable datagram is
    // still a cheap allocation for an unauthenticated peer; per-endpoint rate
    // limiting lands with the session token in B2.)
    void OnReliable(const Net::Endpoint& _ep, const uint8_t* _data, std::size_t _size, uint32_t _tick)
    {
      Session& s = m_sessions.try_emplace(EndpointKey(_ep)).first->second;
      s.endpoint = _ep;
      s.lastSeenTick = _tick;
      s.events.OnDatagram(_data, _size);
    }

    // Handle a decoded ClientHello - the front door. Provisions the shell if
    // absent (so tests can call this directly). Version-checks: on mismatch a
    // HelloReject is queued and the (entity-less) session is left to reap. On a
    // first valid hello the player entity is spawned, the name adopted, and a
    // HelloAck queued. A hello on an already-live session is a rename.
    HelloOutcome OnHello(ECS::Registry& _world, const Net::Endpoint& _ep,
                         const Msg::ClientHello& _hello, uint32_t _tick)
    {
      const uint64_t key = EndpointKey(_ep);
      Session& s = m_sessions.try_emplace(key).first->second;
      s.endpoint = _ep;
      s.lastSeenTick = _tick;

      if (_hello.protocolVersion != Msg::PROTOCOL_VERSION)
      {
        s.events.Send(Msg::HelloReject{ static_cast<uint8_t>(Msg::HelloRejectReason::ProtocolMismatch) });
        return HelloOutcome{ HelloResult::Rejected, {}, false };
      }

      if (s.Live())
      {
        HelloOutcome out{ HelloResult::NameChanged, s.entity, false };
        out.nameChanged = ApplyNameTo(_world, s, key, _hello.commanderName);
        return out;
      }

      // First valid hello: spawn the controllable entity and adopt the name.
      s.entity = SpawnPlayer(_world);
      const std::string clean = SanitizeName(_hello.commanderName);
      s.name = clean.empty() ? ("Commander-" + std::to_string(s.entity.index))
                             : UniqueName(clean, key);
      if (PlayerRecord* pr = _world.TryGet<PlayerRecord>(s.entity))
        pr->name = s.name;
      s.events.Send(Msg::HelloAck{ /*sessionToken*/ 0, s.entity.index,
                                   static_cast<uint16_t>(Msg::PROTOCOL_VERSION) });   // Control lane
      return HelloOutcome{ HelloResult::Accepted, s.entity, true };
    }

    // Drop sessions idle for more than `_timeoutTicks`, destroying their entities.
    // Returns the destroyed entity indices (so the caller can broadcast despawns).
    std::vector<uint32_t> Reap(ECS::Registry& _world, uint32_t _tick, uint32_t _timeoutTicks)
    {
      std::vector<uint32_t> gone;
      for (auto it = m_sessions.begin(); it != m_sessions.end();)
      {
        if (_tick - it->second.lastSeenTick > _timeoutTicks)
        {
          if (_world.IsValid(it->second.entity))
          {
            gone.push_back(it->second.entity.index);
            _world.Destroy(it->second.entity);
          }
          it = m_sessions.erase(it);
        }
        else
        {
          ++it;
        }
      }
      return gone;
    }

    // Queue a catalog message onto every session's lanes (e.g. a despawn or death
    // that everyone must see exactly once); routed to the message's lane.
    template <Msg::Message M>
    void Broadcast(const M& _m)
    {
      for (auto& entry : m_sessions)
        entry.second.events.Send(_m);
    }

    // Apply a client's ClientHello name to its session + authoritative PlayerRecord:
    // sanitize, cap, de-duplicate against the other sessions. Returns true when the
    // stored name actually changed (so the caller broadcasts an updated PlayerInfo).
    // A blank/all-control name, or an unknown endpoint, keeps the assigned default.
    bool ApplyName(ECS::Registry& _world, const Net::Endpoint& _ep, const std::string& _raw)
    {
      const uint64_t key = EndpointKey(_ep);
      auto it = m_sessions.find(key);
      if (it == m_sessions.end())
        return false;
      return ApplyNameTo(_world, it->second, key, _raw);
    }

    // The full roster (one PlayerInfo per live session) to replay to a joiner and
    // broadcast on membership changes.
    [[nodiscard]] std::vector<Msg::PlayerInfo> Roster(ECS::Registry& _world) const
    {
      std::vector<Msg::PlayerInfo> out;
      out.reserve(m_sessions.size());
      for (const auto& e : m_sessions)
        if (Msg::PlayerInfo pi; BuildPlayerInfo(_world, e.second, pi))
          out.push_back(std::move(pi));
      return out;
    }

    // Build the current PlayerInfo for the session controlling `_entityIndex` (for a
    // targeted broadcast after that player's name or wanted level changed). Returns
    // false if no live session owns that entity.
    bool PlayerInfoFor(ECS::Registry& _world, uint32_t _entityIndex, Msg::PlayerInfo& _out) const
    {
      for (const auto& e : m_sessions)
        if (e.second.entity.index == _entityIndex)
          return BuildPlayerInfo(_world, e.second, _out);
      return false;
    }

    // Set the galaxy manifest clients pull from (static for the world's
    // lifetime; built once at startup from the generated galaxy).
    void SetManifest(std::vector<Net::GalaxySystemInfo> _manifest) { m_manifest = std::move(_manifest); }

    // Answer a client's GalaxyChunkRequest: send the requested range as one or
    // more GalaxyChunk messages on the Bulk lane, each at most
    // GALAXY_CHUNK_MAX_SYSTEMS entries so it fits a safe datagram. The request
    // count is clamped (GALAXY_CHUNK_MAX_REQUEST) so a hostile request can't
    // provoke an unbounded burst; an out-of-range baseIndex is answered with an
    // empty chunk still carrying `total`, so the client always learns the size.
    void SendGalaxyChunks(Session& _s, uint32_t _baseIndex, uint16_t _count)
    {
      const uint32_t total = static_cast<uint32_t>(m_manifest.size());

      if (_baseIndex >= total)
      {
        Msg::GalaxyChunk empty;
        empty.total = total;
        empty.baseIndex = _baseIndex;
        _s.events.Send(empty);   // Bulk lane
        return;
      }

      const uint16_t wanted = _count == 0 ? Msg::GALAXY_CHUNK_MAX_REQUEST
                                          : std::min(_count, Msg::GALAXY_CHUNK_MAX_REQUEST);
      const uint32_t end = std::min<uint32_t>(_baseIndex + wanted, total);

      for (uint32_t base = _baseIndex; base < end; base += Msg::GALAXY_CHUNK_MAX_SYSTEMS)
      {
        Msg::GalaxyChunk chunk;
        chunk.total = total;
        chunk.baseIndex = base;
        const uint32_t n = std::min<uint32_t>(Msg::GALAXY_CHUNK_MAX_SYSTEMS, end - base);
        chunk.systems.reserve(n);
        for (uint32_t i = 0; i < n; ++i)
          chunk.systems.push_back(Msg::ToWireEntry(m_manifest[base + i]));
        _s.events.Send(chunk);   // Bulk lane
      }
    }

    [[nodiscard]] std::unordered_map<uint64_t, Session>& All() { return m_sessions; }
    [[nodiscard]] std::size_t Count() const { return m_sessions.size(); }
    [[nodiscard]] bool Has(const Net::Endpoint& _ep) const
    {
      return m_sessions.find(EndpointKey(_ep)) != m_sessions.end();
    }

  private:
    // Sanitize/de-dupe `_raw` and mirror it onto the session + PlayerRecord.
    // Returns true if the stored name actually changed. Shared by ApplyName (live
    // rename) and OnHello (a rename on an already-connected session).
    bool ApplyNameTo(ECS::Registry& _world, Session& _s, uint64_t _key, const std::string& _raw)
    {
      std::string clean = SanitizeName(_raw);
      if (clean.empty())
        return false;
      clean = UniqueName(clean, _key);
      if (clean == _s.name)
        return false;

      _s.name = clean;
      if (PlayerRecord* pr = _world.TryGet<PlayerRecord>(_s.entity))
        pr->name = clean;
      return true;
    }

    // Spawn a controllable player entity, spread out so clients don't overlap.
    ECS::EntityId SpawnPlayer(ECS::Registry& _world)
    {
      const int64_t offset = static_cast<int64_t>(m_spawnCount++) * 2000;
      const ECS::EntityId e = _world.Create();
      _world.Add<WorldTransform>(e, WorldTransform{ { offset, 0, 0 } });
      _world.Add<Flight>(e, Flight{});
      _world.Add<FlightIntent>(e, FlightIntent{});
      _world.Add<FlightCaps>(e, FlightCaps{});
      // Commerce state so the player can dock, trade and equip.
      _world.Add<Wallet>(e, Wallet{});
      _world.Add<CargoHold>(e, CargoHold{});
      _world.Add<DockState>(e, DockState{});
      _world.Add<Equipment>(e, Equipment{});
      _world.Add<Fuel>(e, Fuel{});   // full hyperspace tank (G7)
      // Combat/faction state: a player is on the Player team, fires only on
      // command (autoEngage = false), and starts with a clean record.
      _world.Add<PlayerTag>(e, PlayerTag{});
      _world.Add<Combatant>(e, Combatant{ Team::Player, /*energy*/ MAX_ENERGY, /*laser*/ 10, /*range*/ 6000, /*autoEngage*/ false });
      // Spawn protection: brief immunity so connecting into nearby hostiles isn't
      // an instant death.
      _world.Get<Combatant>(e).invulnTicks = RESPAWN_GRACE_TICKS;
      _world.Add<Shields>(e, Shields{});   // full directional shields (player-only feature)
      _world.Add<ShipGear>(e, ShipGear{}); // laser temperature + ECM recharge (G8)
      _world.Add<Wanted>(e, Wanted{});
      _world.Add<PlayerRecord>(e, PlayerRecord{});   // name filled in by the caller
      _world.Add<NetType>(e, NetType{ ShipType::Viper });
      return e;
    }

    // Fill `_out` with a session's roster entry (entity + name + legal status).
    // Returns false when the session's entity is no longer valid.
    [[nodiscard]] static bool BuildPlayerInfo(ECS::Registry& _world, const Session& _s, Msg::PlayerInfo& _out)
    {
      if (!_world.IsValid(_s.entity))
        return false;
      _out.entityId = _s.entity.index;
      _out.name = _s.name;
      const Wanted* w = _world.TryGet<Wanted>(_s.entity);
      _out.wantedLevel = (w != nullptr) ? w->level : 0;
      return true;
    }

    // Keep only printable ASCII, drop control bytes, cap length and trim trailing
    // spaces - so a hostile or empty name can't inject control chars or run long.
    [[nodiscard]] static std::string SanitizeName(const std::string& _raw)
    {
      std::string out;
      for (char c : _raw)
      {
        const unsigned char u = static_cast<unsigned char>(c);
        if (u >= 0x20 && u < 0x7F)
          out.push_back(c);
        if (out.size() >= MAX_NAME_LEN)
          break;
      }
      while (!out.empty() && out.back() == ' ')
        out.pop_back();
      return out;
    }

    // Make `_base` unique among the OTHER sessions by appending -2, -3, ... .
    [[nodiscard]] std::string UniqueName(const std::string& _base, uint64_t _selfKey) const
    {
      auto taken = [&](const std::string& _n)
      {
        for (const auto& e : m_sessions)
          if (e.first != _selfKey && e.second.name == _n)
            return true;
        return false;
      };
      if (!taken(_base))
        return _base;
      for (int i = 2; i < 1000; ++i)
      {
        std::string cand = _base + "-" + std::to_string(i);
        if (!taken(cand))
          return cand;
      }
      return _base;   // astronomically unlikely; give up rather than loop forever
    }

    std::unordered_map<uint64_t, Session> m_sessions;
    std::vector<Net::GalaxySystemInfo> m_manifest;   // galaxy chart data for new clients
    uint32_t m_spawnCount = 0;
  };
}
