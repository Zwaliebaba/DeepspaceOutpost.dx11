#include <gtest/gtest.h>

#include <map>
#include <optional>
#include <utility>
#include <vector>

#include "AdminChannel.h"                 // Neuron::Server::AdminChannel
#include "NetLib.h"                       // Net::Endpoint / MakeEndpoint
#include "Messages/MessageEndpoint.h"     // the manager side of the wire
#include "Messages/Reliable.h"            // Msg::TryDecode
#include "Messages/Defs/Admin.h"

using namespace Neuron;
using Server::AdminChannel;

namespace
{
  // The manager side of the management channel: a bare reliable endpoint plus the
  // decoded mirror it builds from what the server pushes. Stands in for SM4's
  // AdminClient, which is this same MessageEndpoint + decode loop with a socket.
  struct FakeManager
  {
    Msg::MessageEndpoint ep;
    Net::Endpoint addr;

    std::optional<Msg::AdminHelloAck> ack;
    std::optional<Msg::AdminHelloReject> reject;
    std::vector<Msg::AdminEvent> events;
    std::map<uint32_t, Msg::AdminPlayerInfo> roster;
    std::vector<uint32_t> gone;
    std::optional<Msg::AdminHealth> health;

    explicit FakeManager(uint16_t _port) { addr = Net::MakeEndpoint(127, 0, 0, 1, _port); }

    // Queue the opening handshake (token 0 until the ack arrives).
    void SendHello(const std::string& _key)
    {
      ep.Send(Msg::AdminHello{ Msg::PROTOCOL_VERSION, _key });
    }

    // Decode everything the endpoint has delivered into the mirror; adopt the admin
    // token the moment the ack arrives so subsequent datagrams authenticate.
    void Drain()
    {
      Net::ReliableMessage m;
      while (ep.Receive(m))
      {
        Msg::AdminHelloAck a;
        Msg::AdminHelloReject r;
        Msg::AdminEvent e;
        Msg::AdminPlayerInfo pi;
        Msg::AdminPlayerGone g;
        Msg::AdminHealth h;
        if (Msg::TryDecode(m, a)) { ack = a; ep.SetToken(a.adminToken); }
        else if (Msg::TryDecode(m, r)) reject = r;
        else if (Msg::TryDecode(m, e)) events.push_back(e);
        else if (Msg::TryDecode(m, pi)) roster[pi.playerId] = pi;
        else if (Msg::TryDecode(m, g)) gone.push_back(g.playerId);
        else if (Msg::TryDecode(m, h)) health = h;
      }
    }
  };

  // One full round trip for a set of managers: manager -> channel, then channel ->
  // manager (routed by endpoint), then each manager decodes what it received.
  void Round(std::vector<FakeManager*> _mgrs, AdminChannel& _ch, uint32_t _tick)
  {
    for (FakeManager* m : _mgrs)
      for (const std::vector<uint8_t>& dg : m->ep.WriteDatagrams())
        _ch.OnDatagram(m->addr, dg.data(), dg.size(), _tick);

    std::vector<std::pair<Net::Endpoint, std::vector<uint8_t>>> out;
    _ch.WriteDatagrams(_tick, out);
    for (const auto& [ep, dg] : out)
      for (FakeManager* m : _mgrs)
        if (m->addr == ep)
          m->ep.OnDatagram(dg.data(), dg.size());

    for (FakeManager* m : _mgrs)
      m->Drain();
  }

  // Pump enough round trips at a fixed tick for the reliable handshake + replay to
  // settle (hello ack + acks of the replay backlog).
  void Settle(std::vector<FakeManager*> _mgrs, AdminChannel& _ch, uint32_t _tick, int _rounds = 6)
  {
    for (int i = 0; i < _rounds; ++i)
      Round(_mgrs, _ch, _tick);
  }

  AdminChannel MakeChannel(const std::string& _key = "s3cret")
  {
    AdminChannel ch(_key);
    ch.SetIdentity(/*gameLogicVersion*/ 42, /*tickRateMs*/ 33, /*gamePort*/ 40000);
    return ch;
  }
}

TEST(AdminChannel, GoodKeyConnectsAndReceivesIdentity)
{
  AdminChannel ch = MakeChannel();
  FakeManager mgr(50001);
  mgr.SendHello("s3cret");

  Settle({ &mgr }, ch, 100);

  ASSERT_TRUE(mgr.ack.has_value());
  EXPECT_NE(mgr.ack->adminToken, 0u);
  EXPECT_EQ(mgr.ack->protocolVersion, Msg::PROTOCOL_VERSION);
  EXPECT_EQ(mgr.ack->gameLogicVersion, 42u);
  EXPECT_EQ(mgr.ack->tickRateMs, 33u);
  EXPECT_EQ(mgr.ack->gamePort, 40000);
  EXPECT_EQ(mgr.ack->serverTick, 100u);
  EXPECT_EQ(ch.SessionCount(), 1u);
  EXPECT_FALSE(mgr.reject.has_value());
}

