#include <gtest/gtest.h>

#include "TickPacer.h"
#include "TickMetrics.h"

using namespace Neuron::Server;

// --- TickPacer (D3) -----------------------------------------------------------

TEST(TickPacer, AheadOfScheduleRunsNoTicksAndSleepsTheRemainder)
{
  TickPacer p(33.0, 5);
  bool over = false;
  EXPECT_EQ(p.Pump(10.0, over), 0);
  EXPECT_FALSE(over);
  EXPECT_NEAR(p.SleepMs(), 23.0, 1e-9);
}

TEST(TickPacer, AccumulatesFractionalTimeAcrossPumps)
{
  TickPacer p(33.0, 5);
  bool over = false;
  EXPECT_EQ(p.Pump(20.0, over), 0);   // 20 < 33 -> no step yet
  EXPECT_EQ(p.Pump(20.0, over), 1);   // 40 >= 33 -> one step, 7 left over
  EXPECT_FALSE(over);
  EXPECT_NEAR(p.SleepMs(), 26.0, 1e-9);
}

TEST(TickPacer, CatchesUpButStaysUnderTheCap)
{
  TickPacer p(33.0, 5);
  bool over = false;
  EXPECT_EQ(p.Pump(100.0, over), 3);   // 100 / 33 = 3 whole steps, 1 ms left
  EXPECT_FALSE(over);
}

TEST(TickPacer, OverrunIsBoundedAndDropsTheBacklog)
{
  TickPacer p(33.0, 5);
  bool over = false;
  EXPECT_EQ(p.Pump(1000.0, over), 5);   // would be ~30 steps; capped at 5
  EXPECT_TRUE(over);                    // still behind after the cap
  EXPECT_NEAR(p.SleepMs(), 33.0, 1e-9); // backlog dropped -> a fresh step is due
}

// --- TickMetrics (D3) ---------------------------------------------------------

TEST(TickMetrics, SummarizesTheWindow)
{
  TickMetrics m;
  m.Record(TickSample{ 10.0, 100, 2, 50, 1000 });
  m.Record(TickSample{ 20.0, 110, 2, 70, 2000 });
  m.NoteOverrun();

  const TickSummary s = m.Snapshot(/*windowSeconds*/ 2.0);
  EXPECT_EQ(s.ticks, 2u);
  EXPECT_EQ(s.overruns, 1u);
  EXPECT_NEAR(s.avgMs, 15.0, 1e-9);
  EXPECT_NEAR(s.maxMs, 20.0, 1e-9);
  EXPECT_EQ(s.entities, 110u);            // level = the latest sample
  EXPECT_EQ(s.sessions, 2u);
  EXPECT_EQ(s.avgCandidatePairs, 60u);    // (50 + 70) / 2
  EXPECT_EQ(s.bytesPerSecond, 1500u);     // (1000 + 2000) / 2 s
}

TEST(TickMetrics, ResetClearsSumsButKeepsLevels)
{
  TickMetrics m;
  m.Record(TickSample{ 10.0, 100, 3, 0, 0 });
  m.Reset();

  EXPECT_EQ(m.WindowTicks(), 0u);
  const TickSummary s = m.Snapshot(1.0);
  EXPECT_EQ(s.ticks, 0u);
  EXPECT_EQ(s.avgMs, 0.0);
  EXPECT_EQ(s.entities, 100u);            // entity/session levels carry forward
}
