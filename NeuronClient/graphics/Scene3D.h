#pragma once

#include "GraphicsCore.h"

#include <d3d11.h>
#include <winrt/base.h>

#include <cstdint>
#include <functional>
#include <unordered_map>
#include <vector>

#include "Mesh.h"
#include "RenderQueue.h"      // Neuron::Render::ModelDraw
#include "SceneProjection.h"  // Neuron::Client::Matrix4 / ViewMetrics

// Native Direct3D 11 3D scene renderer (Neuron::Graphics) - the GPU successor to the
// CPU-projected flight scene. Where the legacy path projected each ship's vertices on
// the CPU and depth-sorted flat 2D polygons (painter's algorithm), Scene3D uploads each
// ship type's geometry once into an immutable vertex/index buffer and draws it with a
// real perspective matrix + hardware depth buffer.
//
// Sibling to Render2D (same all-static lifetime, same GraphicsCore device + back buffer
// and the depth buffer alongside it), but a genuinely separate pipeline: a 3-component
// position + normal vertex, a perspective/model constant buffer, and depth-test +
// (currently) cull-off state - none of which fit Render2D's strict-2D contract.
//
// The scene is fed through the render seam: the sim records ModelDraw commands (camera-
// space transform, no D3D), and the client replays them here (see gfx2d's scene marker).
// Mesh geometry is provided by the game layer through a callback (it owns the ship
// tables), so this renderer carries no game-specific data.

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

      // Opt-in procedural skybox (star migration): a gradient + procedural-star background
      // drawn behind the scene, replacing the black clear. Off by default (behaviour
      // unchanged). NOTE: when on it fills the scene viewport, so it currently occludes the
      // legacy 2D starfield - the 3D "dust" that replaces that starfield is the next step.
      static void SetSkyboxEnabled(bool _enabled) { s_skybox = _enabled; }

      // Dust points for this frame (star migration): the streaming starfield rendered in the
      // scene pass over the skybox instead of the legacy 2D batch. The game projects the stars
      // with the scene optics and hands over small clip-space quads (6 verts each); Scene3D
      // draws them - but only when the skybox is enabled (otherwise the legacy 2D starfield
      // still shows). One vertex = clip-space XY + brightness.
      struct DustVertex { float x, y, bright; };
      static void SetDust(const DustVertex* _pts, int _count);

      // Render camera-space models to _rtv with depth-testing against _dsv. The
      // projection comes from _view (the live flight optics); the scene is placed in the
      // letterbox content rect (_vpX, _vpY, _vpW, _vpH) in target pixels - the same rect
      // the 2D batch uses, so 3D and HUD align. Clears DEPTH only (the colour target
      // already holds the 2D background). A no-op if the device/resources are unavailable.
      static void RenderModels(ID3D11RenderTargetView* _rtv, ID3D11DepthStencilView* _dsv,
                               const Neuron::Client::ViewMetrics& _view, int _vpX, int _vpY, int _vpW, int _vpH,
                               const Neuron::Render::ModelDraw* _models, int _count);

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

      // Render one camera-space planet/sun billboard (a depth-tested quad) for a
      // SHIP_PLANET / SHIP_SUN model. Shares the depth/cull/blend state with the ship
      // pass; uses the billboard shader + a per-billboard params buffer.
      static void renderBillboard(const Neuron::Render::ModelDraw& _model, const Neuron::Client::Matrix4& _proj);

      // Draw the procedural skybox background (full-screen, depth-disabled) at the start of
      // the scene pass, before the depth-tested ships.
      static void renderSkybox();

      // Draw this frame's dust quads (SetDust) over the skybox, behind the ships.
      static void renderDust();

      inline static winrt::com_ptr<ID3D11VertexShader> s_vs;
      inline static winrt::com_ptr<ID3D11PixelShader> s_ps;
      inline static winrt::com_ptr<ID3D11InputLayout> s_layout;
      inline static winrt::com_ptr<ID3D11Buffer> s_cb;
      inline static winrt::com_ptr<ID3D11Buffer> s_shipLightCb; // b2: model-view + directional light (ships)
      inline static bool s_lit = false;                          // opt-in ship lighting (default flat)
      inline static winrt::com_ptr<ID3D11DepthStencilState> s_depth;
      inline static winrt::com_ptr<ID3D11RasterizerState> s_raster;
      inline static winrt::com_ptr<ID3D11BlendState> s_blend;

      // Billboard (planet / sun) program + its dynamic 6-vertex quad and b1 params.
      inline static winrt::com_ptr<ID3D11VertexShader> s_bbVs;
      inline static winrt::com_ptr<ID3D11PixelShader> s_bbPs;
      inline static winrt::com_ptr<ID3D11Buffer> s_bbVb;
      inline static winrt::com_ptr<ID3D11Buffer> s_bbParamsCb;
      // Procedural skybox program (star migration) + its b0 params. No vertex buffer -
      // the VS builds a full-screen triangle from SV_VertexID.
      inline static winrt::com_ptr<ID3D11VertexShader> s_skyVs;
      inline static winrt::com_ptr<ID3D11PixelShader> s_skyPs;
      inline static winrt::com_ptr<ID3D11Buffer> s_skyCb;
      inline static winrt::com_ptr<ID3D11DepthStencilState> s_skyDepth; // depth test/write off
      inline static bool s_skybox = false;                              // opt-in (default off)

      // Dust program (star migration) + its dynamic vertex buffer and this frame's quads.
      inline static winrt::com_ptr<ID3D11VertexShader> s_dustVs;
      inline static winrt::com_ptr<ID3D11PixelShader> s_dustPs;
      inline static winrt::com_ptr<ID3D11InputLayout> s_dustLayout;
      inline static winrt::com_ptr<ID3D11Buffer> s_dustVb;
      inline static size_t s_dustCapacity = 0;
      inline static std::vector<DustVertex> s_dust;

      // Viewport optics for the in-progress RenderModels pass (billboard sizing).
      inline static Neuron::Client::ViewMetrics s_view;

      inline static std::unordered_map<int, GpuMesh> s_meshes;
      inline static MeshProvider s_provider;
      inline static bool s_ready = false;
  };
}
