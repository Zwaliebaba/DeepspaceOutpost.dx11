#pragma once

// GameServer - the dedicated server's orchestrator (Server).
//
// Owns the authoritative world and every per-process service around it
// (sessions, AOI, spawning, the combat message bus, the RNG streams) and runs
// the fixed tick as a sequence of named phases:
//
//   ReceiveDatagrams -> dispatch fire commands -> roster upkeep ->
//   ProcessReliableRequests -> AdvanceSimulation -> ResolveKills ->
//   LootAndScoop -> DecayWantedRecords -> ReapAndDespawn -> PublishState
//
// All game BEHAVIOUR lives in GameLogic systems; this class only sequences them
// and moves their results on/off the wire. Main.cpp is reduced to process
// startup + the loop; world construction lives in WorldBuilder; tuning knobs in
// ServerConfig.h.

#include <cstddef>
#include <cstdint>
#include <vector>

#include "ECS.h"
#include "NetLib.h"
#include "Messages/MessageBus.h"
#include "Messages/Defs/PlayerSession.h"
#include "Messages/Defs/Travel.h"         // TravelRequest / TravelResponse
#include "DatagramPump.h"     // NeuronServer: bounded drain + magic routing
#include "OnChangeCache.h"    // NeuronServer: send-on-change suppression

#include "GameLogic.h"

namespace DSOServer
{
  class GameServer
  {
  public:
    // Builds the world, provisions the services and registers the combat
    // subscribers. The socket stays owned by main (it is process lifetime).
    explicit GameServer(Neuron::Net::UdpSocket& _socket);

    // One fixed simulation tick (the caller paces it and updates the clock).
    void RunTick();

  private:
    // Catalog messages compare via their Fields() tuple (no operator==).
    struct StatusFieldsEqual
    {
      bool operator()(const Neuron::Msg::PlayerStatus& _a, const Neuron::Msg::PlayerStatus& _b) const
      {
        return _a.Fields() == _b.Fields();
      }
    };

    // --- tick phases (in run order) ---
    void ReceiveDatagrams();
    void BroadcastRosterIfMembershipChanged();
    void ProcessReliableRequests();
    void AdvanceSimulation();
    void ResolveKills();
    void LootAndScoop();
    void DecayWantedRecords();
    void ReapAndDespawn();
    void PublishState();

    // --- handlers & helpers ---
    void RegisterSubscribers();
    void OnInputPacket(const Neuron::Net::Endpoint& _from, const uint8_t* _data, std::size_t _size);
    void OnCrime(const Neuron::GameLogic::Crime& _c);
    void OnEntityKilled(const Neuron::GameLogic::EntityKilled& _k);
    void HandleStationRequest(Neuron::GameLogic::Session& _session, const Neuron::Net::StationRequest& _req);
    void HandleTravelRequest(Neuron::GameLogic::Session& _session, const Neuron::Msg::TravelRequest& _req);
    void SendCargoTo(uint32_t _entityIndex);
    void BroadcastPlayerInfo(uint32_t _entityIndex);

    // --- state ---
    Neuron::Net::UdpSocket& m_socket;
    Neuron::ECS::Registry m_world;
    std::vector<Neuron::ECS::EntityId> m_landmarks;
    Neuron::GameLogic::AreaOfInterest m_aoi;
    Neuron::GameLogic::ServerSessions m_sessions;
    Neuron::GameLogic::DespawnTracker m_despawns;
    Neuron::GameLogic::SpawnDirector m_spawner;
    Neuron::Msg::MessageBus m_bus;

    std::vector<Neuron::Server::MagicRoute> m_routes;   // datagram magic -> handler
    uint8_t m_recv[2048] = {};

    uint32_t m_tick = 0;
    std::size_t m_lastSessions = 0;

    // Deterministic RNG streams (seeds in ServerConfig.h).
    uint32_t m_lootRng;
    uint32_t m_aiRng;
    uint32_t m_hyperRng;

    // Last PlayerStatus sent per session (by endpoint key): resend on change only.
    Neuron::Server::OnChangeCache<uint64_t, Neuron::Msg::PlayerStatus, StatusFieldsEqual> m_lastStatus;

    // Rate-limited respawn logging.
    uint32_t m_lastRespawnLogTick = 0;
    int m_suppressedRespawns = 0;
  };
}
