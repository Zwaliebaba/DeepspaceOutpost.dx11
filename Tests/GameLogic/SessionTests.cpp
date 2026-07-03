#include <gtest/gtest.h>

#include <vector>

#include "GameLogic.h"
#include "ReliableChannel.h"
#include "Messages/MessageEndpoint.h"

using namespace Neuron;

namespace
{
  Msg::InputCommand Input(uint32_t _seq, float _throttle)
  {
    Msg::InputCommand in;
    in.sequence = _seq;
    in.throttle = _throttle;
    return in;
  }

  // A well-formed ClientHello on the current protocol (the front door since B1).
  Msg::ClientHello Hello(const std::string& _name = "")
  {
    Msg::ClientHello h;
    h.protocolVersion = Msg::PROTOCOL_VERSION;
    h.commanderName = _name;
    return h;
  }

  // Connect an endpoint the way the server does: a valid ClientHello spawns the
  // session and its controlled entity. Returns that entity.
  ECS::EntityId Connect(ECS::Registry& _world, GameLogic::ServerSessions& _sessions,
                        const Net::Endpoint& _ep, const std::string& _name = "", uint32_t _tick = 1)
  {
    return _sessions.OnHello(_world, _ep, Hello(_name), _tick).entity;
  }
}

TEST(Session, HelloConnectsAndSpawns)
{
  ECS::Registry world;
  GameLogic::ServerSessions sessions;

  const Net::Endpoint a{ 0x7F000001, 1001 };
  GameLogic::HelloOutcome out = sessions.OnHello(world, a, Hello(), /*tick*/ 1);

  EXPECT_TRUE(out.result == GameLogic::HelloResult::Accepted);
  EXPECT_TRUE(sessions.Count() == 1);
  EXPECT_TRUE(world.IsValid(out.entity));

  // The accepted session has a HelloAck queued on its Control lane.
  GameLogic::Session& s = sessions.All().at(GameLogic::EndpointKey(a));
  EXPECT_TRUE(s.Live());
  EXPECT_TRUE(s.events.PendingOutgoing() == 1);
}

TEST(Session, InputFromUnknownEndpointIsIgnored)
{
  ECS::Registry world;
  GameLogic::ServerSessions sessions;

  // No hello yet: input from an unknown endpoint neither spawns nor connects.
  const Net::Endpoint a{ 0x7F000001, 1001 };
  ECS::EntityId e = sessions.OnInput(world, a, Input(1, 0.5f), /*tick*/ 1);

  EXPECT_FALSE(world.IsValid(e));
  EXPECT_TRUE(sessions.Count() == 0);
}

TEST(Session, HelloWithBadVersionIsRejectedWithoutSpawning)
{
  ECS::Registry world;
  GameLogic::ServerSessions sessions;

  const Net::Endpoint a{ 0x7F000001, 1001 };
  Msg::ClientHello bad = Hello();
  bad.protocolVersion = Msg::PROTOCOL_VERSION + 1000u;   // not the server's
  GameLogic::HelloOutcome out = sessions.OnHello(world, a, bad, /*tick*/ 1);

  EXPECT_TRUE(out.result == GameLogic::HelloResult::Rejected);
  EXPECT_FALSE(world.IsValid(out.entity));

  // A pending (entity-less) shell exists only to carry the HelloReject back.
  GameLogic::Session& s = sessions.All().at(GameLogic::EndpointKey(a));
  EXPECT_FALSE(s.Live());
  EXPECT_TRUE(s.events.PendingOutgoing() == 1);   // HelloReject queued
}

TEST(Session, InputAppliesToALiveSessionOnly)
{
  ECS::Registry world;
  GameLogic::ServerSessions sessions;

  const Net::Endpoint a{ 0x7F000001, 1001 };
  ECS::EntityId e = Connect(world, sessions, a);
  ASSERT_TRUE(world.IsValid(e));

  ECS::EntityId back = sessions.OnInput(world, a, Input(1, 0.5f), /*tick*/ 2);
  EXPECT_TRUE(back == e);
  EXPECT_TRUE(world.Get<GameLogic::FlightIntent>(e).throttle == 0.5f);
}

TEST(Session, DistinctEndpointsGetDistinctEntities)
{
  ECS::Registry world;
  GameLogic::ServerSessions sessions;

  ECS::EntityId e1 = Connect(world, sessions, Net::Endpoint{ 0x7F000001, 1001 });
  ECS::EntityId e2 = Connect(world, sessions, Net::Endpoint{ 0x7F000001, 1002 });

  EXPECT_TRUE(sessions.Count() == 2);
  EXPECT_TRUE(e1 != e2);
}

TEST(Session, SameEndpointReusesSessionAndAppliesLatestInput)
{
  ECS::Registry world;
  GameLogic::ServerSessions sessions;

  const Net::Endpoint a{ 0x7F000001, 1001 };
  ECS::EntityId e = Connect(world, sessions, a);

  sessions.OnInput(world, a, Input(5, 0.9f), 2);    // newer -> applied
  EXPECT_TRUE(sessions.Count() == 1);
  EXPECT_TRUE(world.Get<GameLogic::FlightIntent>(e).throttle == 0.9f);

  sessions.OnInput(world, a, Input(3, 0.1f), 3);    // stale seq -> ignored
  EXPECT_TRUE(world.Get<GameLogic::FlightIntent>(e).throttle == 0.9f);
}

