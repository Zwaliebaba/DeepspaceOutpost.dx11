#include <gtest/gtest.h>

#include <vector>

#include "Messages/MessageBus.h"
#include "MessageTestMessages.h"

using namespace Neuron::Msg;
using namespace MsgTest;

TEST(MessageBus, DeliversToAllSubscribersInFifoOrder)
{
  MessageBus bus;
  std::vector<int> a, b;
  bus.Subscribe<TickEv>([&](const TickEv& _e) { a.push_back(_e.n); });
  bus.Subscribe<TickEv>([&](const TickEv& _e) { b.push_back(_e.n); });

  bus.Publish(TickEv{ 1 });
  bus.Publish(TickEv{ 2 });
  bus.Publish(TickEv{ 3 });
  bus.Dispatch();

  EXPECT_EQ(a, (std::vector<int>{ 1, 2, 3 }));
  EXPECT_EQ(b, (std::vector<int>{ 1, 2, 3 }));
}

TEST(MessageBus, HandlersRunOnlyDuringDispatch)
{
  MessageBus bus;
  int count = 0;
  bus.Subscribe<TickEv>([&](const TickEv&) { ++count; });
  bus.Publish(TickEv{ 1 });
  EXPECT_EQ(count, 0);            // nothing fires until Dispatch
  bus.Dispatch();
  EXPECT_EQ(count, 1);
}

TEST(MessageBus, RepublishFromHandlerResolvesSameDispatchNextGeneration)
{
  MessageBus bus;
  bool followHandled = false;
  bus.Subscribe<TickEv>([&](const TickEv& _e) { if (_e.n == 1) bus.Publish(FollowUp{ 100 }); });
  bus.Subscribe<FollowUp>([&](const FollowUp& _f) { followHandled = (_f.n == 100); });

  bus.Publish(TickEv{ 1 });
  bus.Dispatch();

  EXPECT_TRUE(followHandled);                 // chained within one Dispatch
  EXPECT_GE(bus.Stats().generations, 2u);     // base gen + the follow-up gen
  EXPECT_EQ(bus.PendingCount(), 0u);
}

TEST(MessageBus, UnsubscribeStopsDelivery)
{
  MessageBus bus;
  int count = 0;
  const SubscriptionToken tok = bus.Subscribe<TickEv>([&](const TickEv&) { ++count; });

  bus.Publish(TickEv{ 1 });
  bus.Dispatch();
  EXPECT_EQ(count, 1);

  bus.Unsubscribe(tok);
  bus.Publish(TickEv{ 2 });
  bus.Dispatch();
  EXPECT_EQ(count, 1);            // no further delivery
}

TEST(MessageBus, UnsubscribeDuringDispatchIsSafe)
{
  MessageBus bus;
  int kept = 0;
  SubscriptionToken self = INVALID_SUBSCRIPTION;
  // A one-shot handler that removes itself the first time it runs.
  self = bus.Subscribe<TickEv>([&](const TickEv&) { bus.Unsubscribe(self); });
  bus.Subscribe<TickEv>([&](const TickEv&) { ++kept; });

  bus.Publish(TickEv{ 1 });
  bus.Publish(TickEv{ 2 });
  bus.Dispatch();                // must not crash mid-dispatch
  EXPECT_EQ(kept, 2);
}

TEST(MessageBus, OverflowDropsOldestNonControl)
{
  MessageBus bus;
  for (std::size_t i = 0; i < MAX_QUEUE_DEPTH + 10; ++i)
    bus.Publish(TickEv{ static_cast<int>(i) });

  EXPECT_EQ(bus.PendingCount(), MAX_QUEUE_DEPTH);
  EXPECT_GE(bus.Stats().dropped, 10u);
}

TEST(MessageBus, ControlMessagesAreNeverDropped)
{
  MessageBus bus;
  for (std::size_t i = 0; i < MAX_QUEUE_DEPTH + 5; ++i)
    bus.Publish(CtrlMsg{ static_cast<uint32_t>(i) });

  EXPECT_EQ(bus.Stats().dropped, 0u);
  EXPECT_EQ(bus.PendingCount(), MAX_QUEUE_DEPTH + 5);
}

TEST(MessageBus, SelfFeedingLoopIsBoundedByGenerationCap)
{
  MessageBus bus;
  // A handler that always re-publishes would loop forever without the cap.
  bus.Subscribe<TickEv>([&](const TickEv& _e) { bus.Publish(TickEv{ _e.n + 1 }); });
  bus.Publish(TickEv{ 0 });
  bus.Dispatch();                // terminates
  EXPECT_LE(bus.Stats().generations, MAX_GENERATIONS + 1);
  EXPECT_GT(bus.Stats().dropped, 0u);
}
