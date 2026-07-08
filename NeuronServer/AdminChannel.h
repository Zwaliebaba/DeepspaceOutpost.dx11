#pragma once

// AdminChannel - the ServerManager management-channel service (NeuronServer).
//
// The server-side of the second (management) UDP port: it authenticates managers,
// keeps a bounded ring of recent events, and pushes roster / event / health state
// to every connected ServerManager over the same reliable lanes the game uses
// (Msg::MessageEndpoint). It is deliberately SOCKET-FREE - bytes in via OnDatagram,
// bytes out via WriteDatagrams - so the whole protocol (handshake, reject, mute,
// replay, deltas, reaping) is unit-tested headlessly, exactly like ServerSessions
// and the reliable lanes. The Server host owns the socket and pumps it through here.
//
// Identity mirrors the game's B2 model: a manager's opening AdminHello carries the
// shared admin secret and a zero session-token; on a good key the channel mints a
// CSPRNG admin token (injected, like ServerSessions::SetTokenSource) that the
// manager stamps on every later datagram, so the server authenticates by token, not
// address, and a NAT rebind heals silently. Pre-token datagrams are rate-limited per
// endpoint and a bad key arms a per-endpoint failure mute, so the management port
// cannot be brute-forced or flooded into provisioning unbounded shells.
//
// v1 is READ-ONLY: the only manager -> server message is the hello; everything else
// is server -> manager. Command messages (kick/broadcast/save/settings) are a later
// extension that reuses this same channel and token discipline (see sm.md §8).

#include <cstddef>
#include <cstdint>
#include <deque>
#include <functional>
#include <optional>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include "NetLib.h"                        // Net::Endpoint
#include "Messages/Framing.h"              // Msg::PROTOCOL_VERSION
#include "Messages/MessageEndpoint.h"      // Msg::MessageEndpoint, PeekReliableToken
#include "Messages/Reliable.h"             // Msg::TryDecode
#include "Messages/Serialize.h"            // Msg::Encode
#include "Messages/Defs/Admin.h"           // the management-channel schema

namespace Neuron::Server
{
  // Tuning for the management channel. These are DEPLOYMENT knobs (like ServerConfig's
  // game-side ones); the Server host may override them, but the defaults are sane.
  inline constexpr std::size_t ADMIN_EVENT_RING     = 256;   // recent events replayed on connect
  inline constexpr std::size_t MAX_ADMIN_SESSIONS   = 4;     // concurrent managers cap
  inline constexpr uint32_t ADMIN_TIMEOUT_TICKS     = 900;   // reap a LIVE admin after ~30 s silence (30 Hz)
  inline constexpr uint32_t ADMIN_PENDING_TICKS     = 300;   // reap a pre-hello shell after ~10 s
  inline constexpr uint32_t ADMIN_RATE_WINDOW_TICKS = 30;    // ~1 s pre-auth datagram window
  inline constexpr uint16_t ADMIN_RATE_MAX_UNAUTH   = 60;    // token-less datagrams / window / endpoint
  inline constexpr uint32_t ADMIN_RATE_MUTE_TICKS   = 1800;  // ~60 s mute once the cap is exceeded
  inline constexpr uint16_t ADMIN_MAX_KEY_FAILURES  = 5;     // bad-key attempts before a failure mute

  class AdminChannel
  {
  public:
    // `_key` is the shared admin secret (empty ⇒ the channel is DISABLED and every
    // datagram is ignored - the Server host simply never opens the socket then).
    // `_tokenSource` mints the CSPRNG admin token (the host injects SecureRandom64);
    // unset falls back to a deterministic nonzero counter for headless tests.
    explicit AdminChannel(std::string _key = std::string(),
                          std::function<uint64_t()> _tokenSource = {})
      : m_key(std::move(_key)), m_tokenSource(std::move(_tokenSource)) {}

    [[nodiscard]] bool Enabled() const { return !m_key.empty(); }

    // The static server-identity block echoed in every AdminHelloAck. Set once by the
    // host at startup (the per-accept token/serverTick are filled in on acceptance).
    void SetIdentity(uint32_t _gameLogicVersion, uint32_t _tickRateMs, uint16_t _gamePort)
    {
      m_identity.gameLogicVersion = _gameLogicVersion;
      m_identity.tickRateMs = _tickRateMs;
      m_identity.gamePort = _gamePort;
    }

    // --- feed points (host calls these at its existing hook sites; plain data) ----

