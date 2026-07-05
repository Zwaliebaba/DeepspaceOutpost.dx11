#include <gtest/gtest.h>

#include "input/GestureRecognizer.h"

using namespace Neuron::Input;

namespace
{
  PointerSample Down(uint32_t _id, float _x, float _y, uint32_t _t)
  {
    return PointerSample{_id, _x, _y, _t, PointerPhase::Down};
  }
  PointerSample Move(uint32_t _id, float _x, float _y, uint32_t _t)
  {
    return PointerSample{_id, _x, _y, _t, PointerPhase::Move};
  }
  PointerSample Up(uint32_t _id, float _x, float _y, uint32_t _t)
  {
    return PointerSample{_id, _x, _y, _t, PointerPhase::Up};
  }

  // Count events of a given type in a batch.
  int Count(const std::vector<GestureEvent>& _v, GestureType _t)
  {
    int n = 0;
    for (const auto& e : _v) if (e.type == _t) ++n;
    return n;
  }

  bool Has(const std::vector<GestureEvent>& _v, GestureType _t)
  {
    return Count(_v, _t) > 0;
  }
}

TEST(Gesture, QuickPressReleaseIsATap)
{
  GestureRecognizer g;
  auto d = g.Push(Down(1, 100, 100, 0));
  EXPECT_TRUE(d.empty());
  auto u = g.Push(Up(1, 101, 101, 50));   // within slop, quick
  ASSERT_EQ(u.size(), 1u);
  EXPECT_EQ(u[0].type, GestureType::Tap);
  EXPECT_FLOAT_EQ(u[0].x, 101);
  EXPECT_EQ(g.ActivePointers(), 0);
}

TEST(Gesture, MovementBeyondSlopIsADragNotATap)
{
  GestureRecognizer g;
  g.Push(Down(1, 100, 100, 0));
  auto m1 = g.Push(Move(1, 103, 100, 10));  // still within 6px slop
  EXPECT_TRUE(m1.empty());
  auto m2 = g.Push(Move(1, 110, 100, 20));  // now past slop
  ASSERT_EQ(m2.size(), 1u);
  EXPECT_EQ(m2[0].type, GestureType::DragBegin);
  auto m3 = g.Push(Move(1, 120, 105, 30));
  ASSERT_EQ(m3.size(), 1u);
  EXPECT_EQ(m3[0].type, GestureType::DragMove);
  EXPECT_FLOAT_EQ(m3[0].dx, 10);
  EXPECT_FLOAT_EQ(m3[0].dy, 5);
  auto u = g.Push(Up(1, 120, 105, 40));
  ASSERT_EQ(u.size(), 1u);
  EXPECT_EQ(u[0].type, GestureType::DragEnd);
  EXPECT_FALSE(Has(u, GestureType::Tap));
}

TEST(Gesture, HoldWithoutMovementFiresLongPressOnceAndSuppressesTap)
{
  GestureRecognizer g;
  g.Push(Down(1, 200, 200, 0));
  EXPECT_TRUE(g.Tick(100).empty());          // before threshold
  auto lp = g.Tick(360);                      // past 350ms
  ASSERT_EQ(lp.size(), 1u);
  EXPECT_EQ(lp[0].type, GestureType::LongPress);
  EXPECT_TRUE(g.Tick(500).empty());          // fires only once
  auto u = g.Push(Up(1, 200, 200, 600));
  EXPECT_FALSE(Has(u, GestureType::Tap));    // long-press consumed the press
}

TEST(Gesture, MovingCancelsPendingLongPress)
{
  GestureRecognizer g;
  g.Push(Down(1, 0, 0, 0));
  g.Push(Move(1, 40, 0, 10));                 // drag
  auto lp = g.Tick(400);
  EXPECT_FALSE(Has(lp, GestureType::LongPress));
}

TEST(Gesture, SecondTapWithinWindowIsDoubleTap)
{
  GestureRecognizer g;
  g.Push(Down(1, 50, 50, 0));
  auto t1 = g.Push(Up(1, 50, 50, 40));
  ASSERT_EQ(t1[0].type, GestureType::Tap);
  g.Push(Down(1, 52, 51, 120));
  auto t2 = g.Push(Up(1, 52, 51, 150));       // within 300ms, near
  ASSERT_EQ(t2.size(), 1u);
  EXPECT_EQ(t2[0].type, GestureType::DoubleTap);
}