TEST(Session, HelloOnALiveSessionRenamesInsteadOfRespawning)
{
  ECS::Registry world;
  GameLogic::ServerSessions sessions;

  const Net::Endpoint a{ 0x7F000001, 1001 };
  ECS::EntityId e = Connect(world, sessions, a, "Jameson");

  // A second hello on the same (live) endpoint keeps the entity and just renames.
  GameLogic::HelloOutcome out = sessions.OnHello(world, a, Hello("Raxxla"), /*tick*/ 2);
  EXPECT_TRUE(out.result == GameLogic::HelloResult::NameChanged);
  EXPECT_TRUE(out.entity == e);
  EXPECT_TRUE(out.nameChanged);
  EXPECT_EQ(sessions.All().at(GameLogic::EndpointKey(a)).name, "Raxxla");
  EXPECT_EQ(world.Get<GameLogic::PlayerRecord>(e).name, "Raxxla");
}

TEST(Session, IdleSessionsAreReapedAndEntitiesDestroyed)
{
  ECS::Registry world;
  GameLogic::ServerSessions sessions;

  ECS::EntityId e1 = Connect(world, sessions, Net::Endpoint{ 0x7F000001, 1001 });
  ECS::EntityId e2 = Connect(world, sessions, Net::Endpoint{ 0x7F000001, 1002 });

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
  Connect(world, sessions, a, "", /*tick*/ 98);
  // tick 100, timeout 5: 100 - 98 = 2 <= 5, so it stays.
  sessions.Reap(world, 100, 5);
  EXPECT_TRUE(sessions.Count() == 1);
}

TEST(Session, NewSessionGetsADefaultNameAndPlayerRecord)
{
  ECS::Registry world;
  GameLogic::ServerSessions sessions;

  const Net::Endpoint a{ 0x7F000001, 3000 };
  ECS::EntityId e = Connect(world, sessions, a);   // blank hello name -> placeholder

  GameLogic::Session& s = sessions.All().at(GameLogic::EndpointKey(a));
  EXPECT_FALSE(s.name.empty());                                   // a placeholder was assigned
  ASSERT_TRUE(world.Has<GameLogic::PlayerRecord>(e));
  EXPECT_EQ(world.Get<GameLogic::PlayerRecord>(e).name, s.name);  // mirrored onto the record
}

TEST(Session, HelloNameIsSanitizedAndMirroredToTheRecord)
{
  ECS::Registry world;
  GameLogic::ServerSessions sessions;

  const Net::Endpoint a{ 0x7F000001, 3001 };
  // Control chars are dropped; the printable remainder is kept.
  ECS::EntityId e = Connect(world, sessions, a, std::string("Ja\x01me\x7Fson"));

  GameLogic::Session& s = sessions.All().at(GameLogic::EndpointKey(a));
  EXPECT_EQ(s.name, "Jameson");
  EXPECT_EQ(world.Get<GameLogic::PlayerRecord>(e).name, "Jameson");
}

TEST(Session, ApplyNameKeepsDefaultForABlankName)
{
  ECS::Registry world;
  GameLogic::ServerSessions sessions;

  const Net::Endpoint a{ 0x7F000001, 3002 };
  Connect(world, sessions, a);
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
  Connect(world, sessions, a);
  Connect(world, sessions, b);

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
  ECS::EntityId e = Connect(world, sessions, a, "Elite");
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

TEST(Session, PendingShellIsExcludedFromTheRoster)
{
  ECS::Registry world;
  GameLogic::ServerSessions sessions;

  // A bad-version hello leaves a pending shell (no entity): it must not appear in
  // the roster, which only carries live players.
  const Net::Endpoint a{ 0x7F000001, 3006 };
  Msg::ClientHello bad = Hello();
  bad.protocolVersion = Msg::PROTOCOL_VERSION + 1u;
  sessions.OnHello(world, a, bad, /*tick*/ 1);

  EXPECT_TRUE(sessions.Count() == 1);              // the shell exists...
  EXPECT_TRUE(sessions.Roster(world).empty());     // ...but is not a roster member
}

TEST(Session, ClientAckClearsTheReliableQueue)
{
  ECS::Registry world;
  GameLogic::ServerSessions sessions;

  const Net::Endpoint a{ 0x7F000001, 2000 };
  Connect(world, sessions, a);

  GameLogic::Session& s = sessions.All().at(GameLogic::EndpointKey(a));
  EXPECT_TRUE(s.events.PendingOutgoing() == 1);   // HelloAck pending (Control lane)

  // Client receives the handshake datagram(s) and acks them back.
  Msg::MessageEndpoint client;
  for (const std::vector<uint8_t>& dg : s.events.WriteDatagrams())
    client.OnDatagram(dg.data(), dg.size());
  for (const std::vector<uint8_t>& dg : client.WriteDatagrams())
    sessions.OnReliable(a, dg.data(), dg.size(), /*tick*/ 2);

  EXPECT_TRUE(s.events.PendingOutgoing() == 0);   // handshake acknowledged
}
