#include <gtest/gtest.h>

#include "graphics/LineExpand.h"

using namespace Neuron::Graphics;

// --- Gaussian blur kernel (H4 emissive glow) -------------------------------------

TEST(GaussianKernel, IsNormalisedAndSymmetric)
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

TEST(GaussianKernel, RadiusZeroIsASingleTap)
{
  const std::vector<float> k = GaussianKernel(0);
  ASSERT_EQ(k.size(), 1u);
  EXPECT_NEAR(k[0], 1.0f, 1e-6f);
}

TEST(GaussianKernel, NegativeRadiusIsEmpty)
{
  EXPECT_TRUE(GaussianKernel(-1).empty());
}

TEST(GaussianKernel, MatchesTheBakedRadius4ShaderWeights)
{
  // blurPS.hlsl bakes these literals; keep the core and the shader in lockstep.
  const std::vector<float> k = GaussianKernel(4);
  ASSERT_EQ(k.size(), 9u);
  EXPECT_NEAR(k[4], 0.2042f, 5e-4f);   // w0 (centre)
  EXPECT_NEAR(k[3], 0.1802f, 5e-4f);   // w1
  EXPECT_NEAR(k[2], 0.1238f, 5e-4f);   // w2
  EXPECT_NEAR(k[1], 0.0663f, 5e-4f);   // w3
  EXPECT_NEAR(k[0], 0.0276f, 5e-4f);   // w4
}
