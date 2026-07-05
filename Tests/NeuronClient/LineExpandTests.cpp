#include <gtest/gtest.h>

#include <cmath>

#include "graphics/LineExpand.h"

using namespace Neuron::Graphics;

namespace
{
  // Convert a clip-space corner to pixel coordinates for assertions.
  void ToPixels(const Clip4& _c, float _vpW, float _vpH, float& _px, float& _py)
  {
    const float ndcx = _c.x / _c.w, ndcy = _c.y / _c.w;
    _px = (ndcx * 0.5f + 0.5f) * _vpW;
    _py = (0.5f - ndcy * 0.5f) * _vpH;
  }
}

TEST(LineExpand, HorizontalSegmentOffsetsVerticallyByTheLineWidth)
{
  // A horizontal screen line (y constant): its two corners at one end should be
  // separated vertically by ~2*halfWidth pixels.
  const float vpW = 800.0f, vpH = 600.0f, half = 4.0f;
  const Clip4 a{ -0.5f, 0.0f, 0.5f, 1.0f };
  const Clip4 b{  0.5f, 0.0f, 0.5f, 1.0f };

  const Clip4 c0 = ExpandLineCorner(a, b, 0, half, vpW, vpH);   // start, -side
  const Clip4 c1 = ExpandLineCorner(a, b, 1, half, vpW, vpH);   // start, +side

  float px0, py0, px1, py1;
  ToPixels(c0, vpW, vpH, px0, py0);
  ToPixels(c1, vpW, vpH, px1, py1);
  EXPECT_NEAR(px0, px1, 0.05f);                         // same x (offset is vertical)
  EXPECT_NEAR(std::fabs(py1 - py0), 2.0f * half, 0.1f); // separated by the line weight
}

TEST(LineExpand, VerticalSegmentOffsetsHorizontally)
{
  const float vpW = 800.0f, vpH = 600.0f, half = 3.0f;
  const Clip4 a{ 0.0f, -0.5f, 0.5f, 1.0f };
  const Clip4 b{ 0.0f,  0.5f, 0.5f, 1.0f };

  const Clip4 c0 = ExpandLineCorner(a, b, 0, half, vpW, vpH);
  const Clip4 c1 = ExpandLineCorner(a, b, 1, half, vpW, vpH);

  float px0, py0, px1, py1;
  ToPixels(c0, vpW, vpH, px0, py0);
  ToPixels(c1, vpW, vpH, px1, py1);
  EXPECT_NEAR(py0, py1, 0.05f);                          // same y (offset is horizontal)
  EXPECT_NEAR(std::fabs(px1 - px0), 2.0f * half, 0.1f);
}

TEST(LineExpand, BothEndsAreCovered)
{
  const float vpW = 640.0f, vpH = 480.0f, half = 2.0f;
  const Clip4 a{ -0.3f, -0.1f, 0.4f, 1.0f };
  const Clip4 b{  0.4f,  0.2f, 0.4f, 1.0f };
  // Corners 0/1 sit at end a, 2/3 at end b.
  float ax, ay, bx, by, cx, cy, dx, dy;
  ToPixels(ExpandLineCorner(a, b, 0, half, vpW, vpH), vpW, vpH, ax, ay);
  ToPixels(ExpandLineCorner(a, b, 1, half, vpW, vpH), vpW, vpH, bx, by);
  ToPixels(ExpandLineCorner(a, b, 2, half, vpW, vpH), vpW, vpH, cx, cy);
  ToPixels(ExpandLineCorner(a, b, 3, half, vpW, vpH), vpW, vpH, dx, dy);
  // The 0/1 midpoint is near end a; the 2/3 midpoint near end b.
  float apx, apy, bpx, bpy;
  ToPixels(a, vpW, vpH, apx, apy);
  ToPixels(b, vpW, vpH, bpx, bpy);
  EXPECT_NEAR((ax + bx) * 0.5f, apx, 0.5f);
  EXPECT_NEAR((cx + dx) * 0.5f, bpx, 0.5f);
}

TEST(LineExpand, DegenerateSegmentDoesNotCrashAndStaysNearBase)
{
  const float vpW = 800.0f, vpH = 600.0f, half = 5.0f;
  const Clip4 a{ 0.1f, 0.1f, 0.5f, 1.0f };
  const Clip4 c = ExpandLineCorner(a, a, 1, half, vpW, vpH);   // zero-length
  float px, py, apx, apy;
  ToPixels(c, vpW, vpH, px, py);
  ToPixels(a, vpW, vpH, apx, apy);
  // Arbitrary but bounded: the corner sits exactly `half` pixels from the base.
  const float d = std::sqrt((px - apx) * (px - apx) + (py - apy) * (py - apy));
  EXPECT_NEAR(d, half, 0.2f);
}

TEST(LineExpand, BehindEyeIsPassedThrough)
{
  const Clip4 a{ 0.1f, 0.1f, 0.5f, -1.0f };   // w <= 0: behind the near plane
  const Clip4 b{ 0.2f, 0.2f, 0.5f,  1.0f };
  const Clip4 c = ExpandLineCorner(a, b, 0, 4.0f, 800.0f, 600.0f);
  EXPECT_FLOAT_EQ(c.x, a.x);   // returned unexpanded
  EXPECT_FLOAT_EQ(c.w, a.w);
}

// --- Gaussian blur kernel --------------------------------------------------------

TEST(LineExpand, GaussianKernelIsNormalisedAndSymmetric)
{
  const std::vector<float> k = GaussianKernel(4);
  ASSERT_EQ(k.size(), 9u);   // 2*radius+1
  float sum = 0.0f;
  for (float v : k) sum += v;
  EXPECT_NEAR(sum, 1.0f, 1e-5f);
  for (int i = 0; i < 4; ++i)
    EXPECT_NEAR(k[i], k[8 - i], 1e-6f);   // symmetric
  EXPECT_GT(k[4], k[0]);                  // peak at the centre
}

TEST(LineExpand, GaussianKernelRadiusZeroIsASingleTap)
{
  const std::vector<float> k = GaussianKernel(0);
  ASSERT_EQ(k.size(), 1u);
  EXPECT_NEAR(k[0], 1.0f, 1e-6f);
}
