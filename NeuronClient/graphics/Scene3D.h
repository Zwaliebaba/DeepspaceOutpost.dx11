#pragma once

#include "GraphicsCore.h"

#include <d3d11.h>
#include <winrt/base.h>

#include <cstdint>
#include <functional>
#include <unordered_map>
#include <vector>

#include "Mesh.h"
#include "ModelDraw.h"        // Neuron::Render::ModelDraw
#include "Camera.h"           // Neuron::Client::Camera - the view + projection source

// Native Direct3D 11 3D scene renderer (Neuron::Graphics) - the GPU successor to the
// CPU-projected flight scene. Where the legacy path projected each ship's vertices on
// the CPU and depth-sorted flat 2D polygons (painter's algorithm), Scene3D uploads each
// ship type's geometry once into an immutable vertex/index buffer and draws it with the
// Camera's view + projection matrices and the hardware depth buffer.
//
// Sibling to Render2D (same all-static lifetime, same GraphicsCore device + back buffer
// and the depth buffer alongside it), but a genuinely separate pipeline: a 3-component
// position + normal vertex, a perspective/model constant buffer, and depth-test +
// (currently) cull-off state - none of which fit Render2D's strict-2D contract.
//
// The scene is fed through the render seam: the game submits ModelDraw commands in the
// WORLD frame (floating-origin-relative position + world basis, no D3D); RenderModels
// composes model * View() * Projection() per draw. Mesh geometry is provided by the
// game layer through a callback (it owns the ship tables), so this renderer carries no
// game-specific data.

namespace Neuron::Graphics
{
  class Scene3D
  {
    public:
      static void Startup();
      static void Shutdown();

      // Build a CPU mesh for a ship type on demand. Returns false if the type has no
      // model. The game sets this once at startup (it owns ship_data / ship_solids); the
      // first draw of each type builds + caches the GPU buffers from the returned data.
      using MeshProvider = std::function<bool(int /*type*/, MeshData& /*out*/)>;
      static void SetMeshProvider(MeshProvider _provider);

      // Opt-in faceted directional lighting for ships (Phase 5). Off by default, which
      // reproduces the faithful flat per-face colour exactly. Planet/sun billboards are
      // unaffected. The game toggles this from its "Ship Shading" setting.
      static void SetLightingEnabled(bool _enabled) { s_lit = _enabled; }

      // Opt-in solid-mesh instancing (H2). Off by default, which keeps the proven
      // one-DrawIndexed-per-model path. When on, every ship of a hull type is drawn in a
      // single DrawIndexedInstanced from the SAME solid geometry (identical look, fewer
      // draw calls). Suns still render as individual billboards. Lighting composes with it.
      static void SetInstancingEnabled(bool _enabled) { s_instancing = _enabled; }

      // Dust points for this frame: the streaming starfield rendered in the scene pass (behind
      // the ships) instead of the legacy 2D batch. The game projects the stars with the scene
      // optics and hands over small clip-space quads (6 verts each); Scene3D draws them every
      // frame as the background. One vertex = clip-space XY + brightness.
      struct DustVertex { float x, y, bright; };
      static void SetDust(const DustVertex* _pts, int _count);

      // Submit one WORLD-frame model (ship / planet / sun) for this frame's scene pass. The
      // game's draw pass calls this directly - the successor to routing ModelDraws through the
      // RenderQueue -> GfxRenderSink -> gfx2d round-trip. Accumulated into s_models, consumed
      // and cleared by RenderModels (mirrors how SetDust feeds the dust pass).
      static void SubmitModel(const Neuron::Render::ModelDraw& _model);

      // Render this frame's submitted models (SubmitModel) to _rtv with depth-testing against
      // _dsv. The view + projection come from _camera (Camera::View() / Projection()); the
      // scene is placed in the letterbox content rect (_vpX, _vpY, _vpW, _vpH) in target
      // pixels - the same rect the 2D batch uses, so 3D and HUD align. Clears DEPTH only (the
      // colour target already holds the 2D background) and clears s_models. A no-op if the
      // device/resources are unavailable.
      static void RenderModels(ID3D11RenderTargetView* _rtv, ID3D11DepthStencilView* _dsv,
                               Neuron::Client::Camera& _camera, int _vpX, int _vpY, int _vpW, int _vpH);

    private:
      struct GpuMesh
      {
        winrt::com_ptr<ID3D11Buffer> vb;
        winrt::com_ptr<ID3D11Buffer> ib;
        UINT indexCount = 0;
      };

