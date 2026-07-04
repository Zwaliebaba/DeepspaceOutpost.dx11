#include <gtest/gtest.h>

#include "Messages/Defs/TimeSync.h"   // Ping / Pong
#include "Messages/Serialize.h"       // Encode / Decode
#include "LatencyEstimate.h"          // Net::LatencyEstimate

using namespace Neuron;

// --- wire round-trips ---------------------------------------------------------

TEST(TimeSync, PingRoundTrips)
{
  Msg::Ping in;
  in.clientTimeMs = 0xDEADBEEF;
  in.rttMs = 84;

  Msg::Ping out;
  ASSERT_TRUE(Msg::Decode(Msg::Encode(in), out));
  EXPECT_TRUE(in.Fields() == out.Fields());
  EXPECT_EQ(out.clientTimeMs, 0xDEADBEEFu);
  EXPECT_EQ(out.rttMs, 84u);
}

TEST(TimeSync, PongRoundTrips)
{
  Msg::Pong in;
  in.clientTimeMs = 123456;
  in.serverTick = 999;

  Msg::Pong out;
  ASSERT_TRUE(Msg::Decode(Msg::Encode(in), out));
  EXPECT_TRUE(in.Fields() == out.Fields());
}

TEST(TimeSync, PingAndPongAreControlLaneAndDirected)
{
  // The handshake rides the reliable Control lane, one way each.
  EXPECT_TRUE(Msg::Ping::Lane == Msg::MessageLane::Control);
  EXPECT_TRUE(Msg::Pong::Lane == Msg::MessageLane::Control);
  EXPECT_TRUE(Msg::Ping::Dir == Msg::Direction::ClientToServer);
  EXPECT_TRUE(Msg::Pong::Dir == Msg::Direction::ServerToClient);
}

// --- the smoothed RTT estimator (pure arithmetic) -----------------------------

TEST(LatencyEstimate, StartsInvalidUntilAFirstSample)
{
  Net::LatencyEstimate e;
  EXPECT_FALSE(e.valid);
  EXPECT_EQ(e.rttMs, 0.0);
}

TEST(LatencyEstimate, FirstSampleSeedsTheEstimateExactly)
{
  Net::LatencyEstimate e;
  ASSERT_TRUE(e.AddRttSample(100.0));
  EXPECT_TRUE(e.valid);
  EXPECT_DOUBLE_EQ(e.rttMs, 100.0);   // the first accepted sample is taken as-is
}

TEST(LatencyEstimate, LaterSamplesBlendTowardTheNewValue)
{
  Net::LatencyEstimate e;
  e.AddRttSample(100.0);
  e.AddRttSample(200.0);
  // EWMA: 100 + 0.25 * (200 - 100) = 125.
  EXPECT_DOUBLE_EQ(e.rttMs, 100.0 + Net::LatencyEstimate::RTT_BLEND * 100.0);

  // A steady stream at the new value converges toward it monotonically.
  const double afterOne = e.rttMs;
  e.AddRttSample(200.0);
  EXPECT_GT(e.rttMs, afterOne);
  EXPECT_LT(e.rttMs, 200.0);
}

TEST(LatencyEstimate, NonPositiveAndSpikeSamplesAreRejectedWithoutDisturbingTheEstimate)
{
  Net::LatencyEstimate e;
  e.AddRttSample(120.0);
  const double kept = e.rttMs;

  EXPECT_FALSE(e.AddRttSample(0.0));                                            // non-positive
  EXPECT_FALSE(e.AddRttSample(-5.0));                                           // negative
  EXPECT_FALSE(e.AddRttSample(Net::LatencyEstimate::MAX_SANE_RTT_MS + 1.0));    // spike (late resend)
  EXPECT_DOUBLE_EQ(e.rttMs, kept);   // untouched by every rejected sample
  EXPECT_TRUE(e.valid);
}