    // Append one event to the ring (ALWAYS, even with no managers connected - that is
    // what makes connect-anytime history work) and push it live to every connected
    // manager on the Gameplay lane.
    void PushEvent(uint32_t _tick, Msg::AdminEventKind _kind, uint32_t _subject, std::string _text)
    {
      Msg::AdminEvent ev;
      ev.sequence = ++m_eventSeq;
      ev.tick = _tick;
      ev.kind = static_cast<uint8_t>(_kind);
      ev.subject = _subject;
      ev.text = std::move(_text);

      if (m_ring.size() >= ADMIN_EVENT_RING)
        m_ring.pop_front();
      m_ring.push_back(ev);

      for (auto& kv : m_sessions)
        if (kv.second.Live())
          kv.second.events.Send(ev);   // Gameplay lane (AdminEvent::Lane)
    }

    // Record/refresh one roster row (change-gated by the caller) and push it live.
    // The stored copy is replayed to a manager that connects later.
    void PushRoster(const Msg::AdminPlayerInfo& _row)
    {
      m_roster[_row.playerId] = _row;
      for (auto& kv : m_sessions)
        if (kv.second.Live())
          kv.second.events.Send(_row);
    }

    // Drop a roster row and tell every manager the player left.
    void PushPlayerGone(uint32_t _playerId, Msg::AdminGoneReason _reason)
    {
      m_roster.erase(_playerId);
      Msg::AdminPlayerGone gone;
      gone.playerId = _playerId;
      gone.reason = static_cast<uint8_t>(_reason);
      for (auto& kv : m_sessions)
        if (kv.second.Live())
          kv.second.events.Send(gone);
    }

    // Push a rolling health sample to every manager, and remember the latest so a
    // freshly-connected manager's health panel isn't blank until the next cadence.
    void PushHealth(const Msg::AdminHealth& _h)
    {
      m_lastHealth = _h;
      for (auto& kv : m_sessions)
        if (kv.second.Live())
          kv.second.events.Send(_h);
    }

    // --- socket edge (the host wires the admin UdpSocket to these) ----------------

    // Route one inbound 'NRLB' reliable datagram from the admin socket. `_tick` is the
    // current world tick (liveness + the accept serverTick baseline). A token-0
    // datagram is the hello front door (rate-limited, provisions a pending shell); a
    // tokened datagram is authenticated and routed to its session (re-binding the
    // endpoint on a NAT rebind). No-op when the channel is disabled.
    void OnDatagram(const Net::Endpoint& _ep, const uint8_t* _data, std::size_t _size, uint32_t _tick)
    {
      if (!Enabled())
        return;

      const uint64_t token = Msg::PeekReliableToken(_data, _size);
      if (token == 0)
      {
        if (RateLimited(_ep, _tick))
          return;
        auto it = m_sessions.find(Key(_ep));
        if (it != m_sessions.end() && it->second.Live())
          return;   // a live manager must present its token; ignore token-less traffic
        AdminSession& s = m_sessions.try_emplace(Key(_ep)).first->second;
        s.endpoint = _ep;
        s.lastSeenTick = _tick;
        if (!s.events.OnDatagram(_data, _size))
          return;   // foreign/malformed inner packet
        ProcessPending(s, Key(_ep), _tick);
        return;
      }

      AdminSession* s = Authenticate(_ep, token, _tick);
      if (s == nullptr)
        return;   // unknown/spoofed token: drop before any decode
      s->events.OnDatagram(_data, _size);
      // v1 has no post-auth manager -> server message; drain and discard so the
      // reliable lanes advance their delivery cursor (their acks are still emitted).
      Net::ReliableMessage msg;
      while (s->events.Receive(msg)) { /* reserved for the future command track */ }
    }

    // Collect every pending outgoing datagram (endpoint + bytes) and reap managers
    // that have gone silent. The host sends each pair on the admin socket. Server
    // datagrams carry token 0 (the manager trusts the server by address).
    void WriteDatagrams(uint32_t _tick, std::vector<std::pair<Net::Endpoint, std::vector<uint8_t>>>& _out)
    {
      // Reap first, so a just-timed-out session doesn't emit one last datagram.
      for (auto it = m_sessions.begin(); it != m_sessions.end();)
      {
        const uint32_t timeout = it->second.Live() ? ADMIN_TIMEOUT_TICKS : ADMIN_PENDING_TICKS;
        if (_tick - it->second.lastSeenTick > timeout)
        {
          if (it->second.token != 0)
            m_byToken.erase(it->second.token);
          it = m_sessions.erase(it);
        }
        else
        {
          ++it;
        }
      }

      for (auto& [key, s] : m_sessions)
        for (std::vector<uint8_t>& dg : s.events.WriteDatagrams())
          _out.emplace_back(s.endpoint, std::move(dg));

      // Drop stale rate-limit records (no longer muted and long idle) so the map can't
      // grow without bound over a long uptime.
      for (auto it = m_rate.begin(); it != m_rate.end();)
      {
        if (_tick >= it->second.mutedUntil && _tick - it->second.windowStart > ADMIN_PENDING_TICKS)
          it = m_rate.erase(it);
        else
          ++it;
      }
    }

