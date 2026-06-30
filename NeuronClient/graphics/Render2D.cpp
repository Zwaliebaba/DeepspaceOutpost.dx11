#include "pch.h"
#include "Render2D.h"

#include <d3dcompiler.h>
#include <DirectXMath.h>

#include <cassert>
#include <cstring>

using winrt::com_ptr;
using winrt::check_hresult;
using namespace DirectX;

namespace Neuron::Graphics
{
  namespace
  {
    // One interleaved vertex serves colored and textured draws. The pixel shader is
    // col * texture: colored primitives sample a 1x1 white texture, so the result is
    // just the per-vertex colour; text/sprites bind their atlas. Row-major, row-vector
    // matrix convention (mul(pos, M)), matching the rest of the renderer.
    const char* kShaderHLSL = R"(
cbuffer Cb2D : register(b0)
{
    row_major float4x4 u_Proj;
};

struct VSIn  { float2 pos : POSITION; float2 uv : TEXCOORD0; float4 col : COLOR0; };
struct VSOut { float4 pos : SV_Position; float2 uv : TEXCOORD0; float4 col : COLOR0; };

VSOut VSMain(VSIn i)
{
    VSOut o;
    o.pos = mul(float4(i.pos, 0.0, 1.0), u_Proj);
    o.uv  = i.uv;
    o.col = i.col;
    return o;
}

Texture2D    u_Tex : register(t0);
SamplerState u_Smp : register(s0);

float4 PSMain(VSOut i) : SV_Target
{
    return i.col * u_Tex.Sample(u_Smp, i.uv);
}
)";

