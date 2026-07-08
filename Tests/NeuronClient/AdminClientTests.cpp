#include <gtest/gtest.h>

#include <string>
#include <vector>

#include "AdminClient.h"                 // Neuron::Client::AdminClient (the seam under test)
#include "Messages/MessageEndpoint.h"    // the fake server side of the wire
#include "Messages/Framing.h"            // Msg::PROTOCOL_VERSION, PeekReliableToken
#include "Messages/Reliable.h"           // Msg::TryDecode
#include "Messages/Defs/Admin.h"

using namespace Neuron;
using Client::AdminClient;

namespace
{
  // Deliver the client's pending datagrams to the fake server.
  void Up(AdminClient& _c, Msg::MessageEndpoint& _srv)
  {
    for (const std::vector<uint8_t>& dg : _c.TakeOutgoing())
      _srv.OnDatagram(dg.data(), dg.size());
  }

  // Deliver the fake server's pending datagrams to the client at time `_nowMs`.
  void Down(Msg::MessageEndpoint& _srv, AdminClient& _c, double _nowMs)
  {
    for (const std::vector<uint8_t>& dg : _srv.WriteDatagrams())
      _c.Ingest(dg.data(), dg.size(), _nowMs);
    _c.Service(_nowMs);
  }

  // The fake server: receive the client's hello and reply with the given ack.
  void AcceptHello(Msg::MessageEndpoint& _srv, const Msg::AdminHelloAck& _ack)
  {
    Net::ReliableMessage m;
    while (_srv.Receive(m))
    {
      Msg::AdminHello hello;
      if (Msg::TryDecode(m, hello))
        _srv.Send(_ack);
    }
  }

  Msg::AdminHelloAck MakeAck(uint64_t _token)
  {
    Msg::AdminHelloAck ack;
    ack.adminToken = _token;
    ack.protocolVersion = Msg::PROTOCOL_VERSION;
    ack.gameLogicVersion = 7;
    ack.tickRateMs = 33;
    ack.gamePort = 40000;
    ack.serverTick = 500;
    return ack;
  }

  // Drive a full handshake and leave the client Connected.
  void Connect(AdminClient& _c, Msg::MessageEndpoint& _srv, uint64_t _token = 0xABCDEF01ull)
  {
    _c.BeginHandshake("s3cret", 0.0);
    Up(_c, _srv);
    AcceptHello(_srv, MakeAck(_token));
    Down(_srv, _c, 10.0);
  }
}

TEST(AdminClient, HandshakeConnectsAndAdoptsIdentityAndToken)
{
  AdminClient c;
  Msg::MessageEndpoint srv;
  Connect(c, srv, 0xDEADBEEFull);

  EXPECT_EQ(c.GetState(), AdminClient::State::Connected);
  EXPECT_EQ(c.ServerIdentity().adminToken, 0xDEADBEEFull);
  EXPECT_EQ(c.ServerIdentity().gameLogicVersion, 7u);
  EXPECT_EQ(c.ServerIdentity().gamePort, 40000);

  // From here the client stamps the admin token on every outgoing datagram.
  const std::vector<std::vector<uint8_t>> dgs = c.TakeOutgoing();
  ASSERT_FALSE(dgs.empty());
  EXPECT_EQ(Msg::PeekReliableToken(dgs[0].data(), dgs[0].size()), 0xDEADBEEFull);
}

TEST(AdminClient, RejectSetsRejectedState)
{
  AdminClient c;
  Msg::MessageEndpoint srv;
  c.BeginHandshake("wrong", 0.0);
  Up(c, srv);

  // The fake server refuses.
  Net::ReliableMessage m;
  ASSERT_TRUE(srv.Receive(m));
  srv.Send(Msg::AdminHelloReject{ static_cast<uint8_t>(Msg::AdminRejectReason::BadKey) });
  Down(srv, c, 10.0);

  EXPECT_EQ(c.GetState(), AdminClient::State::Rejected);
  EXPECT_EQ(c.RejectReason(), Msg::AdminRejectReason::BadKey);
}

TEST(AdminClient, EventsRosterAndHealthPopulateTheMirror)
{
  AdminClient c;
  Msg::MessageEndpoint srv;
  Connect(c, srv);

  Msg::AdminPlayerInfo row;
  row.playerId = 7;
  row.name = "JAMESON";
  srv.Send(row);
  srv.Send(Msg::AdminEvent{ 1, 100, static_cast<uint8_t>(Msg::AdminEventKind::Kill), 7, "boom" });
  Msg::AdminHealth h;
  h.tick = 200;
  h.avgTickMs = 9.5f;
  h.sessions = 4;
  srv.Send(h);

  Down(srv, c, 20.0);

  ASSERT_EQ(c.Roster().count(7), 1u);
  EXPECT_EQ(c.Roster().at(7).name, "JAMESON");
  ASSERT_EQ(c.Events().size(), 1u);
  EXPECT_EQ(c.Events().front().text, "boom");
  ASSERT_TRUE(c.HasHealth());
  EXPECT_FLOAT_EQ(c.LatestHealth().avgTickMs, 9.5f);
  EXPECT_EQ(c.LatestHealth().sessions, 4u);
}

TEST(AdminClient, PlayerGoneRemovesTheRosterRow)
{
  AdminClient c;
  Msg::MessageEndpoint srv;
  Connect(c, srv);

  Msg::AdminPlayerInfo row;
  row.playerId = 9;
  row.name = "Trader Jane";
  srv.Send(row);
  Down(srv, c, 20.0);
  ASSERT_EQ(c.Roster().count(9), 1u);

  srv.Send(Msg::AdminPlayerGone{ 9, static_cast<uint8_t>(Msg::AdminGoneReason::Disconnected) });
  Down(srv, c, 21.0);
  EXPECT_EQ(c.Roster().count(9), 0u);
}

TEST(AdminClient, EventLogIsBounded)
{
  AdminClient c;
  Msg::MessageEndpoint srv;
  Connect(c, srv);

  const std::size_t overfill = AdminClient::MAX_EVENTS + 20;
  for (std::size_t i = 0; i < overfill; ++i)
    srv.Send(Msg::AdminEvent{ static_cast<uint32_t>(i), 0,
                              static_cast<uint8_t>(Msg::AdminEventKind::Chat), 0, "line" });

  // The Gameplay lane carries one datagram per WriteDatagrams; pump enough rounds for
  // the whole backlog to flow (and be acked so the next batch is sent).
  for (int r = 0; r < 40; ++r)
  {
    Up(c, srv);
    Down(srv, c, 100.0);
  }

  EXPECT_EQ(c.Events().size(), AdminClient::MAX_EVENTS);
}

TEST(AdminClient, ConnectTimeoutWithoutAReply)
{
  AdminClient c;
  c.BeginHandshake("s3cret", 0.0);
  EXPECT_EQ(c.GetState(), AdminClient::State::Connecting);

  c.Service(AdminClient::CONNECT_TIMEOUT_MS + 1.0);
  EXPECT_EQ(c.GetState(), AdminClient::State::TimedOut);
}

TEST(AdminClient, LiveTimeoutWhenTheServerGoesSilent)
{
  AdminClient c;
  Msg::MessageEndpoint srv;
  Connect(c, srv);   // last server contact at t = 10 ms
  ASSERT_EQ(c.GetState(), AdminClient::State::Connected);

  c.Service(10.0 + AdminClient::LIVE_TIMEOUT_MS + 1.0);
  EXPECT_EQ(c.GetState(), AdminClient::State::TimedOut);
}
