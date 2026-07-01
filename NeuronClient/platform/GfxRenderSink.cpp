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

void GfxRenderSink::Line(int _x0, int _y0, int _x1, int _y1)
{
  gfx_draw_line(_x0, _y0, _x1, _y1);
}

void GfxRenderSink::ColourLine(int _x0, int _y0, int _x1, int _y1, int _colour)
{
  gfx_draw_colour_line(_x0, _y0, _x1, _y1, _colour);
}

void GfxRenderSink::Triangle(int _x0, int _y0, int _x1, int _y1, int _x2, int _y2, int _colour)
{
  gfx_draw_triangle(_x0, _y0, _x1, _y1, _x2, _y2, _colour);
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

void GfxRenderSink::FinishRender() { gfx_finish_render(); }
