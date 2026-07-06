#include <gtest/gtest.h>

#include "input/MovePlan.h"

using namespace Neuron::Input;

TEST(MovePlan, ActivateNeedsAnOwnUnit)
{
  // No own unit selected -> M / RMB-on-empty does nothing.
  GridTransition t = StepGrid(GridMode::Off, GridEvent::Activate, /*hasOwnUnit*/ false);
  EXPECT_EQ(t.next, GridMode::Off);
  EXPECT_FALSE(t.confirm);
  EXPECT_FALSE(t.cancel);

  // With an own unit -> the grid spawns in PlacingXZ.
  t = StepGrid(GridMode::Off, GridEvent::Activate, true);
  EXPECT_EQ(t.next, GridMode::PlacingXZ);
}

TEST(MovePlan, OffSwallowsEverythingButActivate)
{
  for (GridEvent ev : {GridEvent::ShiftDown, GridEvent::ShiftUp, GridEvent::Confirm, GridEvent::Cancel})
  {
    GridTransition t = StepGrid(GridMode::Off, ev, true);
    EXPECT_EQ(t.next, GridMode::Off);
    EXPECT_FALSE(t.confirm);
    EXPECT_FALSE(t.cancel);
  }
}

TEST(MovePlan, ShiftEntersAndLeavesElevation)
{
  GridTransition down = StepGrid(GridMode::PlacingXZ, GridEvent::ShiftDown, true);
  EXPECT_EQ(down.next, GridMode::AdjustingY);

  // Releasing Shift returns to XZ placement WITH the elevation kept (no cancel).
  GridTransition up = StepGrid(GridMode::AdjustingY, GridEvent::ShiftUp, true);
  EXPECT_EQ(up.next, GridMode::PlacingXZ);
  EXPECT_FALSE(up.cancel);
  EXPECT_FALSE(up.confirm);
}

TEST(MovePlan, ConfirmFromEitherPlacementSendsTheMove)
{
  GridTransition a = StepGrid(GridMode::PlacingXZ, GridEvent::Confirm, true);
  EXPECT_EQ(a.next, GridMode::Off);
  EXPECT_TRUE(a.confirm);

  GridTransition b = StepGrid(GridMode::AdjustingY, GridEvent::Confirm, true);
  EXPECT_EQ(b.next, GridMode::Off);
  EXPECT_TRUE(b.confirm);
}

TEST(MovePlan, CancelFromEitherPlacementDropsTheGizmo)
{
  GridTransition a = StepGrid(GridMode::PlacingXZ, GridEvent::Cancel, true);
  EXPECT_EQ(a.next, GridMode::Off);
  EXPECT_TRUE(a.cancel);
  EXPECT_FALSE(a.confirm);

  GridTransition b = StepGrid(GridMode::AdjustingY, GridEvent::Cancel, true);
  EXPECT_EQ(b.next, GridMode::Off);
  EXPECT_TRUE(b.cancel);
}

TEST(MovePlan, RedundantModifierEventsAreNoOps)
{
  // ShiftUp while placing, ShiftDown while already adjusting: stay put, no side effects.
  GridTransition a = StepGrid(GridMode::PlacingXZ, GridEvent::ShiftUp, true);
  EXPECT_EQ(a.next, GridMode::PlacingXZ);
  EXPECT_FALSE(a.confirm);
  EXPECT_FALSE(a.cancel);

  GridTransition b = StepGrid(GridMode::AdjustingY, GridEvent::ShiftDown, true);
  EXPECT_EQ(b.next, GridMode::AdjustingY);
  EXPECT_FALSE(b.confirm);
  EXPECT_FALSE(b.cancel);
}

TEST(MovePlan, GridActiveHelper)
{
  EXPECT_FALSE(GridActive(GridMode::Off));
  EXPECT_TRUE(GridActive(GridMode::PlacingXZ));
  EXPECT_TRUE(GridActive(GridMode::AdjustingY));
}

TEST(MovePlan, FullTwoStepMoveThenConfirm)
{
  // A representative Homeworld move: activate -> place XZ -> Shift to set Y ->
  // release Shift (keep Y) -> confirm.
  GridMode m = GridMode::Off;
  m = StepGrid(m, GridEvent::Activate, true).next;
  EXPECT_EQ(m, GridMode::PlacingXZ);
  m = StepGrid(m, GridEvent::ShiftDown, true).next;
  EXPECT_EQ(m, GridMode::AdjustingY);
  m = StepGrid(m, GridEvent::ShiftUp, true).next;
  EXPECT_EQ(m, GridMode::PlacingXZ);
  GridTransition done = StepGrid(m, GridEvent::Confirm, true);
  EXPECT_EQ(done.next, GridMode::Off);
  EXPECT_TRUE(done.confirm);
}