TEST(AdminChannel, BadKeyIsRejectedAndNoSession)
{
  AdminChannel ch = MakeChannel("correct-horse");
  FakeManager mgr(50002);
  mgr.SendHello("wrong-key");

  Settle({ &mgr }, ch, 10);

  ASSERT_TRUE(mgr.reject.has_value());
  EXPECT_EQ(mgr.reject->reason, static_cast<uint8_t>(Msg::AdminRejectReason::BadKey));
  EXPECT_FALSE(mgr.ack.has_value());
  EXPECT_EQ(ch.SessionCount(), 0u);
}

TEST(AdminChannel, ProtocolMismatchIsRejected)
{
  AdminChannel ch = MakeChannel();
  FakeManager mgr(50003);
  // Hand-build a hello with the wrong version.
  mgr.ep.Send(Msg::AdminHello{ Msg::PROTOCOL_VERSION + 1u, "s3cret" });

  Settle({ &mgr }, ch, 10);

  ASSERT_TRUE(mgr.reject.has_value());
  EXPECT_EQ(mgr.reject->reason, static_cast<uint8_t>(Msg::AdminRejectReason::ProtocolMismatch));
  EXPECT_EQ(ch.SessionCount(), 0u);
}

TEST(AdminChannel, RepeatedBadKeysArmTheFailureMute)
{
  AdminChannel ch = MakeChannel("correct-horse");

  // The realistic brute-force vector: many guesses pipelined on ONE connection (the
  // reliable channel dedups a resend, so distinct guesses each advance the sequence).
  FakeManager attacker(50010);
  for (uint16_t i = 0; i < Server::ADMIN_MAX_KEY_FAILURES; ++i)
    attacker.ep.Send(Msg::AdminHello{ Msg::PROTOCOL_VERSION, "guess" });
  Settle({ &attacker }, ch, 5);
  EXPECT_EQ(ch.SessionCount(), 0u);
  ASSERT_TRUE(attacker.reject.has_value());   // got at least one BadKey reject

  // The endpoint is now muted: a correct key sent in a LATER datagram is dropped
  // before it can be processed (the mute is checked before any decode).
  attacker.ack.reset();
  attacker.ep.Send(Msg::AdminHello{ Msg::PROTOCOL_VERSION, "correct-horse" });
  Settle({ &attacker }, ch, 6);
  EXPECT_FALSE(attacker.ack.has_value());
  EXPECT_EQ(ch.SessionCount(), 0u);
}

TEST(AdminChannel, RecentEventRingIsReplayedInOrderOnConnect)
{
  AdminChannel ch = MakeChannel();

  ch.PushEvent(1, Msg::AdminEventKind::ServerStarted, 0, "server up");
  ch.PushEvent(2, Msg::AdminEventKind::PlayerJoined, 7, "JAMESON joined");
  ch.PushEvent(3, Msg::AdminEventKind::Kill, 7, "JAMESON killed a pirate");

  FakeManager mgr(50004);
  mgr.SendHello("s3cret");
  Settle({ &mgr }, ch, 100);

  ASSERT_EQ(mgr.events.size(), 3u);
  EXPECT_EQ(mgr.events[0].text, "server up");
  EXPECT_EQ(mgr.events[1].text, "JAMESON joined");
  EXPECT_EQ(mgr.events[2].text, "JAMESON killed a pirate");
  EXPECT_LT(mgr.events[0].sequence, mgr.events[1].sequence);
  EXPECT_LT(mgr.events[1].sequence, mgr.events[2].sequence);
}

TEST(AdminChannel, RingIsBoundedAndDropsOldest)
{
  AdminChannel ch = MakeChannel();
  const std::size_t overfill = Server::ADMIN_EVENT_RING + 50;
  for (std::size_t i = 0; i < overfill; ++i)
    ch.PushEvent(static_cast<uint32_t>(i), Msg::AdminEventKind::Chat, 0, "line");

  EXPECT_EQ(ch.EventCount(), Server::ADMIN_EVENT_RING);

  FakeManager mgr(50005);
  mgr.SendHello("s3cret");
  Settle({ &mgr }, ch, 100, /*rounds*/ 20);   // a full ring is a larger replay backlog
  EXPECT_EQ(mgr.events.size(), Server::ADMIN_EVENT_RING);
}

TEST(AdminChannel, LiveEventReachesAConnectedManager)
{
  AdminChannel ch = MakeChannel();
  FakeManager mgr(50006);
  mgr.SendHello("s3cret");
  Settle({ &mgr }, ch, 100);
  ASSERT_TRUE(mgr.ack.has_value());
  mgr.events.clear();

  ch.PushEvent(200, Msg::AdminEventKind::Chat, 7, "hello world");
  Settle({ &mgr }, ch, 101);

  ASSERT_EQ(mgr.events.size(), 1u);
  EXPECT_EQ(mgr.events[0].text, "hello world");
  EXPECT_EQ(mgr.events[0].kind, static_cast<uint8_t>(Msg::AdminEventKind::Chat));
}

