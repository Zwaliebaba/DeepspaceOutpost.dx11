#include <gtest/gtest.h>

#include <vector>

#include "GameLogic.h"
#include "ReliableChannel.h"
#include "Messages/MessageEndpoint.h"
#include "Messages/Reliable.h"        // Msg::TryDecode (reconnect test decodes a ClientHello)

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

  // The session token minted for `_ep` on accept (B2). The client learns this from
  // HelloAck and stamps it on every subsequent datagram.
  uint64_t TokenOf(GameLogic::ServerSessions& _sessions, const Net::Endpoint& _ep)
  {
    return _sessions.All().at(GameLogic::EndpointKey(_ep)).token;
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

  // The accepted session has a HelloAck queued on its Control lane, and a nonzero
  // session token (the client's identity from here on).
  GameLogic::Session& s = sessions.All().at(GameLogic::EndpointKey(a));
  EXPECT_TRUE(s.Live());
  EXPECT_TRUE(s.events.PendingOutgoing() == 1);
  EXPECT_NE(s.token, 0u);
}

TEST(Session, InputFromUnknownEndpointIsIgnored)
{
  ECS::Registry world;
  GameLogic::ServerSessions sessions;

  // No hello yet: input from an unknown endpoint neither spawns nor connects,
  // whether it is token-less or carries a bogus token.
  const Net::Endpoint a{ 0x7F000001, 1001 };
  EXPECT_FALSE(world.IsValid(sessions.OnInput(world, a, /*token*/ 0, Input(1, 0.5f), 1)));
  EXPECT_FALSE(world.IsValid(sessions.OnInput(world, a, /*token*/ 0xDEADBEEFu, Input(2, 0.5f), 1)));
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

  // A pending (entity-less, token-less) shell exists only to carry the HelloReject.
  GameLogic::Session& s = sessions.All().at(GameLogic::EndpointKey(a));
  EXPECT_FALSE(s.Live());
  EXPECT_EQ(s.token, 0u);
  EXPECT_TRUE(s.events.PendingOutgoing() == 1);   // HelloReject queued
}

TEST(Session, InputAppliesOnlyWithTheRightToken)
{
  ECS::Registry world;
  GameLogic::ServerSessions sessions;

  const Net::Endpoint a{ 0x7F000001, 1001 };
  ECS::EntityId e = Connect(world, sessions, a);
  ASSERT_TRUE(world.IsValid(e));
  const uint64_t token = TokenOf(sessions, a);

  // Right token from the session's endpoint: applied.
  EXPECT_TRUE(sessions.OnInput(world, a, token, Input(1, 0.5f), 2) == e);
  EXPECT_TRUE(world.Get<GameLogic::FlightIntent>(e).throttle == 0.5f);

  // Wrong token (spoofed source with a bad token) and token-less input: ignored.
  EXPECT_FALSE(world.IsValid(sessions.OnInput(world, a, token ^ 0x1u, Input(2, 0.9f), 3)));
  EXPECT_FALSE(world.IsValid(sessions.OnInput(world, a, /*token*/ 0, Input(3, 0.9f), 4)));
  EXPECT_TRUE(world.Get<GameLogic::FlightIntent>(e).throttle == 0.5f);   // unchanged
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
  const uint64_t token = TokenOf(sessions, a);

  sessions.OnInput(world, a, token, Input(5, 0.9f), 2);    // newer -> applied
  EXPECT_TRUE(sessions.Count() == 1);
  EXPECT_TRUE(world.Get<GameLogic::FlightIntent>(e).throttle == 0.9f);

  sessions.OnInput(world, a, token, Input(3, 0.1f), 3);    // stale seq -> ignored
  EXPECT_TRUE(world.Get<GameLogic::FlightIntent>(e).throttle == 0.9f);
}

TEST(Session, AuthenticatedEndpointChangeKeepsTheSession)
{
  // A NAT rebind moves the client to a new source address; a correctly-tokened
  // datagram from that address re-binds the (same) session and keeps the entity.
  ECS::Registry world;
  GameLogic::ServerSessions sessions;

  const Net::Endpoint a{ 0x7F000001, 1001 };
  const Net::Endpoint b{ 0x7F000001, 2222 };   // same client, new address
  ECS::EntityId e = Connect(world, sessions, a);
  const uint64_t token = TokenOf(sessions, a);

  // Input arrives from the new endpoint with the same token: same entity, migrated.
  EXPECT_TRUE(sessions.OnInput(world, b, token, Input(1, 0.7f), 2) == e);
  EXPECT_TRUE(sessions.Count() == 1);
  EXPECT_TRUE(sessions.Has(b));
  EXPECT_FALSE(sessions.Has(a));                       // old key vacated
  EXPECT_EQ(TokenOf(sessions, b), token);             // token index followed the move
  EXPECT_TRUE(world.Get<GameLogic::FlightIntent>(e).throttle == 0.7f);
}

