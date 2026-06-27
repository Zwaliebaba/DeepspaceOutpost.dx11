#include "TestFramework.h"

#include <string>
#include <vector>

#include "DataWriter.h"
#include "ReliableChannel.h"
#include "GameEvents.h"

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
    return Net::DecodeDespawn(m, _id);
  }
}

TEST(Reliable_DeliversMessagesInOrder)
{
  Net::ReliableChannel server;
  Net::ReliableChannel client;

  Net::SendDespawn(server, 11);
  Net::SendDespawn(server, 22);

  std::vector<uint8_t> pkt = server.WritePacket();
  CHECK(client.ReadPacket(pkt.data(), pkt.size()));

  uint32_t id = 0;
  CHECK(ReceiveDespawn(client, id));
  CHECK(id == 11);
  CHECK(ReceiveDespawn(client, id));
  CHECK(id == 22);
  CHECK(!client.HasReady());
}

TEST(Reliable_AckClearsTheOutgoingQueue)
{
  Net::ReliableChannel server;
  Net::ReliableChannel client;

  Net::SendDeath(server, 5, 9);
  CHECK(server.PendingOutgoing() == 1);

  std::vector<uint8_t> toClient = server.WritePacket();
  CHECK(client.ReadPacket(toClient.data(), toClient.size()));

  // The client's reply carries its cumulative ack; the server then drops it.
  std::vector<uint8_t> ackPkt = client.WritePacket();
  CHECK(server.ReadPacket(ackPkt.data(), ackPkt.size()));
  CHECK(server.PendingOutgoing() == 0);
}

TEST(Reliable_ResendsUntilAcked)
{
  Net::ReliableChannel server;
  Net::ReliableChannel client;

  Net::SendChat(server, 1, "hello");

  // First packet is "lost" (built but never delivered).
  (void)server.WritePacket();
  CHECK(server.PendingOutgoing() == 1);   // still unacked

  // The next packet re-includes the same message, so it still arrives.
  std::vector<uint8_t> retry = server.WritePacket();
  CHECK(client.ReadPacket(retry.data(), retry.size()));

  Net::ReliableMessage m;
  CHECK(client.Receive(m));
  uint32_t sender = 0;
  std::string text;
  CHECK(Net::DecodeChat(m, sender, text));
  CHECK(sender == 1);
  CHECK(text == "hello");
}

TEST(Reliable_BuffersGapsAndFlushesWhenFilled)
{
  Net::ReliableChannel client;

  const uint16_t despawnType = static_cast<uint16_t>(Net::EventType::EntityDespawn);

  // Deliver sequence 2 before sequence 1 (a reordering): nothing is ready yet.
  std::vector<uint8_t> p2 = BuildPacket(0, { { 2, despawnType, Net::EncodeDespawn(222) } });
  CHECK(client.ReadPacket(p2.data(), p2.size()));
  CHECK(!client.HasReady());
  CHECK(client.NextExpected() == 1);

  // Now sequence 1 arrives: it is delivered, then the buffered 2 flushes behind it.
  std::vector<uint8_t> p1 = BuildPacket(0, { { 1, despawnType, Net::EncodeDespawn(111) } });
  CHECK(client.ReadPacket(p1.data(), p1.size()));

  uint32_t id = 0;
  CHECK(ReceiveDespawn(client, id));
  CHECK(id == 111);
  CHECK(ReceiveDespawn(client, id));
  CHECK(id == 222);
  CHECK(client.NextExpected() == 3);
}

TEST(Reliable_IgnoresDuplicates)
{
  Net::ReliableChannel client;

  std::vector<uint8_t> p =
    BuildPacket(0, { { 1, static_cast<uint16_t>(Net::EventType::EntityDespawn), Net::EncodeDespawn(7) } });

  CHECK(client.ReadPacket(p.data(), p.size()));
  CHECK(client.ReadPacket(p.data(), p.size()));   // same packet again

  uint32_t id = 0;
  CHECK(ReceiveDespawn(client, id));
  CHECK(id == 7);
  CHECK(!client.HasReady());                       // not delivered twice
}

TEST(Reliable_PacketStaysWithinMtu)
{
  Net::ReliableChannel server;
  for (int i = 0; i < 500; ++i)
    Net::SendChat(server, static_cast<uint32_t>(i), "a fairly chatty line of text for sizing");

  std::vector<uint8_t> pkt = server.WritePacket(Net::SAFE_UDP_PAYLOAD);
  CHECK(pkt.size() <= Net::SAFE_UDP_PAYLOAD);
  // Not everything fit, so the rest remain queued for subsequent packets.
  CHECK(server.PendingOutgoing() == 500);
}

TEST(Reliable_EventEncodingsRoundTrip)
{
  Net::ReliableMessage despawn{ static_cast<uint16_t>(Net::EventType::EntityDespawn), Net::EncodeDespawn(99) };
  uint32_t id = 0;
  CHECK(Net::DecodeDespawn(despawn, id));
  CHECK(id == 99);

  Net::ReliableMessage death{ static_cast<uint16_t>(Net::EventType::EntityDeath), Net::EncodeDeath(3, 8) };
  uint32_t victim = 0, killer = 0;
  CHECK(Net::DecodeDeath(death, victim, killer));
  CHECK(victim == 3);
  CHECK(killer == 8);

  Net::ReliableMessage chat{ static_cast<uint16_t>(Net::EventType::Chat), Net::EncodeChat(42, "gg wp") };
  uint32_t sender = 0;
  std::string text;
  CHECK(Net::DecodeChat(chat, sender, text));
  CHECK(sender == 42);
  CHECK(text == "gg wp");

  // A decoder rejects a mismatched type.
  CHECK(!Net::DecodeDeath(despawn, victim, killer));
}

TEST(Reliable_SurvivesLossyReorderingLink)
{
  // Drive a full duplex loop where every other packet in each direction is
  // dropped and packets are applied with a one-step delay (reordering). Despite
  // that, all messages must arrive exactly once and in order.
  Net::ReliableChannel server;
  Net::ReliableChannel client;

  for (int i = 0; i < 20; ++i)
    Net::SendDespawn(server, static_cast<uint32_t>(1000 + i));

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
      CHECK(client.ReadPacket(s2c.data(), s2c.size()));

    Net::ReliableMessage m;
    while (client.Receive(m))
    {
      uint32_t id = 0;
      CHECK(Net::DecodeDespawn(m, id));
      got.push_back(id);
    }

    std::vector<uint8_t> c2s = client.WritePacket();   // carries the ack back
    if ((round % 4) == 3)   // acks land ~1/4 of the time
      CHECK(server.ReadPacket(c2s.data(), c2s.size()));
  }

  CHECK(got.size() == 20);
  bool ordered = true;
  for (int i = 0; i < 20; ++i)
    if (got[static_cast<std::size_t>(i)] != static_cast<uint32_t>(1000 + i))
      ordered = false;
  CHECK(ordered);
  CHECK(server.PendingOutgoing() == 0);   // everything eventually acked
}
