#include <gtest/gtest.h>

#include <vector>

#include "Messages/MessageEndpoint.h"
#include "Messages/Reliable.h"          // Msg::TryDecode
#include "Messages/Defs/CoreEvents.h"   // AssignPlayer (Control), EntityDeath (Gameplay)

using namespace Neuron;

namespace
{
  // Deliver every datagram one endpoint wants to send into another.
  void Pump(Msg::MessageEndpoint& _from, Msg::MessageEndpoint& _to)
  {
    for (const std::vector<uint8_t>& dg : _from.WriteDatagrams())
      _to.OnDatagram(dg.data(), dg.size());
  }
}

TEST(MessageEndpoint, RoutesMessagesToLanesAndDrainsByPriority)
{
  Msg::MessageEndpoint server;
  Msg::MessageEndpoint client;

  // Queue a Bulk payload first, then a Gameplay death, then a Control assign.
  server.SendRaw(Msg::MessageLane::Bulk, 0x0210, { 1, 2, 3 });
  server.Send(Msg::EntityDeath{ 7, 3 });          // Gameplay
  server.Send(Msg::AssignPlayer{ 42 });           // Control

  Pump(server, client);

  // Receive drains Control, then Gameplay, then Bulk - regardless of send order.
  Net::ReliableMessage m;
  ASSERT_TRUE(client.Receive(m));
  EXPECT_EQ(m.type, Msg::Raw(Msg::AssignPlayer::Id));   // Control first

  ASSERT_TRUE(client.Receive(m));
  EXPECT_EQ(m.type, Msg::Raw(Msg::EntityDeath::Id));    // then Gameplay
  Msg::EntityDeath d;
  ASSERT_TRUE(Msg::TryDecode(m, d));
  EXPECT_EQ(d.victim, 7u);

  ASSERT_TRUE(client.Receive(m));
  EXPECT_EQ(m.type, 0x0210);                            // then Bulk
  EXPECT_FALSE(client.Receive(m));
}

TEST(MessageEndpoint, BulkLossDoesNotBlockGameplay)
{
  Msg::MessageEndpoint server;
  Msg::MessageEndpoint client;

  server.SendRaw(Msg::MessageLane::Bulk, 0x0210, { 9, 9, 9 });
  server.Send(Msg::EntityDeath{ 1, 2 });   // Gameplay

  // Deliver ONLY the gameplay datagram; drop the bulk one (HOL test).
  for (const std::vector<uint8_t>& dg : server.WriteDatagrams())
  {
    const uint8_t lane = dg[4];   // [magic(4)][lane]
    if (lane == Msg::LaneIndex(Msg::MessageLane::Gameplay))
      client.OnDatagram(dg.data(), dg.size());
    // bulk datagram intentionally dropped
  }

  // Gameplay is delivered even though Bulk is still missing.
  Net::ReliableMessage m;
  ASSERT_TRUE(client.Receive(m));
  EXPECT_EQ(m.type, Msg::Raw(Msg::EntityDeath::Id));
  EXPECT_FALSE(client.Receive(m));   // nothing else ready (bulk lost, not blocking)

  // The bulk message still arrives once its lane is delivered.
  Pump(server, client);
  ASSERT_TRUE(client.Receive(m));
  EXPECT_EQ(m.type, 0x0210);
}

TEST(MessageEndpoint, AcksFlowAndStopResends)
{
  Msg::MessageEndpoint server;
  Msg::MessageEndpoint client;

  server.Send(Msg::EntityDeath{ 5, 6 });
  EXPECT_EQ(server.PendingOutgoing(), 1u);

  Pump(server, client);          // client receives + marks the lane dirty
  Pump(client, server);          // client's ack flows back

  EXPECT_EQ(server.PendingOutgoing(), 0u);   // acked -> no more resends
}

TEST(MessageEndpoint, IdleEndpointSendsNothing)
{
  Msg::MessageEndpoint ep;
  EXPECT_TRUE(ep.WriteDatagrams().empty());   // no pending sends, nothing received
}

TEST(MessageEndpoint, ForeignDatagramRejected)
{
  Msg::MessageEndpoint ep;
  std::vector<uint8_t> foreign = { 0xDE, 0xAD, 0xBE, 0xEF, 0x00 };
  EXPECT_FALSE(ep.OnDatagram(foreign.data(), foreign.size()));
}

TEST(MessageEndpoint, SurvivesLossOnTheBulkLane)
{
  // The bulk lane drops every other datagram; resends still get the payload through,
  // and the gameplay lane is unaffected throughout.
  Msg::MessageEndpoint server;
  Msg::MessageEndpoint client;

  server.SendRaw(Msg::MessageLane::Bulk, 0x0210, { 1 });
  server.SendRaw(Msg::MessageLane::Bulk, 0x0210, { 2 });
  server.Send(Msg::EntityDeath{ 1, 1 });   // gameplay, should arrive promptly

  int round = 0;
  std::vector<uint8_t> bulkGot;
  bool gameplaySeen = false;
  for (; round < 50 && (bulkGot.size() < 2 || server.PendingOutgoing() > 0); ++round)
  {
    for (const std::vector<uint8_t>& dg : server.WriteDatagrams())
    {
      const uint8_t lane = dg[4];
      const bool dropBulk = (lane == Msg::LaneIndex(Msg::MessageLane::Bulk)) && ((round % 2) == 0);
      if (!dropBulk)
        client.OnDatagram(dg.data(), dg.size());
    }
    Net::ReliableMessage m;
    while (client.Receive(m))
    {
      if (m.type == Msg::Raw(Msg::EntityDeath::Id)) gameplaySeen = true;
      else if (m.type == 0x0210) bulkGot.push_back(m.payload.empty() ? 0 : m.payload[0]);
    }
    for (const std::vector<uint8_t>& dg : client.WriteDatagrams())
      server.OnDatagram(dg.data(), dg.size());
  }

  EXPECT_TRUE(gameplaySeen);
  ASSERT_EQ(bulkGot.size(), 2u);
  EXPECT_EQ(bulkGot[0], 1);
  EXPECT_EQ(bulkGot[1], 2);
  EXPECT_EQ(server.PendingOutgoing(), 0u);
}