TEST(Session, WrongTokenReliableDatagramIsDropped)
{
  // A spoofed reliable datagram carrying the wrong token is dropped before decode:
  // it neither reaches the real session nor provisions a new one.
  ECS::Registry world;
  GameLogic::ServerSessions sessions;

  const Net::Endpoint a{ 0x7F000001, 1001 };
  Connect(world, sessions, a);
  const uint64_t token = TokenOf(sessions, a);

  Msg::MessageEndpoint spoof;
  spoof.SetToken(token ^ 0xABCDu);                    // NOT this (or any) session's token
  spoof.SendRaw(Msg::MessageLane::Gameplay, 0x0400, { 1, 2, 3 });   // any reliable payload

  const Net::Endpoint attacker{ 0x0A000001, 5000 };
  for (const std::vector<uint8_t>& dg : spoof.WriteDatagrams())
    sessions.OnReliable(attacker, dg.data(), dg.size(), /*tick*/ 2);

  EXPECT_TRUE(sessions.Count() == 1);                 // no session provisioned for the attacker
  EXPECT_FALSE(sessions.Has(attacker));
  Net::ReliableMessage m;
  EXPECT_FALSE(sessions.All().at(GameLogic::EndpointKey(a)).events.Receive(m));   // nothing delivered
}

TEST(Session, HelloOnALiveSessionResumesAndReAcks)
{
  ECS::Registry world;
  GameLogic::ServerSessions sessions;

  const Net::Endpoint a{ 0x7F000001, 1001 };
  ECS::EntityId e = Connect(world, sessions, a, "Jameson");
  const uint64_t token = TokenOf(sessions, a);

  // A second hello on the same (live) session is a RECONNECT (B3): the entity and
  // token are kept, any rename applied, and a fresh HelloAck queued so the client
  // re-confirms its identity.
  GameLogic::HelloOutcome out = sessions.OnHello(world, a, Hello("Raxxla"), /*tick*/ 2);
  EXPECT_TRUE(out.result == GameLogic::HelloResult::Resumed);
  EXPECT_TRUE(out.entity == e);
  EXPECT_TRUE(out.nameChanged);
  EXPECT_EQ(TokenOf(sessions, a), token);                        // same identity
  EXPECT_EQ(sessions.All().at(GameLogic::EndpointKey(a)).name, "Raxxla");
  EXPECT_EQ(world.Get<GameLogic::PlayerRecord>(e).name, "Raxxla");
  EXPECT_TRUE(sessions.All().at(GameLogic::EndpointKey(a)).events.PendingOutgoing() == 2);  // 1st + resume HelloAck
}

TEST(Session, ReconnectFromANewEndpointResumesTheSameShip)
{
  // The full B3 path: a token-bearing reconnect from a new address re-binds the
  // session (B2) and resumes it (a fresh HelloAck), keeping the entity + token.
  ECS::Registry world;
  GameLogic::ServerSessions sessions;

  const Net::Endpoint a{ 0x7F000001, 1001 };
  const Net::Endpoint b{ 0x7F000001, 4444 };   // new address after a reconnect
  ECS::EntityId e = Connect(world, sessions, a, "Jameson");
  const uint64_t token = TokenOf(sessions, a);

  // A tokened datagram (here, the reconnect hello's transport) migrates the
  // endpoint; the hello then resumes the live session at its new address.
  Msg::MessageEndpoint client;
  client.SetToken(token);
  client.Send(Hello("Jameson"));   // Control lane
  for (const std::vector<uint8_t>& dg : client.WriteDatagrams())
    sessions.OnReliable(b, dg.data(), dg.size(), /*tick*/ 50);

  // Drain the migrated session's reliable channel and drive OnHello (as the server
  // loop does), from the session's CURRENT endpoint.
  GameLogic::Session& s = sessions.All().at(GameLogic::EndpointKey(b));
  Net::ReliableMessage m;
  bool resumed = false;
  while (s.events.Receive(m))
  {
    Msg::ClientHello h;
    if (Msg::TryDecode(m, h))
      resumed = sessions.OnHello(world, s.endpoint, h, 50).result == GameLogic::HelloResult::Resumed;
  }

  EXPECT_TRUE(resumed);
  EXPECT_TRUE(sessions.Has(b));
  EXPECT_FALSE(sessions.Has(a));
  EXPECT_EQ(sessions.All().at(GameLogic::EndpointKey(b)).entity, e);   // same ship
  EXPECT_EQ(TokenOf(sessions, b), token);                             // same identity
}

