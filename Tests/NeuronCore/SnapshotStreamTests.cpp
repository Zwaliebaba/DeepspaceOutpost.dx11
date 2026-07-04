#include <gtest/gtest.h>

#include <vector>

#include "Replication.h"
#include "SnapshotStream.h"

using namespace Neuron;

namespace
{
  // A world with a stationary entity (id 1) and a mover (id 2), integer/axis-aligned
  // so quantization is lossless and reconstruction can be checked for exact equality.
  Net::WorldSnapshot World(uint32_t _tick, int64_t _moverX)
  {
    Net::WorldSnapshot s;
    s.tick = _tick;
    s.viewerId = 1;
    Net::EntitySnapshot a; a.id = 1; a.x = 1000; a.noseZ = 1.0f; a.roofY = 1.0f;   // stationary
    Net::EntitySnapshot b; b.id = 2; b.x = _moverX; b.noseZ = 1.0f; b.roofY = 1.0f; // moving
    s.entities = { a, b };
    return s;
  }

  bool SameEntities(const Net::WorldSnapshot& _a, const Net::WorldSnapshot& _b)
  {
    if (_a.entities.size() != _b.entities.size())
      return false;
    for (const Net::EntitySnapshot& ea : _a.entities)
    {
      bool ok = false;
      for (const Net::EntitySnapshot& eb : _b.entities)
        if (ea.id == eb.id) { ok = Net::SameOnWire(ea, eb); break; }
      if (!ok)
        return false;
    }
    return true;
  }

  // Deliver every datagram to the decoder and return the last snapshot it
  // reconstructed (the delta/full for this tick reconstructs the whole world).
  bool DeliverAll(Net::SnapshotStreamDecoder& _dec,
                  const std::vector<std::vector<uint8_t>>& _dgrams, Net::WorldSnapshot& _out)
  {
    bool any = false;
    for (const std::vector<uint8_t>& dg : _dgrams)
    {
      Net::WorldSnapshot out;
      if (_dec.Decode(dg.data(), dg.size(), out)) { _out = out; any = true; }
    }
    return any;
  }
}

TEST(SnapshotStream, FirstTickIsAKeyframeThenDeltasReconstructEachTick)
{
  Net::SnapshotStreamEncoder enc;
  Net::SnapshotStreamDecoder dec;
  uint32_t ack = 0;

  for (uint32_t t = 1; t <= 10; ++t)
  {
    const Net::WorldSnapshot cur = World(t, 1000 + static_cast<int64_t>(t) * 10);
    const std::vector<std::vector<uint8_t>> dgrams = enc.Encode(cur, ack);
    ASSERT_FALSE(dgrams.empty());

    Net::WorldSnapshot recon;
    ASSERT_TRUE(DeliverAll(dec, dgrams, recon));
    EXPECT_TRUE(SameEntities(recon, cur)) << "tick " << t;

    if (t == 1)
      EXPECT_FALSE(enc.LastWasDelta());   // first send with nothing acked = keyframe
    else
      EXPECT_TRUE(enc.LastWasDelta());    // once the ack flows, deltas

    ack = dec.AckTick();
    EXPECT_EQ(ack, t);
  }
}

TEST(SnapshotStream, ADroppedDeltaSelfHealsAgainstTheStillAckedBaseline)
{
  Net::SnapshotStreamEncoder enc;
  Net::SnapshotStreamDecoder dec;

  // Tick 1: keyframe, delivered and acked.
  Net::WorldSnapshot recon;
  ASSERT_TRUE(DeliverAll(dec, enc.Encode(World(1, 1000), 0), recon));
  uint32_t ack = dec.AckTick();
  ASSERT_EQ(ack, 1u);

  // Tick 2: a delta - but it is DROPPED (never delivered). The ack stays at 1.
  (void)enc.Encode(World(2, 1010), ack);
  EXPECT_TRUE(enc.LastWasDelta());
  EXPECT_EQ(dec.AckTick(), 1u);          // client never saw tick 2

  // Tick 3: the server still deltas against the acked baseline (tick 1); the client
  // holds tick 1, so it reconstructs tick 3 correctly despite the lost tick 2.
  const Net::WorldSnapshot cur3 = World(3, 1020);
  ASSERT_TRUE(DeliverAll(dec, enc.Encode(cur3, dec.AckTick()), recon));
  EXPECT_TRUE(SameEntities(recon, cur3));
  EXPECT_EQ(dec.AckTick(), 3u);
}

TEST(SnapshotStream, AKeyframeIsForcedOnTheCadence)
{
  Net::SnapshotStreamEncoder enc;
  Net::SnapshotStreamDecoder dec;
  uint32_t ack = 0;
  int keyframes = 0;

  // Two keyframe intervals' worth of ticks, always acking, so only the cadence (not
  // loss) can force a full: expect exactly two keyframes (the first tick + one more
  // after SNAPSHOT_KEYFRAME_INTERVAL deltas).
  for (uint32_t t = 1; t <= 2 * Net::SNAPSHOT_KEYFRAME_INTERVAL; ++t)
  {
    const std::vector<std::vector<uint8_t>> dgrams = enc.Encode(World(t, 1000 + t), ack);
    if (!enc.LastWasDelta())
      ++keyframes;
    Net::WorldSnapshot recon;
    ASSERT_TRUE(DeliverAll(dec, dgrams, recon));
    ack = dec.AckTick();
  }
  EXPECT_EQ(keyframes, 2);
}

TEST(SnapshotStream, ADeltaWithNoHeldBaselineIsDropped)
{
  // A fresh client that receives a delta before any keyframe cannot apply it.
  Net::DeltaSnapshot d;
  d.tick = 7;
  d.baselineTick = 6;
  Net::EntitySnapshot e; e.id = 1; e.x = 5; e.noseZ = 1.0f; e.roofY = 1.0f;
  d.changed = { e };
  Net::DataWriter w;
  Net::WriteSnapshotDelta(w, d);

  Net::SnapshotStreamDecoder dec;
  Net::WorldSnapshot out;
  EXPECT_FALSE(dec.Decode(w.Bytes().data(), w.Size(), out));
  EXPECT_EQ(dec.AckTick(), 0u);
}

TEST(SnapshotStream, AMultiDatagramFullIsNotAcknowledgedAsABaseline)
{
  // A split (multi-datagram) full snapshot is rendered but is NOT a complete
  // baseline, so the client does not advance its ack off it.
  Net::WorldSnapshot big;
  big.tick = 4;
  for (int i = 0; i < 50; ++i)
  {
    Net::EntitySnapshot e; e.id = static_cast<uint32_t>(i); e.x = i * 10;
    e.noseZ = 1.0f; e.roofY = 1.0f;
    big.entities.push_back(e);
  }
  const std::vector<std::vector<uint8_t>> parts = Net::PacketizeSnapshot(big, 1200);
  ASSERT_GT(parts.size(), 1u);

  Net::SnapshotStreamDecoder dec;
  for (const std::vector<uint8_t>& p : parts)
  {
    Net::WorldSnapshot out;
    EXPECT_TRUE(dec.Decode(p.data(), p.size(), out));   // decodes (renders) each part
  }
  EXPECT_EQ(dec.AckTick(), 0u);   // but none is a complete baseline
}
