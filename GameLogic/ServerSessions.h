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
// Since B2 the identity is the SESSION TOKEN, not the endpoint. HelloAck hands the
// client a random token (from an injected CSPRNG source); every subsequent client
// datagram carries it, and the server authenticates by token before touching a
// session - a spoofed source address with the wrong/no token is dropped. The
// endpoint is just the current return address: a correctly-tokened datagram from a
// new address re-binds the session there (a NAT rebind heals silently). Token-less
// (pre-handshake) datagrams are rate-limited per endpoint so a spoofed-source flood
// can't provision unbounded shells.
//
// Since C a connected player is a PLAYER, not a hull: each accepted session is
// allocated a PlayerId (the §12 "Account → Empire → owns N entities" identity;
// stable across B3 reconnects, carried in HelloAck/PlayerInfo). Every entity the
// player owns - today exactly the one ship, later the F-track's drones/outposts -
// carries Owner{playerId} and is registered in the ECS::OwnershipIndex, so "all my
// units" is O(mine). Reaping a session destroys everything it owns.
//
// Pure apart from the world it mutates (spawns/destroys entities) - no sockets -
// so connection handling, input application and reaping are all unit-tested
// headlessly; the server loop wires the socket I/O around it.

#include <algorithm>
#include <cstdint>
#include <functional>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include "ECS.h"
#include "OwnershipIndex.h"    // ECS::OwnershipIndex (PlayerId -> owned entities, C)
#include "NetLib.h"            // Net::Endpoint (winsock-free)
#include "Messages/Defs/InputCommand.h"
#include "ReliableChannel.h"
#include "Messages/Framing.h"        // Msg::PROTOCOL_VERSION (the hello version check)
#include "Messages/Defs/GalaxyChunks.h"   // Net::GalaxySystemInfo / GalaxyChunk pull protocol
#include "Messages/MessageEndpoint.h" // Msg::MessageEndpoint (Control/Gameplay/Bulk lanes)
#include "SnapshotStream.h"           // Net::SnapshotStreamEncoder (per-session delta stream, E2b)
#include "Messages/Defs/CoreEvents.h"   // lifecycle events (kept for consumers' transitive use)
#include "Messages/Defs/PlayerSession.h" // Msg::ClientHello / HelloAck / HelloReject / PlayerInfo

