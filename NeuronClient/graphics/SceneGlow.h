#pragma once

#include "GraphicsCore.h"

#include <d3d11.h>
#include <winrt/base.h>

// SceneGlow - H4's emissive glow post pass for the solid low-poly ship scene.
//
// The scene pass (Scene3D::RenderModels) draws the whole flight image - dust starfield
// background plus the depth-tested solid ships / planet / sun - onto a freshly-cleared
// target. When glow is enabled, that image is rendered into an offscreen scene target
// instead of straight to the back buffer; SceneGlow then blurs it with a separable
// Gaussian (the LineExpand.h GaussianKernel weights, baked into blurPS) and composites
// the sharp scene plus its blurred, intensity-scaled glow onto the back buffer. The
// bright emissive meshes bloom; the dim background contributes almost nothing - the
// retro-vector "glowing solid" look, layered on top of the EXISTING solid meshes rather
// than replacing them with wireframe.
//
// Opt-in and default-off: when disabled Begin() returns nullptr and the scene renders
// straight to the back buffer exactly as before - the proven path is untouched. All the
// offscreen resources are built lazily and rebuilt on a back-buffer resize.

namespace Neuron::Graphics
{
  class SceneGlow
  {
    public:
      static void Startup();
      static void Shutdown();

      static void SetEnabled(bool _enabled) { s_enabled = _enabled; }
      static bool IsEnabled() { return s_enabled; }

      // Glow strength: the blurred scene is added at this multiplier (0 = no glow, ~1 =
      // a strong bloom). Clamped non-negative. Default 0.8.
      static void SetIntensity(float _intensity) { s_intensity = (_intensity < 0.0f) ? 0.0f : _intensity; }

      // Begin the glow frame. If enabled, (re)builds the offscreen targets at the current
      // back-buffer size, clears the scene target to the space background, and returns its
      // RTV so the scene pass renders THERE instead of the back buffer. Returns nullptr when
      // disabled or on any failure - the caller then renders to the back buffer directly and
      // the glow silently no-ops.
      static ID3D11RenderTargetView* Begin();

      // Blur the scene target and composite (sharp + glow*intensity) onto _backBuffer. Call
      // only after a non-null Begin(), once the scene pass has finished. Full-screen passes;
      // depth is off. Unbinds the scene/glow SRVs before returning so next frame can render
      // into them again without a read/write hazard.
      static void Composite(ID3D11RenderTargetView* _backBuffer);

    private:
      // Build (or rebuild, on size change) the offscreen targets + one-time programs/state.
      static bool EnsureResources(UINT _w, UINT _h);
      // Draw the full-screen triangle (postVS, no vertex buffer) into the bound target.
      static void drawFullscreen();

      inline static bool s_enabled = false;   // opt-in (default: no glow, scene -> back buffer)
      inline static float s_intensity = 0.8f;

      // Offscreen targets: the sharp scene, then two ping-pong blur targets, all SRV+RTV at
      // the back-buffer size/format.
      inline static winrt::com_ptr<ID3D11Texture2D> s_sceneTex;
      inline static winrt::com_ptr<ID3D11RenderTargetView> s_sceneRtv;
      inline static winrt::com_ptr<ID3D11ShaderResourceView> s_sceneSrv;
      inline static winrt::com_ptr<ID3D11Texture2D> s_blurTexA;
      inline static winrt::com_ptr<ID3D11RenderTargetView> s_blurRtvA;
      inline static winrt::com_ptr<ID3D11ShaderResourceView> s_blurSrvA;
      inline static winrt::com_ptr<ID3D11Texture2D> s_blurTexB;
      inline static winrt::com_ptr<ID3D11RenderTargetView> s_blurRtvB;
      inline static winrt::com_ptr<ID3D11ShaderResourceView> s_blurSrvB;

      // Post programs (shared full-screen VS; separable blur + composite PS) and their state.
      inline static winrt::com_ptr<ID3D11VertexShader> s_postVs;
      inline static winrt::com_ptr<ID3D11PixelShader> s_blurPs;
      inline static winrt::com_ptr<ID3D11PixelShader> s_compositePs;
      inline static winrt::com_ptr<ID3D11Buffer> s_blurCb;      // b0: float4 u_Dir (texel step)
      inline static winrt::com_ptr<ID3D11Buffer> s_compositeCb; // b0: float4 u_Params (x = intensity)
      inline static winrt::com_ptr<ID3D11SamplerState> s_sampler;
      inline static winrt::com_ptr<ID3D11RasterizerState> s_raster; // cull-none for the triangle

      inline static UINT s_w = 0;
      inline static UINT s_h = 0;
      inline static bool s_progReady = false;   // one-time programs/state built
  };
}