    // --- observers (for tests / the host's status banner) -------------------------
    [[nodiscard]] std::size_t SessionCount() const   // LIVE (authenticated) managers
    {
      std::size_t n = 0;
      for (const auto& kv : m_sessions)
        if (kv.second.Live())
          ++n;
      return n;
    }
    [[nodiscard]] std::size_t RosterSize() const { return m_roster.size(); }
    [[nodiscard]] std::size_t EventCount() const { return m_ring.size(); }

  private:
    struct AdminSession
    {
      Net::Endpoint endpoint;
      uint64_t token = 0;                 // 0 while a pending (pre-hello) shell
      Msg::MessageEndpoint events;        // reliable lanes to this manager
      uint32_t lastSeenTick = 0;
      [[nodiscard]] bool Live() const { return token != 0; }
    };

    struct RateInfo
    {
      uint32_t windowStart = 0;   // tick the current pre-auth window opened
      uint16_t count = 0;         // token-less datagrams charged this window
      uint16_t keyFailures = 0;   // bad-key attempts (armed the mute at the cap)
      uint32_t mutedUntil = 0;    // muted through this tick (0 = not muted)
    };

    [[nodiscard]] static uint64_t Key(const Net::Endpoint& _ep)
    {
      return (static_cast<uint64_t>(_ep.address) << 16) | static_cast<uint64_t>(_ep.port);
    }

    // Drain a pending shell's reliable lanes looking for the AdminHello front door.
    void ProcessPending(AdminSession& _s, uint64_t _key, uint32_t _tick)
    {
      Net::ReliableMessage msg;
      while (_s.events.Receive(msg))
      {
        Msg::AdminHello hello;
        if (Msg::TryDecode(msg, hello))
          OnHello(_s, _key, hello, _tick);
        // else: an unexpected pre-auth message - ignore it (only the hello opens the door)
      }
    }

    // Validate a decoded AdminHello and, on success, accept the manager: mint a token,
    // queue AdminHelloAck, and replay the event ring + current roster + last health.
    void OnHello(AdminSession& _s, uint64_t _key, const Msg::AdminHello& _hello, uint32_t _tick)
    {
      _s.lastSeenTick = _tick;

      if (_hello.protocolVersion != Msg::PROTOCOL_VERSION)
      {
        // An honest version mismatch, not an attack: reject but do NOT arm the mute.
        _s.events.Send(Msg::AdminHelloReject{ static_cast<uint8_t>(Msg::AdminRejectReason::ProtocolMismatch) });
        return;
      }

      if (!KeyMatches(_hello.adminKey))
      {
        _s.events.Send(Msg::AdminHelloReject{ static_cast<uint8_t>(Msg::AdminRejectReason::BadKey) });
        NoteKeyFailure(_key, _tick);
        return;
      }

      if (_s.Live())
      {
        // A second hello on a LIVE session is a reconnect: keep the token, re-ack and
        // re-replay so the manager rebuilds its mirror from scratch.
        AckAndReplay(_s, _tick);
        return;
      }

      if (SessionCount() >= MAX_ADMIN_SESSIONS)
      {
        _s.events.Send(Msg::AdminHelloReject{ static_cast<uint8_t>(Msg::AdminRejectReason::ServerBusy) });
        return;
      }

      _s.token = NextToken();
      m_byToken[_s.token] = _key;
      AckAndReplay(_s, _tick);
    }

    // Queue the accept reply plus the connect-time replay: the ack on the Control
    // lane, then the ring, roster and last health FORCED onto the Bulk lane so a
    // large backlog can't head-of-line-block live Gameplay traffic.
    void AckAndReplay(AdminSession& _s, uint32_t _tick)
    {
      Msg::AdminHelloAck ack = m_identity;
      ack.adminToken = _s.token;
      ack.protocolVersion = static_cast<uint16_t>(Msg::PROTOCOL_VERSION);
      ack.serverTick = _tick;
      _s.events.Send(ack);   // Control lane (AdminHelloAck::Lane)

      for (const Msg::AdminEvent& ev : m_ring)
        _s.events.SendRaw(Msg::MessageLane::Bulk, Msg::Raw(Msg::AdminEvent::Id), Msg::Encode(ev));
      for (const auto& kv : m_roster)
        _s.events.SendRaw(Msg::MessageLane::Bulk, Msg::Raw(Msg::AdminPlayerInfo::Id), Msg::Encode(kv.second));
      if (m_lastHealth.has_value())
        _s.events.SendRaw(Msg::MessageLane::Bulk, Msg::Raw(Msg::AdminHealth::Id), Msg::Encode(*m_lastHealth));
    }