TEST(AdminChannel, RosterReplayThenLiveDeltaAndGone)
{
  AdminChannel ch = MakeChannel();

  Msg::AdminPlayerInfo row;
  row.playerId = 7;
  row.entityId = 100;
  row.name = "JAMESON";
  row.state = static_cast<uint8_t>(Msg::AdminPlayerState::Live);
  ch.PushRoster(row);   // present before the manager connects

  FakeManager mgr(50007);
  mgr.SendHello("s3cret");
  Settle({ &mgr }, ch, 100);
  ASSERT_EQ(mgr.roster.count(7), 1u);
  EXPECT_EQ(mgr.roster[7].name, "JAMESON");

  // A live join arrives as a delta.
  Msg::AdminPlayerInfo row2;
  row2.playerId = 9;
  row2.name = "Trader Jane";
  ch.PushRoster(row2);
  Settle({ &mgr }, ch, 101);
  EXPECT_EQ(mgr.roster.count(9), 1u);

  // A leave removes the row on the channel and notifies the manager.
  ch.PushPlayerGone(7, Msg::AdminGoneReason::Disconnected);
  Settle({ &mgr }, ch, 102);
  EXPECT_EQ(ch.RosterSize(), 1u);   // only playerId 9 remains
  ASSERT_FALSE(mgr.gone.empty());
  EXPECT_EQ(mgr.gone.back(), 7u);
}

TEST(AdminChannel, HealthIsDeliveredLiveAndReplayedOnConnect)
{
  AdminChannel ch = MakeChannel();

  Msg::AdminHealth h;
  h.tick = 300;
  h.avgTickMs = 12.5f;
  h.sessions = 3;
  ch.PushHealth(h);   // before any manager connects (remembered as the latest)

  FakeManager mgr(50008);
  mgr.SendHello("s3cret");
  Settle({ &mgr }, ch, 300);
  ASSERT_TRUE(mgr.health.has_value());
  EXPECT_FLOAT_EQ(mgr.health->avgTickMs, 12.5f);
  EXPECT_EQ(mgr.health->sessions, 3u);
}

TEST(AdminChannel, SilentManagerIsReaped)
{
  AdminChannel ch = MakeChannel();
  FakeManager mgr(50009);
  mgr.SendHello("s3cret");
  Settle({ &mgr }, ch, 100);
  ASSERT_EQ(ch.SessionCount(), 1u);

  // Advance well past the live timeout with no traffic from the manager; a lone
  // WriteDatagrams pass reaps it.
  std::vector<std::pair<Net::Endpoint, std::vector<uint8_t>>> out;
  ch.WriteDatagrams(100 + Server::ADMIN_TIMEOUT_TICKS + 1, out);
  EXPECT_EQ(ch.SessionCount(), 0u);
}

TEST(AdminChannel, SessionCapRejectsTheOverflowManager)
{
  AdminChannel ch = MakeChannel();

  std::vector<FakeManager> mgrs;
  mgrs.reserve(Server::MAX_ADMIN_SESSIONS + 1);
  for (std::size_t i = 0; i < Server::MAX_ADMIN_SESSIONS + 1; ++i)
    mgrs.emplace_back(static_cast<uint16_t>(51000 + i));
  for (FakeManager& m : mgrs)
    m.SendHello("s3cret");

  // Connect them one at a time so the cap is reached before the last one's hello.
  for (std::size_t i = 0; i < mgrs.size(); ++i)
  {
    std::vector<FakeManager*> upto;
    for (std::size_t j = 0; j <= i; ++j)
      upto.push_back(&mgrs[j]);
    Settle(upto, ch, 100);
  }

  EXPECT_EQ(ch.SessionCount(), Server::MAX_ADMIN_SESSIONS);
  FakeManager& overflow = mgrs.back();
  EXPECT_FALSE(overflow.ack.has_value());
  ASSERT_TRUE(overflow.reject.has_value());
  EXPECT_EQ(overflow.reject->reason, static_cast<uint8_t>(Msg::AdminRejectReason::ServerBusy));
}

TEST(AdminChannel, DisabledChannelIgnoresEverything)
{
  AdminChannel ch;   // empty key => disabled
  EXPECT_FALSE(ch.Enabled());

  FakeManager mgr(52000);
  mgr.SendHello("anything");
  Settle({ &mgr }, ch, 100);
  EXPECT_FALSE(mgr.ack.has_value());
  EXPECT_FALSE(mgr.reject.has_value());
  EXPECT_EQ(ch.SessionCount(), 0u);
}

TEST(AdminChannel, TokenRebindFollowsAManagerToANewAddress)
{
  AdminChannel ch = MakeChannel();
  FakeManager mgr(53000);
  mgr.SendHello("s3cret");
  Settle({ &mgr }, ch, 100);
  ASSERT_TRUE(mgr.ack.has_value());
  mgr.events.clear();

  // The manager keeps its token but its source address changes (a NAT rebind).
  mgr.addr = Net::MakeEndpoint(127, 0, 0, 2, 53000);
  ch.PushEvent(200, Msg::AdminEventKind::Chat, 0, "after rebind");
  Settle({ &mgr }, ch, 101);

  EXPECT_EQ(ch.SessionCount(), 1u);   // still one session, now bound to the new address
  ASSERT_EQ(mgr.events.size(), 1u);
  EXPECT_EQ(mgr.events[0].text, "after rebind");
}
