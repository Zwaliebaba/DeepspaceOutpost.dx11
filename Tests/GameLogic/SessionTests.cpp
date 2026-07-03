#include <gtest/gtest.h>

#include <vector>

#include "GameLogic.h"
#include "ReliableChannel.h"
#include "Messages/MessageEndpoint.h"

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

TEST(Session, FirstInputConnectsAndSpawns)
{
  ECS::Registry world;
  GameLogic::ServerSessions sessions;

  const Net::Endpoint a{ 0x7F000001, 1001 };
  ECS::EntityId e = sessions.OnInput(world, a, Input(1, 0.5f), /*tick*/ 1);

  EXPECT_TRUE(sessions.Count() == 1);
  EXPECT_TRUE(world.IsValid(e));
  EXPECT_TRUE(world.Get<GameLogic::FlightIntent>(e).throttle == 0.5f);

  // The new session has an AssignPlayer handshake queued on its channel.
  GameLogic::Session& s = sessions.All().at(GameLogic::EndpointKey(a));
  EXPECT_TRUE(s.events.PendingOutgoing() == 1);
}

TEST(Session, DistinctEndpointsGetDistinctEntities)
{
  ECS::Registry world;
  GameLogic::ServerSessions sessions;

  ECS::EntityId e1 = sessions.OnInput(world, Net::Endpoint{ 0x7F000001, 1001 }, Input(1, 0.0f), 1);
  ECS::EntityId e2 = sessions.OnInput(world, Net::Endpoint{ 0x7F000001, 1002 }, Input(1, 0.0f), 1);

  EXPECT_TRUE(sessions.Count() == 2);
  EXPECT_TRUE(e1 != e2);
}

TEST(Session, SameEndpointReusesSessionAndAppliesLatestInput)
{
  ECS::Registry world;
  GameLogic::ServerSessions sessions;

  const Net::Endpoint a{ 0x7F000001, 1001 };
  ECS::EntityId e = sessions.OnInput(world, a, Input(1, 0.2f), 1);

  sessions.OnInput(world, a, Input(5, 0.9f), 2);    // newer -> applied
  EXPECT_TRUE(sessions.Count() == 1);
  EXPECT_TRUE(world.Get<GameLogic::FlightIntent>(e).throttle == 0.9f);

  sessions.OnInput(world, a, Input(3, 0.1f), 3);    // stale seq -> ignored
  EXPECT_TRUE(world.Get<GameLogic::FlightIntent>(e).throttle == 0.9f);
}

TEST(Session, IdleSessionsAreReapedAndEntitiesDestroyed)
{
  ECS::Registry world;
  GameLogic::ServerSessions sessions;

  ECS::EntityId e1 = sessions.OnInput(world, Net::Endpoint{ 0x7F000001, 1001 }, Input(1, 0.0f), 1);
  ECS::EntityId e2 = sessions.OnInput(world, Net::Endpoint{ 0x7F000001, 1002 }, Input(1, 0.0f), 1);

  std::vector<uint32_t> gone = sessions.Reap(world, /*tick*/ 100, /*timeout*/ 5);

  EXPECT_TRUE(sessions.Count() == 0);
  EXPECT_TRUE(gone.size() == 2);
  EXPECT_TRUE(!world.IsValid(e1));
  EXPECT_TRUE(!world.IsValid(e2));
}

TEST(Session, RecentSessionsSurviveReaping)
{
  ECS::Registry world;
  GameLogic::ServerSessions sessions;

  const Net::Endpoint a{ 0x7F000001, 1001 };
  sessions.OnInput(world, a, Input(1, 0.0f), /*tick*/ 98);
  // tick 100, timeout 5: 100 - 98 = 2 <= 5, so it stays.
  sessions.Reap(world, 100, 5);
  EXPECT_TRUE(sessions.Count() == 1);
}

TEST(Session, NewSessionGetsADefaultNameAndPlayerRecord)
{
  ECS::Registry world;
  GameLogic::ServerSessions sessions;

  const Net::Endpoint a{ 0x7F000001, 3000 };
  ECS::EntityId e = sessions.OnInput(world, a, Input(1, 0.0f), 1);

  GameLogic::Session& s = sessions.All().at(GameLogic::EndpointKey(a));
  EXPECT_FALSE(s.name.empty());                                   // a placeholder was assigned
  ASSERT_TRUE(world.Has<GameLogic::PlayerRecord>(e));
  EXPECT_EQ(world.Get<GameLogic::PlayerRecord>(e).name, s.name);  // mirrored onto the record
}