      static bool EnsureResources();
      // Look up (building + caching on first use) the GPU mesh for a ship type. May
      // return a mesh with indexCount == 0 (cached "no geometry") - callers skip those.
      static const GpuMesh* MeshForType(int _type);

      // Render one sun billboard (a depth-tested camera-facing quad) for a SHIP_SUN
      // model. The world-frame centre is view-transformed here; the quad itself is
      // built in camera space and drawn with the projection alone. Shares the
      // depth/cull/blend state with the ship pass.
      static void renderBillboard(const Neuron::Render::ModelDraw& _model);

      // Draw this frame's dust quads (SetDust) as the background, behind the depth-tested ships.
      static void renderDust();

      // Instanced ship pass (H2): group this frame's ship models by hull type and draw each
      // group with one DrawIndexedInstanced. `_view` / `_viewProj` are the pass matrices
      // (row-vector). Suns are handled by the caller (still per-billboard). No-op if the
      // instanced resources failed to build.
      static void renderModelsInstanced(const DirectX::XMMATRIX& _view, const DirectX::XMMATRIX& _viewProj);

      inline static winrt::com_ptr<ID3D11VertexShader> s_vs;
      inline static winrt::com_ptr<ID3D11PixelShader> s_ps;
      inline static winrt::com_ptr<ID3D11InputLayout> s_layout;
      inline static winrt::com_ptr<ID3D11Buffer> s_cb;
      inline static winrt::com_ptr<ID3D11Buffer> s_shipLightCb; // b2: model-view + directional light (ships)
      inline static bool s_lit = false;                          // opt-in ship lighting (default flat)
      inline static winrt::com_ptr<ID3D11DepthStencilState> s_depth;
      inline static winrt::com_ptr<ID3D11RasterizerState> s_raster;
      inline static winrt::com_ptr<ID3D11BlendState> s_blend;

      // Instanced ship program (H2, opt-in). A second VS/PS + a two-stream input layout
      // (per-vertex mesh + per-instance world/tint), a per-frame view/projection/light
      // cbuffer, and a dynamic per-instance vertex buffer grown lazily in the pass.
      inline static winrt::com_ptr<ID3D11VertexShader> s_instVs;
      inline static winrt::com_ptr<ID3D11PixelShader> s_instPs;
      inline static winrt::com_ptr<ID3D11InputLayout> s_instLayout;
      inline static winrt::com_ptr<ID3D11Buffer> s_instFrameCb;
      inline static winrt::com_ptr<ID3D11Buffer> s_instVb;
      inline static size_t s_instCapacity = 0;
      inline static bool s_instancing = false;   // opt-in solid instancing (default per-model)

      // Billboard (planet / sun) program + its dynamic 6-vertex quad and b1 params.
      inline static winrt::com_ptr<ID3D11VertexShader> s_bbVs;
      inline static winrt::com_ptr<ID3D11PixelShader> s_bbPs;
      inline static winrt::com_ptr<ID3D11Buffer> s_bbVb;
      inline static winrt::com_ptr<ID3D11Buffer> s_bbParamsCb;

      // Dust program (the scene-pass starfield) + its dynamic vertex buffer, this frame's
      // quads, and the depth-off state the background pass draws with.
      inline static winrt::com_ptr<ID3D11VertexShader> s_dustVs;
      inline static winrt::com_ptr<ID3D11PixelShader> s_dustPs;
      inline static winrt::com_ptr<ID3D11InputLayout> s_dustLayout;
      inline static winrt::com_ptr<ID3D11Buffer> s_dustVb;
      inline static winrt::com_ptr<ID3D11DepthStencilState> s_dustDepth; // depth test/write off
      inline static size_t s_dustCapacity = 0;
      inline static std::vector<DustVertex> s_dust;

      // This frame's submitted models (SubmitModel), consumed + cleared by RenderModels.
      inline static std::vector<Neuron::Render::ModelDraw> s_models;

      // The in-progress pass's view / projection (from the Camera), stored unloaded so the
      // statics need no SIMD alignment. Row-vector DirectXMath convention (p' = p * M).
      inline static DirectX::XMFLOAT4X4 s_viewMat;
      inline static DirectX::XMFLOAT4X4 s_projMat;

      inline static std::unordered_map<int, GpuMesh> s_meshes;
      inline static MeshProvider s_provider;
      inline static bool s_ready = false;
  };
}
