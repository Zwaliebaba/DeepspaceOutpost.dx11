#pragma once

// ServerSessions - the server's per-client connection table (GameLogic).
//
// Replaces the single hardcoded client with a real multi-client model: each
// connected client is a Session keyed by its UDP endpoint, owning the entity it
// controls, its own reliable event channel, and its input/liveness bookkeeping.
// A client "connects" implicitly by sending its first input - the server spawns
// it an entity and hands back the id via AssignPlayer. Idle clients are reaped.
//
// Pure apart from the world it mutates (spawns/destroys entities) - no sockets -
// so connection handling, input application and reaping are all unit-tested
// headlessly; the server loop wires the socket I/O around it.

#include <cstdint>
#include <unordered_map>
#include <vector>

#include "ECS.h"
#include "NetLib.h"            // Net::Endpoint (winsock-free)
#include "ClientInput.h"
#include "ReliableChannel.h"
#include "GameEvents.h"

#include "SimComponents.h"
#include "StationServices.h"
#include "CombatSystem.h"

namespace Neuron::GameLogic
{
  // Stable key for a client endpoint (address in the high bits, port in the low).
  [[nodiscard]] inline uint64_t EndpointKey(const Net::Endpoint& _ep)
  {
    return (static_cast<uint64_t>(_ep.address) << 16) | static_cast<uint64_t>(_ep.port);
  }

  struct Session
  {
    Net::Endpoint endpoint;
    ECS::EntityId entity;
    Net::ReliableChannel events;       // reliable stream to THIS client
    uint32_t lastInputSeq = 0;         // newest input applied (drops stale)
    uint32_t lastSeenTick = 0;         // for idle reaping
  };

  class ServerSessions
  {
  public:
    // Handle an input datagram from `_ep`. On first contact the client is
    // connected: a player entity is spawned and AssignPlayer queued on its
    // reliable channel. The latest intent is applied to the entity. Returns the
    // session's entity.
    ECS::EntityId OnInput(ECS::Registry& _world, const Net::Endpoint& _ep,
                          const Net::ClientInput& _in, uint32_t _tick)
    {
      const uint64_t key = EndpointKey(_ep);
      auto it = m_sessions.find(key);
      if (it == m_sessions.end())
      {
        Session s;
        s.endpoint = _ep;
        s.entity = SpawnPlayer(_world);
        Net::SendAssignPlayer(s.events, s.entity.index);
        it = m_sessions.emplace(key, std::move(s)).first;
      }

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

    // Apply a reliable-channel packet (a client ack) from `_ep`.
    void OnReliable(const Net::Endpoint& _ep, const uint8_t* _data, std::size_t _size)
    {
      auto it = m_sessions.find(EndpointKey(_ep));
      if (it != m_sessions.end())
        it->second.events.ReadPacket(_data, _size);
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

    // Queue a reliable event onto every session's channel (e.g. a despawn that
    // everyone must see exactly once).
    void Broadcast(uint16_t _type, const std::vector<uint8_t>& _payload)
    {
      for (auto& entry : m_sessions)
        entry.second.events.Send(_type, _payload);
    }

    [[nodiscard]] std::unordered_map<uint64_t, Session>& All() { return m_sessions; }
    [[nodiscard]] std::size_t Count() const { return m_sessions.size(); }
    [[nodiscard]] bool Has(const Net::Endpoint& _ep) const
    {
      return m_sessions.find(EndpointKey(_ep)) != m_sessions.end();
    }

  private:
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
      // Combat/faction state: a player is on the Player team, fires only on
      // command (autoEngage = false), and starts with a clean record.
      _world.Add<PlayerTag>(e, PlayerTag{});
      _world.Add<Combatant>(e, Combatant{ Team::Player, /*energy*/ 255, /*laser*/ 10, /*range*/ 6000, /*autoEngage*/ false });
      _world.Add<Wanted>(e, Wanted{});
      return e;
    }

    std::unordered_map<uint64_t, Session> m_sessions;
    uint32_t m_spawnCount = 0;
  };
}