TEST(Session, IdleSessionsAreReapedAndTokensForgotten)
{
  ECS::Registry world;
  GameLogic::ServerSessions sessions;

  const Net::Endpoint a{ 0x7F000001, 1001 };
  ECS::EntityId e1 = Connect(world, sessions, a);
  ECS::EntityId e2 = Connect(world, sessions, Net::Endpoint{ 0x7F000001, 1002 });
  const uint64_t token = TokenOf(sessions, a);

  std::vector<uint32_t> gone = sessions.Reap(world, /*tick*/ 100, /*shell*/ 5, /*grace*/ 5);

  EXPECT_TRUE(sessions.Count() == 0);
  EXPECT_TRUE(gone.size() == 2);
  EXPECT_TRUE(!world.IsValid(e1));
  EXPECT_TRUE(!world.IsValid(e2));

  // The token index was pruned with the session: the stale token authenticates
  // nothing (and doesn't resurrect a session).
  EXPECT_FALSE(world.IsValid(sessions.OnInput(world, a, token, Input(1, 0.5f), 101)));
  EXPECT_TRUE(sessions.Count() == 0);
}

TEST(Session, RecentSessionsSurviveReaping)
{
  ECS::Registry world;
  GameLogic::ServerSessions sessions;

  const Net::Endpoint a{ 0x7F000001, 1001 };
  Connect(world, sessions, a, "", /*tick*/ 98);
  // tick 100, grace 5: 100 - 98 = 2 <= 5, so it stays.
  sessions.Reap(world, 100, /*shell*/ 5, /*grace*/ 5);
  EXPECT_TRUE(sessions.Count() == 1);
}

TEST(Session, LiveSessionSurvivesTheShellTimeoutWithinItsGraceWindow)
{
  // B3: an authenticated (live) session gets the long grace window, while a
  // pending pre-hello shell still reaps on the short shell timeout.
  ECS::Registry world;
  GameLogic::ServerSessions sessions;

  const Net::Endpoint live{ 0x7F000001, 1001 };
  Connect(world, sessions, live, "", /*tick*/ 1);   // authenticated

  const Net::Endpoint shell{ 0x7F000001, 1002 };    // pending: bad-version hello
  Msg::ClientHello bad = Hello();
  bad.protocolVersion = Msg::PROTOCOL_VERSION + 1u;
  sessions.OnHello(world, shell, bad, /*tick*/ 1);

  // At tick 200 the shell (idle 199 > shell 100) reaps; the live session (idle
  // 199 <= grace 1800) survives.
  sessions.Reap(world, /*tick*/ 200, /*shell*/ 100, /*grace*/ 1800);
  EXPECT_TRUE(sessions.Has(live));
  EXPECT_FALSE(sessions.Has(shell));
  EXPECT_TRUE(sessions.Count() == 1);
}

