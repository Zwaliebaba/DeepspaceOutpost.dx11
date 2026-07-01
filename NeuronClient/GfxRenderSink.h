#pragma once

// GfxRenderSink - the production RenderSink that replays recorded draw commands
// into the existing gfx.h backend (Direct3D 11). It is the concrete strategy
// behind the A1 render seam: the simulation records into a RenderQueue, and the
// platform layer replays it through this sink, reproducing the original
// gfx_*-call sequence pixel-for-pixel.
//
// This is a polymorphic implementation of the RenderSink contract (it enables
// the null/headless sink and decouples the sim from the renderer), not a
// gratuitous wrapper - consistent with the Native-First rule.

#include "RenderQueue.h"

class GfxRenderSink final : public Neuron::Render::RenderSink
{
public:
  void Pixel(int _x, int _y, int _colour) override;
  void Line(int _x0, int _y0, int _x1, int _y1) override;
  void ColourLine(int _x0, int _y0, int _x1, int _y1, int _colour) override;
  void Triangle(int _x0, int _y0, int _x1, int _y1, int _x2, int _y2, int _colour) override;
  void RenderLine(int _x0, int _y0, int _x1, int _y1, int _dist, int _colour) override;
  void DrawModel(const Neuron::Render::ModelDraw& _model) override;
  void StartRender() override;
  void FinishRender() override;
};