TEST(Gesture, SecondTapTooLateIsPlainTap)
{
  GestureRecognizer g;
  g.Push(Down(1, 50, 50, 0));
  g.Push(Up(1, 50, 50, 40));
  g.Push(Down(1, 50, 50, 500));
  auto t2 = g.Push(Up(1, 50, 50, 540));       // gap 500 > 300ms
  ASSERT_EQ(t2.size(), 1u);
  EXPECT_EQ(t2[0].type, GestureType::Tap);
}

TEST(Gesture, SecondTapTooFarIsPlainTap)
{
  GestureRecognizer g;
  g.Push(Down(1, 50, 50, 0));
  g.Push(Up(1, 50, 50, 40));
  g.Push(Down(1, 200, 200, 120));
  auto t2 = g.Push(Up(1, 200, 200, 150));     // far from first tap
  ASSERT_EQ(t2.size(), 1u);
  EXPECT_EQ(t2[0].type, GestureType::Tap);
}

TEST(Gesture, TwoFingersApartIsPinchZoomIn)
{
  GestureRecognizer g;
  g.Push(Down(1, 100, 100, 0));
  auto begin = g.Push(Down(2, 140, 100, 5));  // 40px apart
  EXPECT_TRUE(Has(begin, GestureType::PanBegin));
  EXPECT_EQ(g.ActivePointers(), 2);
  // Move finger 2 out to 180 -> distance 40 -> 80, +40px = +1 zoom step.
  auto p = g.Push(Move(2, 180, 100, 15));
  ASSERT_TRUE(Has(p, GestureType::Pinch));
  for (const auto& e : p)
  {
    if (e.type == GestureType::Pinch) { EXPECT_NEAR(e.value, 1.0f, 1e-4f); }
  }
}

TEST(Gesture, TwoFingerCommonMotionIsPan)
{
  GestureRecognizer g;
  g.Push(Down(1, 100, 100, 0));
  g.Push(Down(2, 140, 100, 5));
  // Slide both fingers right by 20 with distance unchanged -> pan, no pinch.
  auto p = g.Push(Move(1, 120, 100, 15));
  // Only one finger moved yet: centroid moved +10, distance shrank -> both.
  EXPECT_TRUE(Has(p, GestureType::PanMove));
  auto p2 = g.Push(Move(2, 160, 100, 20));    // now both shifted +20 total
  EXPECT_TRUE(Has(p2, GestureType::PanMove));
}

TEST(Gesture, SecondFingerMidDragEndsDragAndStartsTwoFinger)
{
  GestureRecognizer g;
  g.Push(Down(1, 0, 0, 0));
  g.Push(Move(1, 30, 0, 10));                 // dragging
  auto second = g.Push(Down(2, 60, 0, 20));
  EXPECT_TRUE(Has(second, GestureType::DragEnd));
  EXPECT_TRUE(Has(second, GestureType::PanBegin));
  EXPECT_EQ(g.ActivePointers(), 2);
}

TEST(Gesture, LiftingOneOfTwoSuppressesTrailingTap)
{
  GestureRecognizer g;
  g.Push(Down(1, 100, 100, 0));
  g.Push(Down(2, 140, 100, 5));
  auto up1 = g.Push(Up(1, 100, 100, 30));
  EXPECT_TRUE(Has(up1, GestureType::PanEnd));
  // The still-down second finger must NOT tap when it lifts.
  auto up2 = g.Push(Up(2, 140, 100, 60));
  EXPECT_FALSE(Has(up2, GestureType::Tap));
  EXPECT_EQ(g.ActivePointers(), 0);
}

TEST(Gesture, CancelClearsStateCleanly)
{
  GestureRecognizer g;
  g.Push(Down(1, 0, 0, 0));
  g.Push(Move(1, 40, 0, 10));                 // dragging
  auto c = g.Push(PointerSample{1, 40, 0, 20, PointerPhase::Cancel});
  EXPECT_TRUE(Has(c, GestureType::DragEnd));
  EXPECT_EQ(g.ActivePointers(), 0);
  // A fresh tap works after cancel.
  g.Push(Down(1, 5, 5, 100));
  auto t = g.Push(Up(1, 5, 5, 130));
  EXPECT_TRUE(Has(t, GestureType::Tap));
}

TEST(Gesture, MousePointerIdSharesThePath)
{
  GestureRecognizer g;
  g.Push(Down(MOUSE_POINTER_ID, 10, 10, 0));
  auto t = g.Push(Up(MOUSE_POINTER_ID, 10, 10, 20));
  ASSERT_EQ(t.size(), 1u);
  EXPECT_EQ(t[0].type, GestureType::Tap);
}
