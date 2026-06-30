#include <gtest/gtest.h>

#include <vector>

#include "Messages/PacketInspect.h"
#include "Messages/Framing.h"
#include "Messages/MessageEndpoint.h"
#include "Messages/Catalog.h"

using namespace Neuron;

// Hostile/garbage bytes must never crash or read out of bounds - every parser must
// reject them cleanly. (The engine forbids wall-clock RNG; a fixed-seed LCG keeps
// the fuzz deterministic and reproducible.)
TEST(MessageFuzz, GarbageBytesAreRejectedWithoutCrashing)
{
  uint32_t rng = 0xC0FFEEu;
  auto next = [&rng]() { rng = rng * 1664525u + 1013904223u; return rng; };

  Msg::MessageEndpoint ep;
  for (int iter = 0; iter < 4000; ++iter)
  {
    std::vector<uint8_t> buf(next() % 96);
    for (uint8_t& b : buf)
      b = static_cast<uint8_t>(next() & 0xFF);

    // None of these may crash or read past the buffer; reaching the end is the pass.
    (void)Msg::InspectPacket(buf.data(), buf.size(), Msg::GlobalRegistry());
    Msg::PacketHeader hdr;
    std::vector<Msg::Record> recs;
    (void)Msg::ReadPacket(buf.data(), buf.size(), hdr, recs);
    (void)ep.OnDatagram(buf.data(), buf.size());
  }
  SUCCEED();
}

// Every strict prefix of a valid reliable datagram is a truncation and must be
// rejected; the full datagram is accepted.
TEST(MessageFuzz, TruncatedReliableDatagramsRejected)
{
  Msg::MessageEndpoint src;
  src.Send(Msg::EntityDeath{ 7, 8 });
  const std::vector<std::vector<uint8_t>> dgs = src.WriteDatagrams();
  ASSERT_FALSE(dgs.empty());
  const std::vector<uint8_t>& dg = dgs[0];

  for (std::size_t n = 0; n < dg.size(); ++n)
  {
    Msg::MessageEndpoint ep;
    EXPECT_FALSE(ep.OnDatagram(dg.data(), n));   // truncated -> rejected
    (void)Msg::InspectPacket(dg.data(), n, Msg::GlobalRegistry());   // and never crashes
  }

  Msg::MessageEndpoint full;
  EXPECT_TRUE(full.OnDatagram(dg.data(), dg.size()));   // the whole datagram is accepted
}

// A record whose declared length overruns the packet is rejected (no over-read).
TEST(MessageFuzz, OverlongRecordLengthRejected)
{
  Net::DataWriter w;
  w.WriteU32(Msg::MESSAGE_MAGIC);
  w.WriteU16(Msg::PROTOCOL_VERSION);
  w.WriteU8(static_cast<uint8_t>(Msg::MessageLane::Gameplay));
  w.WriteU16(0x0201);     // EntityDeath id
  w.WriteU16(60000);      // claims 60000 bytes that aren't there
  w.WriteU8(1);

  Msg::PacketHeader hdr;
  std::vector<Msg::Record> recs;
  EXPECT_FALSE(Msg::ReadPacket(w.Bytes().data(), w.Bytes().size(), hdr, recs));
  const Msg::InspectedPacket p = Msg::InspectPacket(w.Bytes().data(), w.Bytes().size(), Msg::GlobalRegistry());
  EXPECT_FALSE(p.ok);
}
