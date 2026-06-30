#include "pch.h"

#include "GfxRenderSink.h"

#include "gfx.h"
#include "gfx2d.h" // gfx2d_submit_model

// Each method forwards to the matching gfx.h primitive so a replayed queue
// reproduces the original draw sequence exactly.

void GfxRenderSink::Pixel(int _x, int _y, int _colour)
{
  gfx_plot_pixel(_x, _y, _colour);
}

void GfxRenderSink::FastPixel(int _x, int _y, int _colour)
{
  gfx_fast_plot_pixel(_x, _y, _colour);
}

void GfxRenderSink::Line(int _x0, int _y0, int _x1, int _y1)
{
  gfx_draw_line(_x0, _y0, _x1, _y1);
}

void GfxRenderSink::ColourLine(int _x0, int _y0, int _x1, int _y1, int _colour)
{
  gfx_draw_colour_line(_x0, _y0, _x1, _y1, _colour);
}

void GfxRenderSink::Circle(int _cx, int _cy, int _radius, int _colour)
{
  gfx_draw_circle(_cx, _cy, _radius, _colour);
}

void GfxRenderSink::FilledCircle(int _cx, int _cy, int _radius, int _colour)
{
  gfx_draw_filled_circle(_cx, _cy, _radius, _colour);
}

void GfxRenderSink::Triangle(int _x0, int _y0, int _x1, int _y1, int _x2, int _y2, int _colour)
{
  gfx_draw_triangle(_x0, _y0, _x1, _y1, _x2, _y2, _colour);
}

void GfxRenderSink::RenderPolygon(int _numPoints, const int* _points, int _colour, int _dist)
{
  // gfx_render_polygon takes a non-const point list but does not modify it.
  gfx_render_polygon(_numPoints, const_cast<int*>(_points), _colour, _dist);
}

void GfxRenderSink::RenderLine(int _x0, int _y0, int _x1, int _y1, int _dist, int _colour)
{
  gfx_render_line(_x0, _y0, _x1, _y1, _dist, _colour);
}

void GfxRenderSink::DrawModel(const Neuron::Render::ModelDraw& _model)
{
  // Collect the model for this frame's GPU 3D pass (rendered via Scene3D inside
  // gfx2d_flush, between the 2D background and the HUD).
  gfx2d_submit_model(_model);
}

void GfxRenderSink::Sprite(int _spriteId, int _x, int _y)
{
  gfx_draw_sprite(_spriteId, _x, _y);
}

void GfxRenderSink::Text(int _x, int _y, const char* _text)
{
  gfx_display_text(_x, _y, _text);
}

void GfxRenderSink::CentreText(int _y, const char* _text, int _pointSize, int _colour)
{
  gfx_display_centre_text(_y, _text, _pointSize, _colour);
}

void GfxRenderSink::SetClipRegion(int _tx, int _ty, int _bx, int _by)
{
  gfx_set_clip_region(_tx, _ty, _bx, _by);
}

void GfxRenderSink::ClearArea(int _tx, int _ty, int _bx, int _by)
{
  gfx_clear_area(_tx, _ty, _bx, _by);
}

void GfxRenderSink::DrawScanner()  { gfx_draw_scanner(); }
void GfxRenderSink::StartRender()  { gfx_start_render(); }
void GfxRenderSink::FinishRender() { gfx_finish_render(); }
