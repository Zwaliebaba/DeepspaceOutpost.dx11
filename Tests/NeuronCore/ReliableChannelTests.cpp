#include <gtest/gtest.h>

#include <string>
#include <vector>

#include "DataWriter.h"
#include "ReliableChannel.h"
#include "Messages/Reliable.h"
#include "Messages/Defs/CoreEvents.h"

using namespace Neuron;

namespace
{
  // A raw (seq, type, payload) tuple for hand-built packets.
  struct RawMsg
  {
    uint32_t seq;
    uint16_t type;
    std::vector<uint8_t> payload;
  };

  // Hand-build an event packet with an explicit ack and raw messages, so tests
  // can inject arbitrary orderings, gaps and duplicates that a well-behaved
  // sender would never produce on its own.
  std::vector<uint8_t> BuildPacket(uint32_t _ack, const std::vector<RawMsg>& _msgs)
  {
    Net::DataWriter w;
    w.WriteU32(Net::EVENT_MAGIC);
    w.WriteU16(Net::EVENT_VERSION);
    w.WriteU32(_ack);
    w.WriteU16(static_cast<uint16_t>(_msgs.size()));
    for (const RawMsg& m : _msgs)
    {
      w.WriteU32(m.seq);
      w.WriteU16(m.type);
      w.WriteU16(static_cast<uint16_t>(m.payload.size()));
      for (uint8_t b : m.payload)
        w.WriteU8(b);
    }
    return w.Bytes();
  }

  bool ReceiveDespawn(Net::ReliableChannel& _ch, uint32_t& _id)
  {
    Net::ReliableMessage m;
    if (!_ch.Receive(m))
      return false;
    Msg::EntityDespawn ds;
    if (!Msg::TryDecode(m, ds))
      return false;
    _id = ds.entityId;
    return true;
  }
}

TEST(Reliable, DeliversMessagesInOrder)
{
  Net::ReliableChannel server;
  Net::ReliableChannel client;

  Msg::SendReliable(server, Msg::EntityDespawn{ 11 });
  Msg::SendReliable(server, Msg::EntityDespawn{ 22 });

  std::vector<uint8_t> pkt = server.WritePacket();
  EXPECT_TRUE(client.ReadPacket(pkt.data(), pkt.size()));

  uint32_t id = 0;
  EXPECT_TRUE(ReceiveDespawn(client, id));
  EXPECT_TRUE(id == 11);
  EXPECT_TRUE(ReceiveDespawn(client, id));
  EXPECT_TRUE(id == 22);
  EXPECT_TRUE(!client.HasReady());
}

TEST(Reliable, AckClearsTheOutgoingQueue)
{
  Net::ReliableChannel server;
  Net::ReliableChannel client;

  Msg::SendReliable(server, Msg::EntityDeath{ 5, 9 });
  EXPECT_TRUE(server.PendingOutgoing() == 1);

  std::vector<uint8_t> toClient = server.WritePacket();
  EXPECT_TRUE(client.ReadPacket(toClient.data(), toClient.size()));

  // The client's reply carries its cumulative ack; the server then drops it.
  std::vector<uint8_t> ackPkt = client.WritePacket();
  EXPECT_TRUE(server.ReadPacket(ackPkt.data(), ackPkt.size()));
  EXPECT_TRUE(server.PendingOutgoing() == 0);
}

TEST(Reliable, ResendsUntilAcked)
{
  Net::ReliableChannel server;
  Net::ReliableChannel client;

  Msg::SendReliable(server, Msg::Chat{ 1, "hello" });

  // First packet is "lost" (built but never delivered).
  (void)server.WritePacket();
  EXPECT_TRUE(server.PendingOutgoing() == 1);   // still unacked

  // The next packet re-includes the same message, so it still arrives.
  std::vector<uint8_t> retry = server.WritePacket();
  EXPECT_TRUE(client.ReadPacket(retry.data(), retry.size()));

  Net::ReliableMessage m;
  EXPECT_TRUE(client.Receive(m));
  Msg::Chat chat;
  EXPECT_TRUE(Msg::TryDecode(m, chat));
  EXPECT_TRUE(chat.sender == 1);
  EXPECT_TRUE(chat.text == "hello");
}

TEST(Reliable, BuffersGapsAndFlushesWhenFilled)
{
  Net::ReliableChannel client;

  const uint16_t despawnType = Msg::Raw(Msg::EntityDespawn::Id);

  // Deliver sequence 2 before sequence 1 (a reordering): nothing is ready yet.
  std::vector<uint8_t> p2 = BuildPacket(0, { { 2, despawnType, Msg::Encode(Msg::EntityDespawn{ 222 }) } });
  EXPECT_TRUE(client.ReadPacket(p2.data(), p2.size()));
  EXPECT_TRUE(!client.HasReady());
  EXPECT_TRUE(client.NextExpected() == 1);

  // Now sequence 1 arrives: it is delivered, then the buffered 2 flushes behind it.
  std::vector<uint8_t> p1 = BuildPacket(0, { { 1, despawnType, Msg::Encode(Msg::EntityDespawn{ 111 }) } });
  EXPECT_TRUE(client.ReadPacket(p1.data(), p1.size()));

  uint32_t id = 0;
  EXPECT_TRUE(ReceiveDespawn(client, id));
  EXPECT_TRUE(id == 111);
  EXPECT_TRUE(ReceiveDespawn(client, id));
  EXPECT_TRUE(id == 222);
  EXPECT_TRUE(client.NextExpected() == 3);
}

