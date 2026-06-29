#pragma once

#include "GraphicsCore.h"

#include <d3d11.h>
#include <winrt/base.h>

#include <cstdint>
#include <vector>

// Native Direct3D 11 batched 2D layer (Neuron::Graphics).
//
// This is the shared foundation the 2D drawers are migrating onto, replacing the
// hand-rolled "set up an ortho pass, draw, tear down" blocks that currently live in
// GuiOverlay::Render and gfx2d_flush (each driven through ImmediateRenderer).
//
// A frame's 2D work happens inside a Begin/End scope:
//
//     Render2D::Begin(rtv, width, height);   // ortho + blend + depth/cull off, ONCE
//       Render2D::FillRect(...);             // colored + textured primitives, batched
//       Render2D::TexQuad(fontSRV, ...);     // in submission order
//     Render2D::End();                       // flush the whole batch
//
// One interleaved vertex format and one shader serve both colored and textured work:
// colored primitives sample a 1x1 white texture (so colour = the per-vertex colour),
// while text/sprites bind their atlas. Submission-ordered batching means lines, panels,
// glyphs and sprites composite exactly in the order the caller draws them, the way the
// immediate-mode path did.
//
// All-static, mirroring Graphics::Core / ImmediateRenderer so the siblings match. The
// shaders are compiled at runtime with D3DCompile (as Renderer's present pipeline is),
// so no fxc / CompiledShaders build step is involved.

namespace Neuron::Graphics
{
  class Render2D
  {
    public:
      static void Startup();
      static void Shutdown();

      // Pack 8-bit channels into the 0xAABBGGRR order the submit calls expect (R in
      // the low byte), matching the renderer's R8G8B8A8_UNORM vertex colour.
      static constexpr uint32_t Rgba(uint8_t r, uint8_t g, uint8_t b, uint8_t a) noexcept
      {
        return static_cast<uint32_t>(r) | (static_cast<uint32_t>(g) << 8) | (static_cast<uint32_t>(b) << 16) |
          (static_cast<uint32_t>(a) << 24);
      }

      // Open a 2D pass targeting rtv with a (width x height) Y-down orthographic
      // projection (pixel coordinates, origin top-left). Resets the scissor to the
      // whole target. Submissions are batched until End. The filter applies to the
      // textured draws (text/sprites); colored primitives are unaffected.
      static void Begin(ID3D11RenderTargetView* rtv, int width, int height,
                        D3D11_FILTER filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR);
      static void End();

      // Scissor rectangle in target pixels; clamps to the target. Cleared (full
      // target) at every Begin. A change starts a new batch command.
      static void SetClip(int x, int y, int w, int h);
      static void ClearClip();

      // Colored primitives. rgba is 0xAABBGGRR (R in the low byte) to match
      // ImmediateRenderer::PackColor and the palette byte order.
      static void FillRect(float x0, float y0, float x1, float y1, uint32_t rgba);
      static void DrawLine(float x0, float y0, float x1, float y1, uint32_t rgba);
      static void DrawTriangle(float x0, float y0, float x1, float y1, float x2, float y2, uint32_t rgba);
      static void PlotPoint(float x, float y, uint32_t rgba);

      // Textured quad: the atlas sub-rect (u0,v0)-(u1,v1) stretched to the screen
      // rect (x0,y0)-(x1,y1), tinted by rgba. Used for glyphs and sprites.
      static void TexQuad(ID3D11ShaderResourceView* srv, float x0, float y0, float x1, float y1, float u0, float v0,
                          float u1, float v1, uint32_t rgba);

      // General textured quad with an explicit per-corner colour (top-left,
      // top-right, bottom-right, bottom-left), for gradients (e.g. window panels).
      static void TexQuadColored(ID3D11ShaderResourceView* srv, float x0, float y0, float x1, float y1, float u0,
                                 float v0, float u1, float v1, uint32_t cTL, uint32_t cTR, uint32_t cBR, uint32_t cBL);

    private:
      struct Vertex
      {
        float x, y, u, v;
        uint32_t rgba;
      };

      enum class Topo
      {
        Points,
        Lines,
        Tris,
      };

      struct Cmd
      {
        Topo topo;
        uint32_t start;
        uint32_t count;
        ID3D11ShaderResourceView* srv; // the 1x1 white texture for colored prims
        D3D11_RECT scissor;
      };

      static bool EnsureResources();
      static void Append(Topo topo, ID3D11ShaderResourceView* srv, const Vertex* v, int n);
      static void Flush();

      inline static winrt::com_ptr<ID3D11VertexShader> s_vs;
      inline static winrt::com_ptr<ID3D11PixelShader> s_ps;
      inline static winrt::com_ptr<ID3D11InputLayout> s_layout;
      inline static winrt::com_ptr<ID3D11Buffer> s_vb;
      inline static winrt::com_ptr<ID3D11Buffer> s_cb;
      inline static winrt::com_ptr<ID3D11BlendState> s_blend;
      inline static winrt::com_ptr<ID3D11DepthStencilState> s_depth;
      inline static winrt::com_ptr<ID3D11RasterizerState> s_raster;
      inline static winrt::com_ptr<ID3D11SamplerState> s_samplerLinear;
      inline static winrt::com_ptr<ID3D11SamplerState> s_samplerPoint;
      inline static winrt::com_ptr<ID3D11ShaderResourceView> s_white;
      inline static size_t s_vbCapacity = 0;

      inline static std::vector<Vertex> s_verts;
      inline static std::vector<Cmd> s_cmds;
      inline static D3D11_RECT s_scissor = {};
      inline static ID3D11RenderTargetView* s_rtv = nullptr;
      inline static int s_width = 0;
      inline static int s_height = 0;
      inline static D3D11_FILTER s_filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
      inline static bool s_inPass = false;
  };
}
