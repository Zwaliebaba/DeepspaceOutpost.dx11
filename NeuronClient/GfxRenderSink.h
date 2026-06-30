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
  void FastPixel(int _x, int _y, int _colour) override;
  void Line(int _x0, int _y0, int _x1, int _y1) override;
  void ColourLine(int _x0, int _y0, int _x1, int _y1, int _colour) override;
  void Circle(int _cx, int _cy, int _radius, int _colour) override;
  void FilledCircle(int _cx, int _cy, int _radius, int _colour) override;
  void Triangle(int _x0, int _y0, int _x1, int _y1, int _x2, int _y2, int _colour) override;
  void RenderPolygon(int _numPoints, const int* _points, int _colour, int _dist) override;
  void RenderLine(int _x0, int _y0, int _x1, int _y1, int _dist, int _colour) override;
  void DrawModel(const Neuron::Render::ModelDraw& _model) override;
  void Sprite(int _spriteId, int _x, int _y) override;
  void Text(int _x, int _y, const char* _text) override;
  void CentreText(int _y, const char* _text, int _pointSize, int _colour) override;
  void SetClipRegion(int _tx, int _ty, int _bx, int _by) override;
  void ClearArea(int _tx, int _ty, int _bx, int _by) override;
  void DrawScanner() override;
  void StartRender() override;
  void FinishRender() override;
};