TEST(Reliable, IgnoresDuplicates)
{
  Net::ReliableChannel client;

  std::vector<uint8_t> p =
    BuildPacket(0, { { 1, Msg::Raw(Msg::EntityDespawn::Id), Msg::Encode(Msg::EntityDespawn{ 7 }) } });

  EXPECT_TRUE(client.ReadPacket(p.data(), p.size()));
  EXPECT_TRUE(client.ReadPacket(p.data(), p.size()));   // same packet again

  uint32_t id = 0;
  EXPECT_TRUE(ReceiveDespawn(client, id));
  EXPECT_TRUE(id == 7);
  EXPECT_TRUE(!client.HasReady());                       // not delivered twice
}

TEST(Reliable, PacketStaysWithinMtu)
{
  Net::ReliableChannel server;
  for (int i = 0; i < 500; ++i)
    Msg::SendReliable(server, Msg::Chat{ static_cast<uint32_t>(i), "a fairly chatty line of text for sizing" });

  std::vector<uint8_t> pkt = server.WritePacket(Net::SAFE_UDP_PAYLOAD);
  EXPECT_TRUE(pkt.size() <= Net::SAFE_UDP_PAYLOAD);
  // Not everything fit, so the rest remain queued for subsequent packets.
  EXPECT_TRUE(server.PendingOutgoing() == 500);
}

TEST(Reliable, EventEncodingsRoundTrip)
{
  Net::ReliableMessage despawn{ Msg::Raw(Msg::EntityDespawn::Id), Msg::Encode(Msg::EntityDespawn{ 99 }) };
  Msg::EntityDespawn ds;
  EXPECT_TRUE(Msg::TryDecode(despawn, ds));
  EXPECT_TRUE(ds.entityId == 99);

  Net::ReliableMessage death{ Msg::Raw(Msg::EntityDeath::Id), Msg::Encode(Msg::EntityDeath{ 3, 8 }) };
  Msg::EntityDeath d;
  EXPECT_TRUE(Msg::TryDecode(death, d));
  EXPECT_TRUE(d.victim == 3);
  EXPECT_TRUE(d.killer == 8);

  Net::ReliableMessage chat{ Msg::Raw(Msg::Chat::Id), Msg::Encode(Msg::Chat{ 42, "gg wp" }) };
  Msg::Chat c;
  EXPECT_TRUE(Msg::TryDecode(chat, c));
  EXPECT_TRUE(c.sender == 42);
  EXPECT_TRUE(c.text == "gg wp");

  // A decoder rejects a mismatched id.
  EXPECT_TRUE(!Msg::TryDecode(despawn, d));
}

TEST(Reliable, SurvivesLossyReorderingLink)
{
  // Drive a full duplex loop where every other packet in each direction is
  // dropped and packets are applied with a one-step delay (reordering). Despite
  // that, all messages must arrive exactly once and in order.
  Net::ReliableChannel server;
  Net::ReliableChannel client;

  for (int i = 0; i < 20; ++i)
    Msg::SendReliable(server, Msg::EntityDespawn{ static_cast<uint32_t>(1000 + i) });

  // Loop until everything is both delivered AND acked. Each direction lands only
  // occasionally (heavy loss); unacked messages are resent every packet, so they
  // still get through. Distinct, offset cadences exercise repeated resends.
  std::vector<uint32_t> got;
  for (int round = 0;
       round < 400 && (got.size() < 20 || server.PendingOutgoing() > 0);
       ++round)
  {
    std::vector<uint8_t> s2c = server.WritePacket();
    if ((round % 3) == 2)   // server->client lands ~1/3 of the time
      EXPECT_TRUE(client.ReadPacket(s2c.data(), s2c.size()));

    Net::ReliableMessage m;
    while (client.Receive(m))
    {
      Msg::EntityDespawn ds;
      EXPECT_TRUE(Msg::TryDecode(m, ds));
      got.push_back(ds.entityId);
    }

    std::vector<uint8_t> c2s = client.WritePacket();   // carries the ack back
    if ((round % 4) == 3)   // acks land ~1/4 of the time
      EXPECT_TRUE(server.ReadPacket(c2s.data(), c2s.size()));
  }

  EXPECT_TRUE(got.size() == 20);
  bool ordered = true;
  for (int i = 0; i < 20; ++i)
    if (got[static_cast<std::size_t>(i)] != static_cast<uint32_t>(1000 + i))
      ordered = false;
  EXPECT_TRUE(ordered);
  EXPECT_TRUE(server.PendingOutgoing() == 0);   // everything eventually acked
}