    // Built-in text-outline program. Same vertex contract as the default (b0 ortho,
    // POSITION/TEXCOORD0/COLOR0); the PS reads b1 for the outline colour + atlas texel
    // size, samples the glyph coverage (alpha) plus an 8-tap ring one outline-width out,
    // and composites the per-vertex text colour over the outline colour. Straight-alpha
    // blend: it returns the coverage-normalised colour and total coverage in alpha.
    const char* kTextOutlineHLSL = R"(
cbuffer Cb2D : register(b0)
{
    row_major float4x4 u_Proj;
};
cbuffer TextParams : register(b1)
{
    float4 u_OutlineColor;   // rgb + a
    float4 u_OutlineParams;  // x=texelW, y=texelH, z=widthTexels, w=unused
};

struct VSIn  { float2 pos : POSITION; float2 uv : TEXCOORD0; float4 col : COLOR0; };
struct VSOut { float4 pos : SV_Position; float2 uv : TEXCOORD0; float4 col : COLOR0; };

VSOut VSMain(VSIn i)
{
    VSOut o;
    o.pos = mul(float4(i.pos, 0.0, 1.0), u_Proj);
    o.uv  = i.uv;
    o.col = i.col;
    return o;
}

Texture2D    u_Tex : register(t0);
SamplerState u_Smp : register(s0);

float4 PSMain(VSOut i) : SV_Target
{
    float2 d = u_OutlineParams.xy * max(u_OutlineParams.z, 0.0);
    float c = u_Tex.Sample(u_Smp, i.uv).a;                    // glyph coverage at centre
    float o = 0.0;                                            // max coverage in the ring
    o = max(o, u_Tex.Sample(u_Smp, i.uv + float2( d.x, 0.0)).a);
    o = max(o, u_Tex.Sample(u_Smp, i.uv + float2(-d.x, 0.0)).a);
    o = max(o, u_Tex.Sample(u_Smp, i.uv + float2(0.0,  d.y)).a);
    o = max(o, u_Tex.Sample(u_Smp, i.uv + float2(0.0, -d.y)).a);
    o = max(o, u_Tex.Sample(u_Smp, i.uv + float2( d.x,  d.y)).a);
    o = max(o, u_Tex.Sample(u_Smp, i.uv + float2(-d.x,  d.y)).a);
    o = max(o, u_Tex.Sample(u_Smp, i.uv + float2( d.x, -d.y)).a);
    o = max(o, u_Tex.Sample(u_Smp, i.uv + float2(-d.x, -d.y)).a);

    float textA = c * i.col.a;
    float outA  = saturate(o) * (1.0 - c) * u_OutlineColor.a;
    float a = textA + outA;
    if (a <= 0.00390625) discard;                            // < 1/256: nothing to draw
    float3 rgb = (i.col.rgb * textA + u_OutlineColor.rgb * outA) / a;
    return float4(rgb, a);
}
)";

    // Compile one HLSL source/entry/profile. Returns null on failure (logging the
    // compiler errors) rather than throwing, so a bad caller-supplied program shader
    // degrades to the default instead of taking down the app.
    com_ptr<ID3DBlob> CompileHLSL(const char* src, const char* entry, const char* target)
    {
      UINT flags = D3DCOMPILE_ENABLE_STRICTNESS;
#ifdef _DEBUG
      flags |= D3DCOMPILE_DEBUG | D3DCOMPILE_SKIP_OPTIMIZATION;
#endif
      com_ptr<ID3DBlob> code, errors;
      const HRESULT hr =
        D3DCompile(src, std::strlen(src), nullptr, nullptr, nullptr, entry, target, flags, 0, code.put(), errors.put());
      if (FAILED(hr))
      {
        if (errors)
          OutputDebugStringA(static_cast<const char*>(errors->GetBufferPointer()));
        return nullptr;
      }
      return code;
    }

    com_ptr<ID3D11SamplerState> MakeSampler(ID3D11Device* device, D3D11_FILTER filter)
    {
      D3D11_SAMPLER_DESC sd{};
      sd.Filter = filter;
      sd.AddressU = sd.AddressV = sd.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
      sd.ComparisonFunc = D3D11_COMPARISON_NEVER;
      sd.MaxLOD = D3D11_FLOAT32_MAX;
      com_ptr<ID3D11SamplerState> s;
      check_hresult(device->CreateSamplerState(&sd, s.put()));
      return s;
    }
  } // namespace

  void Render2D::Startup()
  {
    // Resources are created lazily on first use (the device may not be up yet at the
    // point Startup is called), so this is just a clean slate.
    Shutdown();
  }

  void Render2D::Shutdown()
  {
    s_programs.clear();
    s_program = DefaultProgram;
    s_textOutlineProgram = DefaultProgram;
    s_layout = nullptr;
    s_vb = nullptr;
    s_cb = nullptr;
    s_paramsCb = nullptr;
    s_blend = nullptr;
    s_depth = nullptr;
    s_raster = nullptr;
    s_samplerLinear = nullptr;
    s_samplerPoint = nullptr;
    s_white = nullptr;
    s_vbCapacity = 0;
    s_verts.clear();
    s_cmds.clear();
    s_inPass = false;
  }

  bool Render2D::EnsureResources()
  {
    if (!s_programs.empty())
      return true;

    ID3D11Device* device = Core::GetD3DDevice();
    if (!device)
      return false;

    // Program 0: the built-in default (DefaultProgram), col * texture.
    com_ptr<ID3DBlob> vs = CompileHLSL(kShaderHLSL, "VSMain", "vs_5_0");
    com_ptr<ID3DBlob> ps = CompileHLSL(kShaderHLSL, "PSMain", "ps_5_0");
    if (!vs || !ps)
      return false;
    Program def;
    check_hresult(device->CreateVertexShader(vs->GetBufferPointer(), vs->GetBufferSize(), nullptr, def.vs.put()));
    check_hresult(device->CreatePixelShader(ps->GetBufferPointer(), ps->GetBufferSize(), nullptr, def.ps.put()));
    s_programs.push_back(std::move(def));

    // The one input layout (built from the default VS) serves every program: each must
    // share this vertex input signature.
    const D3D11_INPUT_ELEMENT_DESC elems[] = {
      {"POSITION", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0},
      {"TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 8, D3D11_INPUT_PER_VERTEX_DATA, 0},
      {"COLOR", 0, DXGI_FORMAT_R8G8B8A8_UNORM, 0, 16, D3D11_INPUT_PER_VERTEX_DATA, 0},
    };
    check_hresult(device->CreateInputLayout(elems, _countof(elems), vs->GetBufferPointer(), vs->GetBufferSize(),
                                            s_layout.put()));

    D3D11_BUFFER_DESC cbd{};
    cbd.ByteWidth = sizeof(XMFLOAT4X4);
    cbd.Usage = D3D11_USAGE_DYNAMIC;
    cbd.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
    cbd.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
    check_hresult(device->CreateBuffer(&cbd, nullptr, s_cb.put()));

    // b1: shared params for custom programs (e.g. the text-outline colour + texel size).
    D3D11_BUFFER_DESC pbd{};
    pbd.ByteWidth = sizeof(s_params);
    pbd.Usage = D3D11_USAGE_DYNAMIC;
    pbd.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
    pbd.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
    check_hresult(device->CreateBuffer(&pbd, nullptr, s_paramsCb.put()));

    D3D11_BLEND_DESC bd{};
    bd.RenderTarget[0].BlendEnable = TRUE;
    bd.RenderTarget[0].SrcBlend = D3D11_BLEND_SRC_ALPHA;
    bd.RenderTarget[0].DestBlend = D3D11_BLEND_INV_SRC_ALPHA;
    bd.RenderTarget[0].BlendOp = D3D11_BLEND_OP_ADD;
    bd.RenderTarget[0].SrcBlendAlpha = D3D11_BLEND_ONE;
    bd.RenderTarget[0].DestBlendAlpha = D3D11_BLEND_INV_SRC_ALPHA;
    bd.RenderTarget[0].BlendOpAlpha = D3D11_BLEND_OP_ADD;
    bd.RenderTarget[0].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
    check_hresult(device->CreateBlendState(&bd, s_blend.put()));

    D3D11_DEPTH_STENCIL_DESC dsd{};
    dsd.DepthEnable = FALSE;
    dsd.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ZERO;
    dsd.StencilEnable = FALSE;
    check_hresult(device->CreateDepthStencilState(&dsd, s_depth.put()));

    D3D11_RASTERIZER_DESC rd{};
    rd.FillMode = D3D11_FILL_SOLID;
    rd.CullMode = D3D11_CULL_NONE;
    rd.DepthClipEnable = TRUE;
    rd.ScissorEnable = TRUE;
    check_hresult(device->CreateRasterizerState(&rd, s_raster.put()));

    s_samplerLinear = MakeSampler(device, D3D11_FILTER_MIN_MAG_MIP_LINEAR);
    s_samplerPoint = MakeSampler(device, D3D11_FILTER_MIN_MAG_MIP_POINT);

    // 1x1 opaque-white texture so colored primitives can share the textured shader.
    const uint32_t whitePixel = 0xFFFFFFFFu;
    D3D11_TEXTURE2D_DESC td{};
    td.Width = td.Height = 1;
    td.MipLevels = td.ArraySize = 1;
    td.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    td.SampleDesc.Count = 1;
    td.Usage = D3D11_USAGE_IMMUTABLE;
    td.BindFlags = D3D11_BIND_SHADER_RESOURCE;
    D3D11_SUBRESOURCE_DATA srd{};
    srd.pSysMem = &whitePixel;
    srd.SysMemPitch = sizeof(whitePixel);
    com_ptr<ID3D11Texture2D> tex;
    check_hresult(device->CreateTexture2D(&td, &srd, tex.put()));
    check_hresult(device->CreateShaderResourceView(tex.get(), nullptr, s_white.put()));

    // Built-in program 1: the text-outline shader.
    s_textOutlineProgram = AddProgram(kTextOutlineHLSL);

    return true;
  }

  // Compile (VSMain/PSMain) + create a program from one HLSL source and append it.
  // The device must already be up. Returns DefaultProgram on any failure.
  Render2D::ProgramId Render2D::AddProgram(const char* hlslSource)
  {
    ID3D11Device* device = Core::GetD3DDevice();
    if (!device || !hlslSource)
      return DefaultProgram;

    com_ptr<ID3DBlob> vs = CompileHLSL(hlslSource, "VSMain", "vs_5_0");
    com_ptr<ID3DBlob> ps = CompileHLSL(hlslSource, "PSMain", "ps_5_0");
    if (!vs || !ps)
    {
      // CompileHLSL already logged the compiler errors.
      assert(false && "Render2D::AddProgram: shader compile failed");
      return DefaultProgram;
    }

    Program p;
    if (FAILED(device->CreateVertexShader(vs->GetBufferPointer(), vs->GetBufferSize(), nullptr, p.vs.put())) ||
        FAILED(device->CreatePixelShader(ps->GetBufferPointer(), ps->GetBufferSize(), nullptr, p.ps.put())))
    {
      OutputDebugStringA("Render2D: shader program creation failed; falling back to the default program.\n");
      assert(false && "Render2D::AddProgram: shader creation failed");
      return DefaultProgram;
    }

    s_programs.push_back(std::move(p));
    return static_cast<ProgramId>(s_programs.size() - 1);
  }

  Render2D::ProgramId Render2D::RegisterProgram(const char* hlslSource)
  {
    if (!EnsureResources())
      return DefaultProgram;
    return AddProgram(hlslSource);
  }

  void Render2D::SetProgram(ProgramId program)
  {
    s_program = (program < s_programs.size()) ? program : DefaultProgram;
  }

  Render2D::ProgramId Render2D::TextOutlineProgram() { return s_textOutlineProgram; }

  void Render2D::SetShaderParams(const void* data, size_t bytes)
  {
    if (!data)
      return;
    const size_t n = (bytes < sizeof(s_params)) ? bytes : sizeof(s_params);
    std::memcpy(s_params, data, n);
  }

  void Render2D::SetTextOutline(uint32_t outlineRgba, float texelW, float texelH, float widthTexels)
  {
    // b1 layout: float4 outlineColor; float4 params(texelW, texelH, widthTexels, 0).
    const float params[8] = {
      (outlineRgba & 0xFFu) / 255.0f,         ((outlineRgba >> 8) & 0xFFu) / 255.0f,
      ((outlineRgba >> 16) & 0xFFu) / 255.0f, ((outlineRgba >> 24) & 0xFFu) / 255.0f,
      texelW,                                 texelH,
      widthTexels,                            0.0f,
    };
    SetShaderParams(params, sizeof(params));
  }

  void Render2D::Begin(ID3D11RenderTargetView* rtv, int width, int height, D3D11_FILTER filter)
  {
    if (!EnsureResources())
      return;

    s_rtv = rtv;
    s_width = width;
    s_height = height;
    s_filter = filter;
    s_inPass = true;

    s_verts.clear();
    s_cmds.clear();
    s_program = DefaultProgram;
    ClearClip();

    // Y-down pixel-space orthographic projection (origin top-left): maps pixel
    // coordinates straight to clip space.
    const XMMATRIX proj = XMMatrixOrthographicOffCenterRH(0.0f, static_cast<float>(width), static_cast<float>(height),
                                                          0.0f, -1.0f, 1.0f);
    D3D11_MAPPED_SUBRESOURCE mapped;
    auto* ctx = Core::GetD3DDeviceContext();
    check_hresult(ctx->Map(s_cb.get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped));
    XMStoreFloat4x4(static_cast<XMFLOAT4X4*>(mapped.pData), proj);
    ctx->Unmap(s_cb.get(), 0);
  }

  void Render2D::End()
  {
    if (!s_inPass)
      return;
    Flush();
    s_inPass = false;
  }

  void Render2D::ClearClip()
  {
    s_scissor = {0, 0, s_width, s_height};
  }

  void Render2D::SetClip(int x, int y, int w, int h)
  {
    LONG l = x, t = y, r = x + w, b = y + h;
    if (l < 0)
      l = 0;
    if (t < 0)
      t = 0;
    if (r > s_width)
      r = s_width;
    if (b > s_height)
      b = s_height;
    s_scissor = {l, t, r, b};
  }

  void Render2D::Append(Topo topo, ID3D11ShaderResourceView* srv, const Vertex* v, int n)
  {
    if (!s_inPass)
      return;

    if (!s_cmds.empty())
    {
      Cmd& back = s_cmds.back();
      if (back.topo == topo && back.srv == srv && back.program == s_program && back.scissor.left == s_scissor.left &&
          back.scissor.top == s_scissor.top && back.scissor.right == s_scissor.right &&
          back.scissor.bottom == s_scissor.bottom)
      {
        back.count += n;
        s_verts.insert(s_verts.end(), v, v + n);
        return;
      }
    }
    s_cmds.push_back({topo, static_cast<uint32_t>(s_verts.size()), static_cast<uint32_t>(n), srv, s_scissor, s_program});
    s_verts.insert(s_verts.end(), v, v + n);
  }

  void Render2D::FillRect(float x0, float y0, float x1, float y1, uint32_t rgba)
  {
    const Vertex q[6] = {
      {x0, y0, 0, 0, rgba}, {x1, y0, 0, 0, rgba}, {x1, y1, 0, 0, rgba},
      {x0, y0, 0, 0, rgba}, {x1, y1, 0, 0, rgba}, {x0, y1, 0, 0, rgba},
    };
    Append(Topo::Tris, s_white.get(), q, 6);
  }

  void Render2D::DrawLine(float x0, float y0, float x1, float y1, uint32_t rgba)
  {
    const Vertex v[2] = {{x0, y0, 0, 0, rgba}, {x1, y1, 0, 0, rgba}};
    Append(Topo::Lines, s_white.get(), v, 2);
  }

  void Render2D::DrawTriangle(float x0, float y0, float x1, float y1, float x2, float y2, uint32_t rgba)
  {
    const Vertex t[3] = {{x0, y0, 0, 0, rgba}, {x1, y1, 0, 0, rgba}, {x2, y2, 0, 0, rgba}};
    Append(Topo::Tris, s_white.get(), t, 3);
  }

  void Render2D::PlotPoint(float x, float y, uint32_t rgba)
  {
    const Vertex p = {x, y, 0, 0, rgba};
    Append(Topo::Points, s_white.get(), &p, 1);
  }

  void Render2D::Submit(Topo topo, const Vertex* verts, int count, ID3D11ShaderResourceView* srv)
  {
    if (count <= 0)
      return;
    assert(verts && "Render2D::Submit: null vertices");
    assert((topo != Topo::Lines || count % 2 == 0) && "Render2D::Submit: line list needs an even vertex count");
    assert((topo != Topo::Tris || count % 3 == 0) && "Render2D::Submit: triangle list needs a multiple of 3");
    Append(topo, srv ? srv : s_white.get(), verts, count);
  }

  void Render2D::TexQuad(ID3D11ShaderResourceView* srv, float x0, float y0, float x1, float y1, float u0, float v0,
                         float u1, float v1, uint32_t rgba)
  {
    TexQuadColored(srv, x0, y0, x1, y1, u0, v0, u1, v1, rgba, rgba, rgba, rgba);
  }

  void Render2D::TexQuadColored(ID3D11ShaderResourceView* srv, float x0, float y0, float x1, float y1, float u0,
                                float v0, float u1, float v1, uint32_t cTL, uint32_t cTR, uint32_t cBR, uint32_t cBL)
  {
    const Vertex q[6] = {
      {x0, y0, u0, v0, cTL}, {x1, y0, u1, v0, cTR}, {x1, y1, u1, v1, cBR},
      {x0, y0, u0, v0, cTL}, {x1, y1, u1, v1, cBR}, {x0, y1, u0, v1, cBL},
    };
    Append(Topo::Tris, srv ? srv : s_white.get(), q, 6);
  }

  void Render2D::Flush()
  {
    if (s_verts.empty())
    {
      s_verts.clear();
      s_cmds.clear();
      return;
    }

    auto* ctx = Core::GetD3DDeviceContext();

    // Grow the dynamic vertex buffer if this batch is bigger than any before it.
    if (s_verts.size() > s_vbCapacity || !s_vb)
    {
      s_vbCapacity = s_verts.size() + s_verts.size() / 2 + 256;
      s_vb = nullptr;
      D3D11_BUFFER_DESC vbd{};
      vbd.ByteWidth = static_cast<UINT>(s_vbCapacity * sizeof(Vertex));
      vbd.Usage = D3D11_USAGE_DYNAMIC;
      vbd.BindFlags = D3D11_BIND_VERTEX_BUFFER;
      vbd.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
      check_hresult(Core::GetD3DDevice()->CreateBuffer(&vbd, nullptr, s_vb.put()));
    }

    D3D11_MAPPED_SUBRESOURCE mapped;
    check_hresult(ctx->Map(s_vb.get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped));
    std::memcpy(mapped.pData, s_verts.data(), s_verts.size() * sizeof(Vertex));
    ctx->Unmap(s_vb.get(), 0);

    // Bind the target + a matching full viewport, unless the caller passed a null RTV
    // (Begin with rtv == nullptr) - then draw to whatever is already bound, e.g. the
    // off-screen canvas the gfx2d HUD batch has just bound via bindCanvasTarget.
    if (s_rtv)
    {
      ID3D11RenderTargetView* rtv = s_rtv;
      ctx->OMSetRenderTargets(1, &rtv, nullptr);

      D3D11_VIEWPORT vp{};
      vp.Width = static_cast<float>(s_width);
      vp.Height = static_cast<float>(s_height);
      vp.MaxDepth = 1.0f;
      ctx->RSSetViewports(1, &vp);
    }

    const UINT stride = sizeof(Vertex);
    const UINT offset = 0;
    ID3D11Buffer* vb = s_vb.get();
    ID3D11Buffer* cb = s_cb.get();
    ctx->IASetInputLayout(s_layout.get());
    ctx->IASetVertexBuffers(0, 1, &vb, &stride, &offset);
    ctx->VSSetConstantBuffers(0, 1, &cb);

    // b1 params for custom programs (text outline); the default program ignores it.
    {
      D3D11_MAPPED_SUBRESOURCE pm;
      check_hresult(ctx->Map(s_paramsCb.get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &pm));
      std::memcpy(pm.pData, s_params, sizeof(s_params));
      ctx->Unmap(s_paramsCb.get(), 0);
      ID3D11Buffer* pcb = s_paramsCb.get();
      ctx->PSSetConstantBuffers(1, 1, &pcb);
    }

    const float blendFactor[4] = {0, 0, 0, 0};
    ctx->OMSetBlendState(s_blend.get(), blendFactor, 0xFFFFFFFF);
    ctx->OMSetDepthStencilState(s_depth.get(), 0);
    ctx->RSSetState(s_raster.get());

    ID3D11SamplerState* sampler =
      (s_filter == D3D11_FILTER_MIN_MAG_MIP_POINT) ? s_samplerPoint.get() : s_samplerLinear.get();
    ctx->PSSetSamplers(0, 1, &sampler);

    ProgramId boundProgram = static_cast<ProgramId>(-1); // force a bind on the first command
    for (const Cmd& c : s_cmds)
    {
      if (c.program != boundProgram)
      {
        const Program& pr = s_programs[c.program < s_programs.size() ? c.program : DefaultProgram];
        ctx->VSSetShader(pr.vs.get(), nullptr, 0);
        ctx->PSSetShader(pr.ps.get(), nullptr, 0);
        boundProgram = c.program;
      }

      ctx->RSSetScissorRects(1, &c.scissor);

      const D3D11_PRIMITIVE_TOPOLOGY topo = (c.topo == Topo::Points) ? D3D11_PRIMITIVE_TOPOLOGY_POINTLIST
                                          : (c.topo == Topo::Lines)  ? D3D11_PRIMITIVE_TOPOLOGY_LINELIST
                                                                     : D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
      ctx->IASetPrimitiveTopology(topo);

      ID3D11ShaderResourceView* srv = c.srv;
      ctx->PSSetShaderResources(0, 1, &srv);
      ctx->Draw(c.count, c.start);
    }

    s_verts.clear();
    s_cmds.clear();
  }
}
