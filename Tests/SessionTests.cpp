#include "TestFramework.h"

#include <vector>

#include "GameLogic.h"
#include "ReliableChannel.h"

using namespace Neuron;

namespace
{
  Net::ClientInput Input(uint32_t _seq, float _throttle)
  {
    Net::ClientInput in;
    in.sequence = _seq;
    in.throttle = _throttle;
    return in;
  }
}

TEST(Session_FirstInputConnectsAndSpawns)
{
  ECS::Registry world;
  GameLogic::ServerSessions sessions;

  const Net::Endpoint a{ 0x7F000001, 1001 };
  ECS::EntityId e = sessions.OnInput(world, a, Input(1, 0.5f), /*tick*/ 1);

  CHECK(sessions.Count() == 1);
  CHECK(world.IsValid(e));
  CHECK(world.Get<GameLogic::FlightIntent>(e).throttle == 0.5f);

  // The new session has an AssignPlayer handshake queued on its channel.
  GameLogic::Session& s = sessions.All().at(GameLogic::EndpointKey(a));
  CHECK(s.events.PendingOutgoing() == 1);
}

TEST(Session_DistinctEndpointsGetDistinctEntities)
{
  ECS::Registry world;
  GameLogic::ServerSessions sessions;

  ECS::EntityId e1 = sessions.OnInput(world, Net::Endpoint{ 0x7F000001, 1001 }, Input(1, 0.0f), 1);
  ECS::EntityId e2 = sessions.OnInput(world, Net::Endpoint{ 0x7F000001, 1002 }, Input(1, 0.0f), 1);

  CHECK(sessions.Count() == 2);
  CHECK(e1 != e2);
}

TEST(Session_SameEndpointReusesSessionAndAppliesLatestInput)
{
  ECS::Registry world;
  GameLogic::ServerSessions sessions;

  const Net::Endpoint a{ 0x7F000001, 1001 };
  ECS::EntityId e = sessions.OnInput(world, a, Input(1, 0.2f), 1);

  sessions.OnInput(world, a, Input(5, 0.9f), 2);    // newer -> applied
  CHECK(sessions.Count() == 1);
  CHECK(world.Get<GameLogic::FlightIntent>(e).throttle == 0.9f);

  sessions.OnInput(world, a, Input(3, 0.1f), 3);    // stale seq -> ignored
  CHECK(world.Get<GameLogic::FlightIntent>(e).throttle == 0.9f);
}

TEST(Session_IdleSessionsAreReapedAndEntitiesDestroyed)
{
  ECS::Registry world;
  GameLogic::ServerSessions sessions;

  ECS::EntityId e1 = sessions.OnInput(world, Net::Endpoint{ 0x7F000001, 1001 }, Input(1, 0.0f), 1);
  ECS::EntityId e2 = sessions.OnInput(world, Net::Endpoint{ 0x7F000001, 1002 }, Input(1, 0.0f), 1);

  std::vector<uint32_t> gone = sessions.Reap(world, /*tick*/ 100, /*timeout*/ 5);

  CHECK(sessions.Count() == 0);
  CHECK(gone.size() == 2);
  CHECK(!world.IsValid(e1));
  CHECK(!world.IsValid(e2));
}

TEST(Session_RecentSessionsSurviveReaping)
{
  ECS::Registry world;
  GameLogic::ServerSessions sessions;

  const Net::Endpoint a{ 0x7F000001, 1001 };
  sessions.OnInput(world, a, Input(1, 0.0f), /*tick*/ 98);
  // tick 100, timeout 5: 100 - 98 = 2 <= 5, so it stays.
  sessions.Reap(world, 100, 5);
  CHECK(sessions.Count() == 1);
}

TEST(Session_ClientAckClearsTheReliableQueue)
{
  ECS::Registry world;
  GameLogic::ServerSessions sessions;

  const Net::Endpoint a{ 0x7F000001, 2000 };
  sessions.OnInput(world, a, Input(1, 0.0f), 1);

  GameLogic::Session& s = sessions.All().at(GameLogic::EndpointKey(a));
  CHECK(s.events.PendingOutgoing() == 1);   // AssignPlayer pending

  // Client receives the handshake and acks it back.
  Net::ReliableChannel client;
  std::vector<uint8_t> toClient = s.events.WritePacket();
  CHECK(client.ReadPacket(toClient.data(), toClient.size()));
  std::vector<uint8_t> ack = client.WritePacket();
  sessions.OnReliable(a, ack.data(), ack.size());

  CHECK(s.events.PendingOutgoing() == 0);   // handshake acknowledged
}