#include "SimComponents.h"
#include "FlightInput.h"       // FlightIntent / FlightCaps (SpawnPlayer components; SafeParkSilent zeroes)
#include "StationServices.h"
#include "CombatSystem.h"
#include "EquipmentSystem.h"   // ShipGear (laser heat + ECM recharge, G8)
#include "CabinHeatSystem.h"   // CabinHeat (sun proximity heat, G4)
#include "ChatModeration.h"    // ChatLimiter (per-session chat rate limit, G3)

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

  // Per-endpoint rate limit on token-less (pre-handshake) datagrams. Over the cap in
  // a window, the endpoint is muted for a cooldown - a spoofed-source flood can't
  // provision unbounded shells or reply traffic. (Authenticated traffic is not rate
  // limited here; it is already bound to a real session.) The cap is generous: a
  // legitimate client resends its unacked hello once per client frame until HelloAck
  // arrives (~one round trip), which stays well under the cap even at a high frame
  // rate, while a genuine flood is orders of magnitude more.
  inline constexpr uint32_t RATE_WINDOW_TICKS = 30;    // ~1 s at 30 Hz
  inline constexpr uint16_t RATE_MAX_UNAUTH   = 120;   // token-less datagrams / window / endpoint
  inline constexpr uint32_t RATE_MUTE_TICKS   = 300;   // ~10 s mute once the cap is exceeded

  // D4: how many authenticated InputCommands one session may have applied per
  // server tick. Input is latest-wins (extra inputs are superseded anyway), so this
  // just bounds the CPU a flooding client can cost us; a normal client at a high
  // frame rate sends only a few inputs per 30 Hz tick, well under the cap.
  inline constexpr uint16_t MAX_INPUTS_PER_TICK = 8;

  struct Session
  {
    Net::Endpoint endpoint;            // current return address (B2: updated on a tokened rebind)
    ECS::EntityId entity;               // INVALID until a valid ClientHello spawns it (pending shell)
    Msg::MessageEndpoint events;       // reliable lanes (Control/Gameplay/Bulk) to THIS client
    std::string name;                  // display name (from ClientHello; placeholder if blank)
    int score = 0;                     // kill score (C2: a PLAYER-level record, off the hull;
                                       //   fed by CreditKill via AddScore, persisted by B4)
    uint64_t token = 0;                // session token (B2 identity); 0 while a pending shell
    uint32_t playerId = 0;             // player identity (C); 0 while a pending/loading shell.
                                       //   Stable across B3 reconnects; every owned entity's
                                       //   Owner component carries it (see OwnershipIndex).
    uint32_t lastInputSeq = 0;         // newest input applied (drops stale)
    uint32_t lastSeenTick = 0;         // for idle reaping
    uint32_t rttMs = 0;                // E1: the client's own reported round-trip time
                                       //   (from Ping). Used, clamped to the transform-
                                       //   history window, to rewind targets for lag-
                                       //   compensated fire. Raw here; the consumer clamps.
    uint32_t inputWindowTick = 0;      // D4: tick the input-cadence window opened
    uint16_t inputsThisWindow = 0;     // D4: inputs applied this tick (capped)
    ChatLimiter chat;                  // G3: per-session chat rate-limit window
    bool loading = false;              // B4: version-checked, awaiting a persistence load before spawn
    uint32_t ackedSnapshotTick = 0;    // E2b: latest snapshot tick this client holds as a baseline
    Net::SnapshotStreamEncoder snapshotEncoder;   // E2b: per-session delta/keyframe snapshot stream

    // A session is LIVE once its ClientHello spawned an entity; before that it is
    // a pending shell that exists only to receive that reliable hello.
    [[nodiscard]] bool Live() const { return entity.index != ECS::INVALID_INDEX; }
  };

  // What OnHello did with a ClientHello.
  enum class HelloResult : uint8_t
  {
    Rejected,   // version mismatch: a HelloReject was queued, no entity spawned
    Accepted,   // the front door: spawned the entity, queued HelloAck
    Resumed,    // a hello on an already-live session = a reconnect (B3): the entity
                // is kept, a fresh HelloAck queued, and (maybe) the name changed.
    Loading,    // deferred-spawn mode (B4): version OK, but the entity is NOT spawned
                // yet - the caller loads the commander first, then calls SpawnLoaded.
  };

  struct HelloOutcome
  {
    HelloResult result = HelloResult::Rejected;
    ECS::EntityId entity{};
    bool nameChanged = false;   // Resumed only: the reconnect also renamed
  };

  class ServerSessions
  {
  public:
    // Handle a heartbeat datagram carrying session `_token` from `_ep`. Input is
    // authenticated (B2): a token-less (0) or wrong/unknown token is ignored - only
    // a live, correctly-tokened session is touched. A fresh heartbeat marks the
    // session seen (liveness for SafeParkSilent/Reap) and applies its snapshot ack;
    // movement rides UnitOrders and abilities ride AbilityRequests, so no flight
    // intent is written here. The endpoint re-binds to `_ep` if the token arrived
    // from a new address. Returns the session's entity, or an INVALID id when
    // unauthenticated / not yet live.
    ECS::EntityId OnInput(ECS::Registry& _world, const Net::Endpoint& _ep, uint64_t _token,
                          const Msg::InputCommand& _in, uint32_t _tick)
    {
      Session* session = Authenticate(_ep, _token, _tick);
      if (session == nullptr || !_world.IsValid(session->entity))
        return ECS::EntityId{};   // unauthenticated / unknown token / not yet live

      // D4 cadence cap: drop inputs beyond the per-tick budget (a new tick resets
      // it). Latest-wins means the survivors already carry the freshest intent, so
      // this only sheds a flood's CPU, never a normal client's inputs.
      if (_tick != session->inputWindowTick)
      {
        session->inputWindowTick = _tick;
        session->inputsThisWindow = 0;
      }
      if (session->inputsThisWindow >= MAX_INPUTS_PER_TICK)
        return ECS::EntityId{};   // capped: drop this input (and its ack) entirely
      ++session->inputsThisWindow;

      if (_in.sequence > session->lastInputSeq)
      {
        session->lastInputSeq = _in.sequence;
        session->ackedSnapshotTick = _in.ackSnapshotTick;   // E2b: freshest input carries the freshest ack
      }
      return session->entity;
    }

    // Route an inbound reliable datagram to its session, authenticated by the token
    // in its header (B2 - peeked here, before any decode):
    //  * token == 0 (pre-handshake): the ClientHello front door only. Rate-limited
    //    per endpoint; provisions a PENDING shell (reliable receive endpoint, no
    //    entity) so the hello can be received and version-checked (OnHello spawns
    //    the entity). A token-less datagram is NEVER routed to a live session - a
    //    live client must present its token.
    //  * token != 0: authenticated. Routed to the owning session (re-binding its
    //    endpoint on a NAT rebind); an unknown/spoofed token is dropped.
    void OnReliable(const Net::Endpoint& _ep, const uint8_t* _data, std::size_t _size, uint32_t _tick)
    {
      const uint64_t token = Msg::PeekReliableToken(_data, _size);
      if (token == 0)
      {
        if (RateLimited(_ep, _tick))
          return;   // muted: spoofed-source flood control
        auto it = m_sessions.find(EndpointKey(_ep));
        if (it != m_sessions.end() && it->second.Live())
          return;   // a live session must present its token; ignore token-less traffic
        Session& s = m_sessions.try_emplace(EndpointKey(_ep)).first->second;
        s.endpoint = _ep;
        s.lastSeenTick = _tick;
        s.events.OnDatagram(_data, _size);
        return;
      }

      Session* s = Authenticate(_ep, token, _tick);
      if (s == nullptr)
        return;   // unknown/spoofed token: drop before any decode
      s->events.OnDatagram(_data, _size);
    }

    // Install the source of session tokens (B2). The dedicated server injects an OS
    // CSPRNG (BCryptGenRandom); left unset, a deterministic nonzero fallback is used
    // (headless tests only - a predictable token is safe only where there's no
    // adversary). A source that returns 0 falls back too (tokens must be nonzero).
    void SetTokenSource(std::function<uint64_t()> _src) { m_tokenSource = std::move(_src); }

    // Handle a decoded ClientHello - the front door. Provisions the shell if
    // absent (so tests can call this directly). Version-checks: on mismatch a
    // HelloReject is queued and the (entity-less) session is left to reap. On a
    // first valid hello the player entity is spawned, the name adopted, and a
    // HelloAck queued. A hello on an already-live session is a RECONNECT (B3):
    // the entity is kept and a fresh HelloAck queued (the caller resends the rest).
    //
    // `_deferSpawn` (B4 persistence) does NOT spawn on a first valid hello: it just
    // records the sanitized name and returns Loading, so the caller can load the
    // commander's durable state from the store FIRST and then call SpawnLoaded - a
    // returning commander is never spawned-fresh (which a save would then alias).
    HelloOutcome OnHello(ECS::Registry& _world, const Net::Endpoint& _ep,
                         const Msg::ClientHello& _hello, uint32_t _tick, bool _deferSpawn = false)
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
        // A hello on an already-live session is a RECONNECT (B3): the client sends
        // its hello exactly once per connection, so a second one means it came back
        // (its token re-bound the session to the new endpoint in OnReliable). Keep
        // the entity, adopt any new name, and re-queue HelloAck so the client
        // re-confirms its entity + token; the caller resends the rest of the state.
        HelloOutcome out{ HelloResult::Resumed, s.entity, false };
        out.nameChanged = ApplyNameTo(_world, s, key, _hello.commanderName);
        s.events.Send(Msg::HelloAck{ s.token, s.playerId, s.entity.index,
                                     static_cast<uint16_t>(Msg::PROTOCOL_VERSION) });   // Control lane
        return out;
      }

      if (_deferSpawn)
      {
        // Park the session: remember the sanitized (pre-dedup) name as the load key
        // and wait. SpawnLoaded finishes the handshake once the load completes.
        s.loading = true;
        s.name = SanitizeName(_hello.commanderName);   // may be empty (blank commander)
        return HelloOutcome{ HelloResult::Loading, {}, false };
      }

      // First valid hello: allocate the player identity (C), spawn the primary
      // entity owned by it, and adopt the name.
      s.playerId = m_nextPlayerId++;
      s.entity = SpawnPlayer(_world, s.playerId);
      const std::string clean = SanitizeName(_hello.commanderName);
      s.name = clean.empty() ? ("Commander-" + std::to_string(s.entity.index))
                             : UniqueName(clean, key);   // the per-player record (C2): no hull mirror

      // Mint the session token (B2 identity) and index it to this endpoint; every
      // subsequent client datagram must carry it. The endpoint's outgoing datagrams
      // stay token-0 (the client authenticates the server by address, not token).
      s.token = NextToken();
      m_byToken[s.token] = key;
      s.events.Send(Msg::HelloAck{ s.token, s.playerId, s.entity.index,
                                   static_cast<uint16_t>(Msg::PROTOCOL_VERSION) });   // Control lane
      return HelloOutcome{ HelloResult::Accepted, s.entity, true };
    }

    // Finish a deferred (B4) handshake once the commander's load has completed:
    // spawn the entity, adopt the parked name (default/dedup), mint the token, and
    // queue HelloAck - exactly the accept path of OnHello, but split out so the
    // caller can apply the loaded durable state to the returned entity before the
    // first snapshot. Returns the spawned entity, or an invalid id if the endpoint
    // is not (or no longer) a loading session.
    ECS::EntityId SpawnLoaded(ECS::Registry& _world, const Net::Endpoint& _ep, uint32_t _tick)
    {
      const uint64_t key = EndpointKey(_ep);
      auto it = m_sessions.find(key);
      if (it == m_sessions.end() || !it->second.loading)
        return ECS::EntityId{};

      Session& s = it->second;
      s.loading = false;
      s.lastSeenTick = _tick;
      s.playerId = m_nextPlayerId++;
      s.entity = SpawnPlayer(_world, s.playerId);
      const std::string clean = s.name;   // the sanitized (pre-dedup) name parked by OnHello
      s.name = clean.empty() ? ("Commander-" + std::to_string(s.entity.index))
                             : UniqueName(clean, key);   // the per-player record (C2): no hull mirror
      s.token = NextToken();
      m_byToken[s.token] = key;
      s.events.Send(Msg::HelloAck{ s.token, s.playerId, s.entity.index,
                                   static_cast<uint16_t>(Msg::PROTOCOL_VERSION) });   // Control lane
      return s.entity;
    }

    // Drop idle sessions, destroying their entities. An authenticated (LIVE)
    // session gets the longer `_graceTicks` window (B3: it can reconnect and
    // resume within it); a pending pre-hello shell gets the short `_shellTicks`.
    // Returns the destroyed entity indices (so the caller can broadcast despawns).
    std::vector<uint32_t> Reap(ECS::Registry& _world, uint32_t _tick,
                               uint32_t _shellTicks, uint32_t _graceTicks)
    {
      std::vector<uint32_t> gone;
      for (auto it = m_sessions.begin(); it != m_sessions.end();)
      {
        // A live session, or one still awaiting its persistence load, gets the long
        // grace window; a bare pre-hello shell gets the short shell timeout.
        const uint32_t timeout = (it->second.Live() || it->second.loading) ? _graceTicks : _shellTicks;
        if (_tick - it->second.lastSeenTick > timeout)
        {
          // Destroy EVERYTHING the player owns (C) - the primary ship plus any
          // granted units - and drop the ownership entries with the session.
          if (it->second.playerId != 0)
          {
            for (const ECS::EntityId owned : m_ownership.Owned(it->second.playerId))
              if (_world.IsValid(owned))
              {
                gone.push_back(owned.index);
                _world.Destroy(owned);
              }
            m_ownership.Forget(it->second.playerId);
          }
          if (_world.IsValid(it->second.entity))   // safety net: primary should be indexed
          {
            gone.push_back(it->second.entity.index);
            _world.Destroy(it->second.entity);
          }
          if (it->second.token != 0)
            m_byToken.erase(it->second.token);   // drop the token index with the session
          it = m_sessions.erase(it);
        }
        else
        {
          ++it;
        }
      }
      // Drop stale rate-limit records (no longer muted and long idle) so the map
      // can't grow without bound over a long uptime.
      for (auto it = m_rate.begin(); it != m_rate.end();)
      {
        if (_tick >= it->second.mutedUntil && _tick - it->second.windowStart > _shellTicks)
          it = m_rate.erase(it);
        else
          ++it;
      }
      return gone;
    }

    // Safe-park (B3): zero the flight intent of any LIVE session that has been
    // silent for `_parkAfterTicks`, so a disconnected ship coasts to a controlled
    // state instead of flying away on its last input during the reconnect-grace
    // window. Idempotent (re-zeroing a parked ship is a no-op); a resumed client's
    // next input overrides it immediately.
    void SafeParkSilent(ECS::Registry& _world, uint32_t _tick, uint32_t _parkAfterTicks)
    {
      for (auto& entry : m_sessions)
      {
        Session& s = entry.second;
        if (!s.Live() || _tick - s.lastSeenTick < _parkAfterTicks)
          continue;
        if (FlightIntent* fi = _world.TryGet<FlightIntent>(s.entity))
          *fi = FlightIntent{};   // neutral: no roll/pitch, zero throttle
      }
    }

    // Queue a catalog message onto every session's lanes (e.g. a despawn or death
    // that everyone must see exactly once); routed to the message's lane.
    template <Msg::Message M>
    void Broadcast(const M& _m)
    {
      for (auto& entry : m_sessions)
        entry.second.events.Send(_m);
    }

    // Apply a client's ClientHello name to its session (the authoritative per-
    // player record): sanitize, cap, de-duplicate against the other sessions.
    // Returns true when the stored name actually changed (so the caller broadcasts
    // an updated PlayerInfo). A blank/all-control name, or an unknown endpoint,
    // keeps the assigned default.
    bool ApplyName(ECS::Registry& _world, const Net::Endpoint& _ep, const std::string& _raw)
    {
      const uint64_t key = EndpointKey(_ep);
      auto it = m_sessions.find(key);
      if (it == m_sessions.end())
        return false;
      return ApplyNameTo(_world, it->second, key, _raw);
    }

    // Route a kill's score delta to the player whose PRIMARY entity is
    // `_entityIndex` (C2: score is a per-player record on the session, fed by
    // KillRewards::CreditKill through the server's kill handler). Returns false
    // when no live session owns that entity (an NPC or departed killer).
    bool AddScore(uint32_t _entityIndex, int _delta)
    {
      for (auto& e : m_sessions)
        if (e.second.entity.index == _entityIndex)
        {
          e.second.score += _delta;
          return true;
        }
      return false;
    }

    // The reported round-trip time (ms) of the session whose PRIMARY entity is
    // `_entityIndex`, or 0 when no session owns it (an NPC / missile shooter, which
    // is never lag-compensated). Used by the fire path to size the rewind (E1).
    [[nodiscard]] uint32_t RttForEntity(uint32_t _entityIndex) const
    {
      for (const auto& e : m_sessions)
        if (e.second.entity.index == _entityIndex)
          return e.second.rttMs;
      return 0;
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

    // Register an ADDITIONAL entity as owned by `_playerId` (C): stamps the Owner
    // component and indexes it, so it despawns with the session on reap and counts
    // in "all my units". This is the F-track's grant point (drones, outposts);
    // today only tests exercise a second entity. No-op for playerId 0.
    void GrantOwnership(ECS::Registry& _world, uint32_t _playerId, ECS::EntityId _entity)
    {
      if (_playerId == 0 || !_world.IsValid(_entity))
        return;
      _world.Add<Owner>(_entity, Owner{ _playerId });
      m_ownership.Add(_playerId, _entity);
    }

    // The PlayerId -> owned-entities relation ("all my units" is O(mine), §12).
    [[nodiscard]] const ECS::OwnershipIndex& Ownership() const { return m_ownership; }

  private:
    // Resolve an authenticated (tokened) datagram to its session, re-binding the
    // endpoint if the token arrived from a new address (NAT rebind). Returns null
    // for a zero/unknown/spoofed token. Updates lastSeenTick on success.
    Session* Authenticate(const Net::Endpoint& _ep, uint64_t _token, uint32_t _tick)
    {
      if (_token == 0)
        return nullptr;
      auto ti = m_byToken.find(_token);
      if (ti == m_byToken.end())
        return nullptr;   // unknown token: spoofed or already reaped

      const uint64_t newKey = EndpointKey(_ep);
      if (ti->second != newKey)
      {
        // Authenticated client talking from a new address: migrate the session to
        // the new endpoint key. Skip if another session already holds that key (a
        // fresh peer reusing the exact addr:port - astronomically unlikely; drop
        // rather than clobber the incumbent).
        if (m_sessions.count(newKey) != 0)
          return nullptr;
        auto node = m_sessions.extract(ti->second);
        if (node.empty())
        {
          m_byToken.erase(ti);   // index/desync guard
          return nullptr;
        }
        node.key() = newKey;
        Session& moved = m_sessions.insert(std::move(node)).position->second;
        moved.endpoint = _ep;
        moved.lastSeenTick = _tick;
        ti->second = newKey;
        return &moved;
      }

      auto si = m_sessions.find(newKey);
      if (si == m_sessions.end())
      {
        m_byToken.erase(ti);   // index points at nothing: heal
        return nullptr;
      }
      si->second.lastSeenTick = _tick;
      return &si->second;
    }

    // Charge one token-less datagram against `_ep`'s budget; return true (drop) when
    // the endpoint is muted or has just exceeded the window cap.
    bool RateLimited(const Net::Endpoint& _ep, uint32_t _tick)
    {
      RateInfo& ri = m_rate[EndpointKey(_ep)];
      if (_tick < ri.mutedUntil)
        return true;   // still cooling down
      if (_tick - ri.windowStart >= RATE_WINDOW_TICKS)
      {
        ri.windowStart = _tick;   // new window
        ri.count = 0;
      }
      if (++ri.count > RATE_MAX_UNAUTH)
      {
        ri.mutedUntil = _tick + RATE_MUTE_TICKS;
        return true;
      }
      return false;
    }

    // Mint a fresh, nonzero session token from the injected source (a CSPRNG on the
    // real server), or a deterministic fallback for headless tests.
    uint64_t NextToken()
    {
      if (m_tokenSource)
      {
        const uint64_t t = m_tokenSource();
        if (t != 0)
          return t;
      }
      return DEFAULT_TOKEN_BASE + (++m_tokenCounter);
    }

    // Sanitize/de-dupe `_raw` onto the session's per-player record (the ONE home
    // of a player's name since C2 - no hull mirror). Returns true if the stored
    // name actually changed. Shared by ApplyName (live rename) and OnHello (a
    // rename on an already-connected session). `_world` is kept for signature
    // stability of the public ApplyName (and future record side-effects).
    bool ApplyNameTo(ECS::Registry& _world, Session& _s, uint64_t _key, const std::string& _raw)
    {
      (void)_world;
      std::string clean = SanitizeName(_raw);
      if (clean.empty())
        return false;
      clean = UniqueName(clean, _key);
      if (clean == _s.name)
        return false;

      _s.name = clean;
      return true;
    }

    // Spawn a controllable player entity owned by `_playerId` (stamped with Owner
    // and registered in the ownership index), spread out so clients don't overlap.
    ECS::EntityId SpawnPlayer(ECS::Registry& _world, uint32_t _playerId)
    {
      const int64_t offset = static_cast<int64_t>(m_spawnCount++) * 2000;
      const ECS::EntityId e = _world.Create();
      _world.Add<Owner>(e, Owner{ _playerId });   // the identity layer (C)
      m_ownership.Add(_playerId, e);
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
      _world.Add<CabinHeat>(e, CabinHeat{}); // G4: sun proximity heat (HUD-mirrored)
      // (C2: no PlayerRecord - name/score are the session's per-player record.)
      _world.Add<NetType>(e, NetType{ ShipType::Sidewinder });
      return e;
    }

    // Fill `_out` with a session's roster entry (player + primary entity + name +
    // legal status). Returns false when the session's entity is no longer valid.
    [[nodiscard]] static bool BuildPlayerInfo(ECS::Registry& _world, const Session& _s, Msg::PlayerInfo& _out)
    {
      if (!_world.IsValid(_s.entity))
        return false;
      _out.playerId = _s.playerId;   // the roster's identity key (C): player, not hull
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

    // Per-endpoint token-less datagram budget (see RateLimited).
    struct RateInfo
    {
      uint32_t windowStart = 0;   // tick the current window opened
      uint16_t count = 0;         // token-less datagrams seen this window
      uint32_t mutedUntil = 0;    // muted through this tick (0 = not muted)
    };

    // Deterministic fallback token base (headless tests only; the server injects a
    // CSPRNG). High bits set so a fallback token never looks like an endpoint key.
    static constexpr uint64_t DEFAULT_TOKEN_BASE = 0xA5A5A5A500000000ULL;

    std::unordered_map<uint64_t, Session> m_sessions;   // keyed by CURRENT endpoint key
    std::unordered_map<uint64_t, uint64_t> m_byToken;   // session token -> its endpoint key
    std::unordered_map<uint64_t, RateInfo> m_rate;      // endpoint key -> token-less budget
    ECS::OwnershipIndex m_ownership;                    // PlayerId -> owned entities (C)
    std::function<uint64_t()> m_tokenSource;            // CSPRNG on the server; unset in tests
    std::vector<Net::GalaxySystemInfo> m_manifest;      // galaxy chart data for new clients
    uint32_t m_spawnCount = 0;
    uint32_t m_nextPlayerId = 1;   // player identity allocator (C); 0 = "no player"
    uint64_t m_tokenCounter = 0;   // fallback-token sequence (tests)
  };
}