    // Authenticated route: token -> session, migrating the endpoint on a NAT rebind.
    AdminSession* Authenticate(const Net::Endpoint& _ep, uint64_t _token, uint32_t _tick)
    {
      auto ti = m_byToken.find(_token);
      if (ti == m_byToken.end())
        return nullptr;

      const uint64_t newKey = Key(_ep);
      if (ti->second != newKey)
      {
        if (m_sessions.count(newKey) != 0)
          return nullptr;   // that addr:port already hosts another session - don't clobber
        auto node = m_sessions.extract(ti->second);
        if (node.empty())
        {
          m_byToken.erase(ti);
          return nullptr;
        }
        node.key() = newKey;
        AdminSession& moved = m_sessions.insert(std::move(node)).position->second;
        moved.endpoint = _ep;
        moved.lastSeenTick = _tick;
        ti->second = newKey;
        return &moved;
      }

      auto si = m_sessions.find(newKey);
      if (si == m_sessions.end())
      {
        m_byToken.erase(ti);
        return nullptr;
      }
      si->second.lastSeenTick = _tick;
      return &si->second;
    }

    // Charge one token-less datagram against `_ep`'s budget; true (drop) when muted or
    // just over the window cap.
    bool RateLimited(const Net::Endpoint& _ep, uint32_t _tick)
    {
      RateInfo& ri = m_rate[Key(_ep)];
      if (_tick < ri.mutedUntil)
        return true;
      if (_tick - ri.windowStart >= ADMIN_RATE_WINDOW_TICKS)
      {
        ri.windowStart = _tick;
        ri.count = 0;
      }
      if (++ri.count > ADMIN_RATE_MAX_UNAUTH)
      {
        ri.mutedUntil = _tick + ADMIN_RATE_MUTE_TICKS;
        return true;
      }
      return false;
    }

    // Count a bad-key attempt for `_key`; arm the failure mute at the cap.
    void NoteKeyFailure(uint64_t _key, uint32_t _tick)
    {
      RateInfo& ri = m_rate[_key];
      if (++ri.keyFailures >= ADMIN_MAX_KEY_FAILURES)
      {
        ri.mutedUntil = _tick + ADMIN_RATE_MUTE_TICKS;
        ri.keyFailures = 0;
      }
    }

    // Constant-time comparison of the presented key against the secret (no early-out
    // on the first differing byte, so a timing side-channel can't reveal the key).
    [[nodiscard]] bool KeyMatches(const std::string& _presented) const
    {
      if (_presented.size() != m_key.size())
        return false;
      uint8_t diff = 0;
      for (std::size_t i = 0; i < m_key.size(); ++i)
        diff |= static_cast<uint8_t>(m_key[i]) ^ static_cast<uint8_t>(_presented[i]);
      return diff == 0;
    }

    uint64_t NextToken()
    {
      if (m_tokenSource)
      {
        const uint64_t t = m_tokenSource();
        if (t != 0)
          return t;
      }
      return 0xAD3100000000ull + (++m_tokenCounter);   // deterministic nonzero fallback (tests)
    }

    std::string m_key;                            // the shared admin secret (empty = disabled)
    std::function<uint64_t()> m_tokenSource;      // CSPRNG on the real server; unset in tests
    uint64_t m_tokenCounter = 0;

    Msg::AdminHelloAck m_identity;                // static identity block (SetIdentity fills 3 fields)

    std::unordered_map<uint64_t, AdminSession> m_sessions;   // endpoint key -> session
    std::unordered_map<uint64_t, uint64_t> m_byToken;        // token -> endpoint key
    std::unordered_map<uint64_t, RateInfo> m_rate;           // endpoint key -> pre-auth budget

    std::deque<Msg::AdminEvent> m_ring;           // recent events (<= ADMIN_EVENT_RING), replayed
    uint32_t m_eventSeq = 0;                      // monotonic event sequence source
    std::unordered_map<uint32_t, Msg::AdminPlayerInfo> m_roster;   // playerId -> latest row
    std::optional<Msg::AdminHealth> m_lastHealth; // latest sample, replayed on connect
  };
}