TEST(Session, ApplyNameSanitizesAndMirrorsToTheRecord)
{
  ECS::Registry world;
  GameLogic::ServerSessions sessions;

  const Net::Endpoint a{ 0x7F000001, 3001 };
  ECS::EntityId e = sessions.OnInput(world, a, Input(1, 0.0f), 1);

  // Control chars are dropped; the printable remainder is kept.
  EXPECT_TRUE(sessions.ApplyName(world, a, std::string("Ja\x01me\x7Fson")));
  GameLogic::Session& s = sessions.All().at(GameLogic::EndpointKey(a));
  EXPECT_EQ(s.name, "Jameson");
  EXPECT_EQ(world.Get<GameLogic::PlayerRecord>(e).name, "Jameson");
}

TEST(Session, ApplyNameKeepsDefaultForABlankName)
{
  ECS::Registry world;
  GameLogic::ServerSessions sessions;

  const Net::Endpoint a{ 0x7F000001, 3002 };
  sessions.OnInput(world, a, Input(1, 0.0f), 1);
  GameLogic::Session& s = sessions.All().at(GameLogic::EndpointKey(a));
  const std::string def = s.name;

  EXPECT_FALSE(sessions.ApplyName(world, a, "\x01\x02   "));   // all control/space -> nothing usable
  EXPECT_EQ(s.name, def);                                      // default retained
}

TEST(Session, ApplyNameDeDuplicatesAcrossSessions)
{
  ECS::Registry world;
  GameLogic::ServerSessions sessions;

  const Net::Endpoint a{ 0x7F000001, 3003 };
  const Net::Endpoint b{ 0x7F000001, 3004 };
  sessions.OnInput(world, a, Input(1, 0.0f), 1);
  sessions.OnInput(world, b, Input(1, 0.0f), 1);

  EXPECT_TRUE(sessions.ApplyName(world, a, "Raxxla"));
  EXPECT_TRUE(sessions.ApplyName(world, b, "Raxxla"));   // same name -> disambiguated

  EXPECT_EQ(sessions.All().at(GameLogic::EndpointKey(a)).name, "Raxxla");
  EXPECT_EQ(sessions.All().at(GameLogic::EndpointKey(b)).name, "Raxxla-2");
}

TEST(Session, RosterAndPlayerInfoReflectNameAndWanted)
{
  ECS::Registry world;
  GameLogic::ServerSessions sessions;

  const Net::Endpoint a{ 0x7F000001, 3005 };
  ECS::EntityId e = sessions.OnInput(world, a, Input(1, 0.0f), 1);
  sessions.ApplyName(world, a, "Elite");
  world.Get<GameLogic::Wanted>(e).level = 4;

  std::vector<Neuron::Msg::PlayerInfo> roster = sessions.Roster(world);
  ASSERT_EQ(roster.size(), 1u);
  EXPECT_EQ(roster[0].entityId, e.index);
  EXPECT_EQ(roster[0].name, "Elite");
  EXPECT_EQ(roster[0].wantedLevel, 4);

  Neuron::Msg::PlayerInfo one;
  ASSERT_TRUE(sessions.PlayerInfoFor(world, e.index, one));
  EXPECT_EQ(one.name, "Elite");
  EXPECT_EQ(one.wantedLevel, 4);

  Neuron::Msg::PlayerInfo missing;
  EXPECT_FALSE(sessions.PlayerInfoFor(world, 99999u, missing));   // no such session
}

TEST(Session, ClientAckClearsTheReliableQueue)
{
  ECS::Registry world;
  GameLogic::ServerSessions sessions;

  const Net::Endpoint a{ 0x7F000001, 2000 };
  sessions.OnInput(world, a, Input(1, 0.0f), 1);

  GameLogic::Session& s = sessions.All().at(GameLogic::EndpointKey(a));
  EXPECT_TRUE(s.events.PendingOutgoing() == 1);   // AssignPlayer pending (Control lane)

  // Client receives the handshake datagram(s) and acks them back.
  Msg::MessageEndpoint client;
  for (const std::vector<uint8_t>& dg : s.events.WriteDatagrams())
    client.OnDatagram(dg.data(), dg.size());
  for (const std::vector<uint8_t>& dg : client.WriteDatagrams())
    sessions.OnReliable(a, dg.data(), dg.size());

  EXPECT_TRUE(s.events.PendingOutgoing() == 0);   // handshake acknowledged
}
