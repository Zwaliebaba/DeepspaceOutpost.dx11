#pragma once

#include "GraphicsCore.h"

#include <d3d11.h>
#include <winrt/base.h>

#include <cstdint>
#include <memory>
#include <vector>

#include "Mesh.h"            // Neuron::Graphics::MeshVertex - the debris triangle vertex
#include "ParticleVertex.h"  // Neuron::Graphics::ParticleVertex - the additive billboard vertex
#include "Camera.h"          // Neuron::Client::Camera - the view + projection source
#include "TextureManager.h"  // Neuron::Graphics::Texture - the Particle.dds sprite

// Native Direct3D 11 particle / debris renderer (Neuron::Graphics) - the GPU pass for the
// ported Darwinia explosion effects (see explosion.md). It draws two batches the client
// effects subsystem (Neuron::Client::Effects) rebuilds each frame from the live simulation:
//
//   * debris    - loose flat-shaded triangles shattered from a dead hull's mesh
//                 (MeshVertex), drawn depth-tested + depth-writing like the ships (solid
//                 opaque fragments), and
//   * particles - additive camera-facing textured billboards (the fireball core, sparks and
//                 trails), depth-tested but NOT depth-writing, so ships occlude them but they
//                 never occlude each other.
//
// Sibling to Scene3D / SceneGlow (same all-static lifetime, same GraphicsCore device and the
// depth buffer alongside it). Render() is invoked from gfx_render_3d_scene AFTER
// Scene3D::RenderModels and BEFORE SceneGlow::Composite, so the effects land in the glow
// target and pick up bloom, and both batches depth-test against the world the ship pass just
// drew.
//
// The renderer owns no simulation state: SetDebris / SetParticles receive this frame's vertex
// batches (the Scene3D::SetDust precedent) and Render draws them. The debris pass reuses the
// scene3d mesh program + MeshVertex layout; the particle pass has its own additive program and
// a compact ParticleVertex.

namespace Neuron::Graphics
{
  class SceneParticles
  {
    public:
      static void Startup();
      static void Shutdown();

      // The additive billboard vertex (Neuron::Graphics::ParticleVertex, in ParticleVertex.h):
      // one camera-facing quad corner the simulation builds (6 verts each) and hands the whole
      // batch over via SetParticles.

      // This frame's batches, rebuilt every frame by the effects subsystem (the SetDust
      // precedent). A null or empty batch clears it, so a frame with no effects draws nothing.
      // Copied into the renderer's staging vector and uploaded to a dynamic VB in Render.
      static void SetParticles(const ParticleVertex* _verts, int _count);
      static void SetDebris(const MeshVertex* _verts, int _count);

      // Draw this frame's debris + particle batches into _rtv, depth-testing against _dsv, with
      // _camera's view + projection, placed in the (_vpX,_vpY,_vpW,_vpH) content rect - the same
      // target / rect / camera the ship pass used, so the effects register with the world. Does
      // NOT clear colour or depth (it draws over the ship pass) and does NOT clear the batches
      // (the subsystem owns them and overwrites via SetDebris/SetParticles next frame). A no-op
      // if the device / resources are unavailable.
      static void Render(ID3D11RenderTargetView* _rtv, ID3D11DepthStencilView* _dsv,
                         Neuron::Client::Camera& _camera, int _vpX, int _vpY, int _vpW, int _vpH);

      // Bring-up smoke test (default OFF): when on and there is no live particle batch, draw one
      // additive quad in front of the camera to prove the particle pass - blend / depth / shader
      // / texture / matrix upload - in isolation. It proved the phase-1 skeleton; phase 2 feeds
      // real particles through SetParticles, so it stays off. Kept as a manual bring-up toggle.
      static void SetSmokeTest(bool _on) { s_smokeTest = _on; }

    private:
      static bool EnsureResources();
      static void renderDebris(const DirectX::XMMATRIX& _viewProj);
      static void renderParticles(const DirectX::XMMATRIX& _viewProj, Neuron::Client::Camera& _camera);

      // Additive particle program: its own VS/PS, the ParticleVertex input layout, a b0
      // view-projection cbuffer, a dynamic vertex buffer grown lazily, additive blend, a
      // depth-test-but-no-write state, a linear-clamp sampler and the Particle.dds sprite.
      inline static winrt::com_ptr<ID3D11VertexShader> s_partVs;
      inline static winrt::com_ptr<ID3D11PixelShader> s_partPs;
      inline static winrt::com_ptr<ID3D11InputLayout> s_partLayout;
      inline static winrt::com_ptr<ID3D11Buffer> s_partCb;               // b0: view-projection
      inline static winrt::com_ptr<ID3D11Buffer> s_partVb;
      inline static winrt::com_ptr<ID3D11BlendState> s_partBlend;        // additive (SRC_ALPHA, ONE)
      inline static winrt::com_ptr<ID3D11DepthStencilState> s_partDepth; // depth test on, write off
      inline static winrt::com_ptr<ID3D11SamplerState> s_sampler;        // linear clamp
      inline static std::shared_ptr<Texture> s_partSprite;              // Textures/Particle.dds
      inline static size_t s_partCapacity = 0;
      inline static std::vector<ParticleVertex> s_particles;

      // Debris program: reuses the scene3d mesh VS/PS + MeshVertex layout (loose opaque
      // triangles, like ships), with its own b0 mvp+tint and b2 (lighting-off) cbuffers, a
      // dynamic vertex buffer, and a depth test+write / cull-none state. The verts arrive in the
      // render frame, so the mvp is just the camera view-projection (world already baked in).
      inline static winrt::com_ptr<ID3D11VertexShader> s_meshVs;
      inline static winrt::com_ptr<ID3D11PixelShader> s_meshPs;
      inline static winrt::com_ptr<ID3D11InputLayout> s_meshLayout;
      inline static winrt::com_ptr<ID3D11Buffer> s_meshCb;               // b0: mvp + tint
      inline static winrt::com_ptr<ID3D11Buffer> s_meshLightCb;          // b2: lighting (params.x = 0)
      inline static winrt::com_ptr<ID3D11Buffer> s_meshVb;
      inline static winrt::com_ptr<ID3D11DepthStencilState> s_meshDepth; // depth test + write
      inline static winrt::com_ptr<ID3D11RasterizerState> s_raster;      // cull none (shared)
      inline static size_t s_meshCapacity = 0;
      inline static std::vector<MeshVertex> s_debris;

      inline static bool s_smokeTest = false; // bring-up quad, off by default (see SetSmokeTest)
      inline static bool s_ready = false;
  };
}
