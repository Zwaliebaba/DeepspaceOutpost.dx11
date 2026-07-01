#include <gtest/gtest.h>

#include <string>
#include <vector>

#include "RenderQueue.h"

using namespace Neuron::Render;

namespace
{
  // A RenderSink that records the calls the DrawModel seam tests care about, in
  // replay order. NullRenderSink is `final`, so the capturing sink implements the
  // whole contract directly; only the methods the tests assert on do real work.
  class CapturingSink final : public RenderSink
  {
  public:
    std::vector<ModelDraw> models;
    std::vector<std::string> order; // coarse call log for interleave checks

    void Pixel(int, int, int) override { order.emplace_back("Pixel"); }
    void Line(int, int, int, int) override { order.emplace_back("Line"); }
    void ColourLine(int, int, int, int, int) override {}
    void Triangle(int, int, int, int, int, int, int) override {}
    void RenderLine(int, int, int, int, int, int) override {}
    void DrawModel(const ModelDraw& _m) override
    {
      models.push_back(_m);
      order.emplace_back("DrawModel");
    }
    void StartRender() override {}
    void FinishRender() override {}
  };

  ModelDraw MakeSampleModel(int _type)
  {
    ModelDraw m;
    m.type = _type;
    m.style = 1;
    m.colour = 7;
    m.flags = 0x40u;
    m.location[0] = 10.5;
    m.location[1] = -20.25;
    m.location[2] = 300.0;
    // A recognisable (non-symmetric) basis so a transpose/order bug would show.
    m.rotmat[0][0] = 1.0; m.rotmat[0][1] = 2.0; m.rotmat[0][2] = 3.0;
    m.rotmat[1][0] = 4.0; m.rotmat[1][1] = 5.0; m.rotmat[1][2] = 6.0;
    m.rotmat[2][0] = 7.0; m.rotmat[2][1] = 8.0; m.rotmat[2][2] = 9.0;
    m.distance = 123.5;
    return m;
  }

  void ExpectModelEq(const ModelDraw& _a, const ModelDraw& _b)
  {
    EXPECT_EQ(_a.type, _b.type);
    EXPECT_EQ(_a.style, _b.style);
    EXPECT_EQ(_a.colour, _b.colour);
    EXPECT_EQ(_a.flags, _b.flags);
    EXPECT_DOUBLE_EQ(_a.distance, _b.distance);
    for (int i = 0; i < 3; ++i)
    {
      EXPECT_DOUBLE_EQ(_a.location[i], _b.location[i]);
      for (int j = 0; j < 3; ++j)
        EXPECT_DOUBLE_EQ(_a.rotmat[i][j], _b.rotmat[i][j]);
    }
  }
}

TEST(RenderQueueDrawModel, RecordsAndReplaysOneModelFaithfully)
{
  RenderQueue q;
  const ModelDraw in = MakeSampleModel(11);
  q.DrawModel(in);

  CapturingSink sink;
  q.Replay(sink);

  ASSERT_EQ(sink.models.size(), 1u);
  ExpectModelEq(sink.models[0], in);
}

TEST(RenderQueueDrawModel, ReplaysMultipleModelsInRecordOrder)
{
  RenderQueue q;
  q.DrawModel(MakeSampleModel(2));
  q.DrawModel(MakeSampleModel(16));
  q.DrawModel(MakeSampleModel(29));

  CapturingSink sink;
  q.Replay(sink);

  ASSERT_EQ(sink.models.size(), 3u);
  EXPECT_EQ(sink.models[0].type, 2);
  EXPECT_EQ(sink.models[1].type, 16);
  EXPECT_EQ(sink.models[2].type, 29);
}

TEST(RenderQueueDrawModel, PreservesInterleavingWithOtherCommands)
{
  RenderQueue q;
  q.Line(0, 0, 1, 1);
  q.DrawModel(MakeSampleModel(12));
  q.Pixel(5, 5, 3);

  CapturingSink sink;
  q.Replay(sink);

  ASSERT_EQ(sink.order.size(), 3u);
  EXPECT_EQ(sink.order[0], "Line");
  EXPECT_EQ(sink.order[1], "DrawModel");
  EXPECT_EQ(sink.order[2], "Pixel");
  ASSERT_EQ(sink.models.size(), 1u);
  EXPECT_EQ(sink.models[0].type, 12);
}

TEST(RenderQueueDrawModel, ClearResetsTheModelArena)
{
  RenderQueue q;
  q.DrawModel(MakeSampleModel(2));
  q.DrawModel(MakeSampleModel(16));
  q.Clear();
  EXPECT_TRUE(q.Empty());

  const ModelDraw in = MakeSampleModel(31);
  q.DrawModel(in);

  CapturingSink sink;
  q.Replay(sink);

  // Exactly the post-Clear model replays (the arena index reset to 0, not 2).
  ASSERT_EQ(sink.models.size(), 1u);
  ExpectModelEq(sink.models[0], in);
}

TEST(RenderQueueDrawModel, NullSinkIgnoresDrawModel)
{
  RenderQueue q;
  q.DrawModel(MakeSampleModel(11));
  EXPECT_EQ(q.Size(), 1u);

  NullRenderSink sink;
  q.Replay(sink); // must not crash; headless sim records but draws nothing
  SUCCEED();
}