TEST(Session, SafeParkZeroesTheIntentOfASilentShip)
{
  ECS::Registry world;
  GameLogic::ServerSessions sessions;

  const Net::Endpoint a{ 0x7F000001, 1001 };
  ECS::EntityId e = Connect(world, sessions, a);
  const uint64_t token = TokenOf(sessions, a);
  sessions.OnInput(world, a, token, Input(1, 1.0f), /*tick*/ 2);   // full throttle
  ASSERT_TRUE(world.Get<GameLogic::FlightIntent>(e).throttle == 1.0f);

  // Not silent long enough yet (2 < parkAfter 45): intent stays.
  sessions.SafeParkSilent(world, /*tick*/ 40, /*parkAfter*/ 45);
  EXPECT_TRUE(world.Get<GameLogic::FlightIntent>(e).throttle == 1.0f);

  // Silent past the park threshold (tick 100, last seen tick 2): intent zeroed.
  sessions.SafeParkSilent(world, /*tick*/ 100, /*parkAfter*/ 45);
  EXPECT_TRUE(world.Get<GameLogic::FlightIntent>(e).throttle == 0.0f);
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

TEST(Session, DeferredHelloParksWithoutSpawning)
{
  // B4: with deferred spawn, a valid hello parks the session (records the name,
  // no entity, no HelloAck) so the caller can load the commander first.
  ECS::Registry world;
  GameLogic::ServerSessions sessions;

  const Net::Endpoint a{ 0x7F000001, 7001 };
  GameLogic::HelloOutcome out = sessions.OnHello(world, a, Hello("Jameson"), /*tick*/ 1, /*deferSpawn*/ true);

  EXPECT_TRUE(out.result == GameLogic::HelloResult::Loading);
  EXPECT_FALSE(world.IsValid(out.entity));
  GameLogic::Session& s = sessions.All().at(GameLogic::EndpointKey(a));
  EXPECT_TRUE(s.loading);
  EXPECT_FALSE(s.Live());
  EXPECT_EQ(s.name, "Jameson");                       // the parked load key
  EXPECT_TRUE(s.events.PendingOutgoing() == 0);       // no HelloAck yet
  EXPECT_EQ(s.token, 0u);                             // no token until spawn
}

TEST(Session, SpawnLoadedCompletesTheDeferredHandshake)
{
  ECS::Registry world;
  GameLogic::ServerSessions sessions;

  const Net::Endpoint a{ 0x7F000001, 7002 };
  sessions.OnHello(world, a, Hello("Jameson"), /*tick*/ 1, /*deferSpawn*/ true);

  ECS::EntityId e = sessions.SpawnLoaded(world, a, /*tick*/ 2);
  ASSERT_TRUE(world.IsValid(e));
  GameLogic::Session& s = sessions.All().at(GameLogic::EndpointKey(a));
  EXPECT_FALSE(s.loading);
  EXPECT_TRUE(s.Live());
  EXPECT_TRUE(s.entity == e);
  EXPECT_NE(s.token, 0u);                             // token minted on spawn
  EXPECT_TRUE(s.events.PendingOutgoing() == 1);       // HelloAck queued
  EXPECT_EQ(world.Get<GameLogic::PlayerRecord>(e).name, "Jameson");

  // A second SpawnLoaded is a no-op (the session is no longer loading).
  EXPECT_FALSE(world.IsValid(sessions.SpawnLoaded(world, a, 3)));
}

TEST(Session, LoadingSessionSurvivesOnTheGraceWindow)
{
  // A parked (loading) session gets the long grace window, not the short shell
  // timeout, so a slow load isn't reaped out from under it.
  ECS::Registry world;
  GameLogic::ServerSessions sessions;

  const Net::Endpoint a{ 0x7F000001, 7003 };
  sessions.OnHello(world, a, Hello("Slow"), /*tick*/ 1, /*deferSpawn*/ true);

  sessions.Reap(world, /*tick*/ 200, /*shell*/ 100, /*grace*/ 1800);
  EXPECT_TRUE(sessions.Has(a));                        // survived the shell timeout
  EXPECT_TRUE(sessions.All().at(GameLogic::EndpointKey(a)).loading);
}

TEST(Session, ClientAckClearsTheReliableQueue)
{
  ECS::Registry world;
  GameLogic::ServerSessions sessions;

  const Net::Endpoint a{ 0x7F000001, 2000 };
  Connect(world, sessions, a);

  GameLogic::Session& s = sessions.All().at(GameLogic::EndpointKey(a));
  EXPECT_TRUE(s.events.PendingOutgoing() == 1);   // HelloAck pending (Control lane)

  // Client receives the handshake datagram(s), adopts the token from HelloAck, and
  // acks back - its acks now carry the token so the server routes them to us.
  Msg::MessageEndpoint client;
  client.SetToken(s.token);
  for (const std::vector<uint8_t>& dg : s.events.WriteDatagrams())
    client.OnDatagram(dg.data(), dg.size());
  for (const std::vector<uint8_t>& dg : client.WriteDatagrams())
    sessions.OnReliable(a, dg.data(), dg.size(), /*tick*/ 2);

  EXPECT_TRUE(s.events.PendingOutgoing() == 0);   // handshake acknowledged
}
