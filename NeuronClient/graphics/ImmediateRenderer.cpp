#include "pch.h"
#include "ImmediateRenderer.h"

// Compiled shader byte arrays (fxc /Fh /Vn). Generated into shaders/CompiledShaders
// by the NeuronClientShaders build target; that directory is on the include path.
#include "generic-coloredVS.h"
#include "generic-coloredPS.h"
#include "generic-texturedVS.h"
#include "generic-texturedPS.h"
#include "textVS.h"
#include "textPS.h"
#include "text-overlayVS.h"
#include "text-overlayPS.h"
#include "gui-windowVS.h"
#include "gui-windowPS.h"
#include "generic-colored-3dVS.h"
#include "generic-colored-3dPS.h"

using namespace Neuron::Graphics;

namespace
{
  // Upload data into a DYNAMIC constant buffer (WRITE_DISCARD).
  void uploadConstant(ID3D11Buffer* buffer, const void* data, size_t size)
  {
    auto* ctx = Core::GetD3DDeviceContext();
    D3D11_MAPPED_SUBRESOURCE mapped;
    check_hresult(ctx->Map(buffer, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped));
    memcpy(mapped.pData, data, size);
    ctx->Unmap(buffer, 0);
  }

  D3D11_BLEND alphaBlendFactor(D3D11_BLEND blend) noexcept
  {
    switch (blend)
    {
    case D3D11_BLEND_SRC_COLOR:
      return D3D11_BLEND_SRC_ALPHA;
    case D3D11_BLEND_INV_SRC_COLOR:
      return D3D11_BLEND_INV_SRC_ALPHA;
    case D3D11_BLEND_DEST_COLOR:
      return D3D11_BLEND_DEST_ALPHA;
    case D3D11_BLEND_INV_DEST_COLOR:
      return D3D11_BLEND_INV_DEST_ALPHA;
    default:
      return blend;
    }
  }

  com_ptr<ID3D11Buffer> createDynamicConstantBuffer(UINT byteWidth)
  {
    D3D11_BUFFER_DESC desc{};
    desc.ByteWidth = byteWidth;
    desc.Usage = D3D11_USAGE_DYNAMIC;
    desc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
    desc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
    com_ptr<ID3D11Buffer> buffer;
    check_hresult(Core::GetD3DDevice()->CreateBuffer(&desc, nullptr, buffer.put()));
    return buffer;
  }
}

void ImmediateRenderer::Startup()
{
  if (s_started)
    return;

  createDeviceResources();
  loadShaders();

  s_modelView.assign(1, XMMatrixIdentity());
  s_projection.assign(1, XMMatrixIdentity());

  // Fixed-function defaults that the generic shaders read.
  s_fog = {};
  s_alphaTest = {AlphaFunc::ALWAYS, 0.0f, 0, 0};
  s_texEnv.unit[0] = XMINT4(TexEnvMode::MODULATE, 0, 0, 0);
  s_texEnv.unit[1] = XMINT4(TexEnvMode::MODULATE, 0, 0, 0);

  s_perDraw = {};
  s_perDraw.color = {1.0f, 1.0f, 1.0f, 1.0f};

  s_lighting = {};
  s_lighting.materialShininess = 1.0f;
  s_lightingDirty = true;

  s_started = true;

  BeginFrame();
}

void ImmediateRenderer::Shutdown()
{
  s_blendCache.clear();
  s_depthCache.clear();
  s_rasterCache.clear();
  s_samplerCache.clear();
  s_textureRegistry.clear();
  s_vertexBuffer = nullptr;
  s_vertexBufferCapacity = 0;
  s_cbPerView = nullptr;
  s_cbFog = nullptr;
  s_cbAlphaTest = nullptr;
  s_cbTexEnv = nullptr;
  s_cbPerDraw = nullptr;
  s_coloredVS = nullptr;
  s_coloredPS = nullptr;
  s_texturedVS = nullptr;
  s_texturedPS = nullptr;
  s_textVS = nullptr;
  s_textPS = nullptr;
  s_textOverlayVS = nullptr;
  s_textOverlayPS = nullptr;
  s_guiWindowVS = nullptr;
  s_guiWindowPS = nullptr;
  s_colored3dVS = nullptr;
  s_colored3dPS = nullptr;
  s_cbLighting = nullptr;
  s_inputLayout = nullptr;
  s_started = false;
}

void ImmediateRenderer::createDeviceResources()
{
  s_cbPerView = createDynamicConstantBuffer(sizeof(PerViewConstants));
  s_cbFog = createDynamicConstantBuffer(sizeof(FogConstants));
  s_cbLighting = createDynamicConstantBuffer(sizeof(LightingConstants));
  s_cbAlphaTest = createDynamicConstantBuffer(sizeof(AlphaTestConstants));
  s_cbTexEnv = createDynamicConstantBuffer(sizeof(TexEnvConstants));
  s_cbPerDraw = createDynamicConstantBuffer(sizeof(PerDrawConstants));
}

void ImmediateRenderer::loadShaders()
{
  auto* device = Core::GetD3DDevice();

  check_hresult(device->CreateVertexShader(g_generic_coloredVS, sizeof(g_generic_coloredVS), nullptr, s_coloredVS.put()));
  check_hresult(device->CreatePixelShader(g_generic_coloredPS, sizeof(g_generic_coloredPS), nullptr, s_coloredPS.put()));
  check_hresult(device->CreateVertexShader(g_generic_texturedVS, sizeof(g_generic_texturedVS), nullptr, s_texturedVS.put()));
  check_hresult(device->CreatePixelShader(g_generic_texturedPS, sizeof(g_generic_texturedPS), nullptr, s_texturedPS.put()));
  check_hresult(device->CreateVertexShader(g_textVS, sizeof(g_textVS), nullptr, s_textVS.put()));
  check_hresult(device->CreatePixelShader(g_textPS, sizeof(g_textPS), nullptr, s_textPS.put()));
  check_hresult(device->CreateVertexShader(g_text_overlayVS, sizeof(g_text_overlayVS), nullptr, s_textOverlayVS.put()));
  check_hresult(device->CreatePixelShader(g_text_overlayPS, sizeof(g_text_overlayPS), nullptr, s_textOverlayPS.put()));
  check_hresult(device->CreateVertexShader(g_gui_windowVS, sizeof(g_gui_windowVS), nullptr, s_guiWindowVS.put()));
  check_hresult(device->CreatePixelShader(g_gui_windowPS, sizeof(g_gui_windowPS), nullptr, s_guiWindowPS.put()));
  check_hresult(device->CreateVertexShader(g_generic_colored_3dVS, sizeof(g_generic_colored_3dVS), nullptr, s_colored3dVS.put()));
  check_hresult(device->CreatePixelShader(g_generic_colored_3dPS, sizeof(g_generic_colored_3dPS), nullptr, s_colored3dPS.put()));

  // One input layout shared by every immediate shader (matches ImmediateVertex).
  const D3D11_INPUT_ELEMENT_DESC layout[] = {
    {"POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0},
    {"NORMAL", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 12, D3D11_INPUT_PER_VERTEX_DATA, 0},
    {"COLOR", 0, DXGI_FORMAT_R8G8B8A8_UNORM, 0, 24, D3D11_INPUT_PER_VERTEX_DATA, 0},
    {"TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 28, D3D11_INPUT_PER_VERTEX_DATA, 0},
    {"TEXCOORD", 1, DXGI_FORMAT_R32G32_FLOAT, 0, 36, D3D11_INPUT_PER_VERTEX_DATA, 0},
  };
  check_hresult(device->CreateInputLayout(layout, static_cast<UINT>(std::size(layout)), g_generic_coloredVS,
                                          sizeof(g_generic_coloredVS), s_inputLayout.put()));
}

// ----- Frame lifecycle --------------------------------------------------------

void ImmediateRenderer::BeginFrame()
{
  auto* ctx = Core::GetD3DDeviceContext();

  ID3D11RenderTargetView* rtv = Core::GetRenderTargetView();
  ID3D11DepthStencilView* dsv = Core::GetDepthStencilView();
  ctx->OMSetRenderTargets(1, &rtv, dsv);

  D3D11_VIEWPORT viewport = Core::GetScreenViewport();
  ctx->RSSetViewports(1, &viewport);

  ctx->ClearRenderTargetView(rtv, s_clearColor);
  if (dsv)
    ctx->ClearDepthStencilView(dsv, D3D11_CLEAR_DEPTH | D3D11_CLEAR_STENCIL, 1.0f, 0);
}

void ImmediateRenderer::SetClearColor(float r, float g, float b, float a) noexcept
{
  s_clearColor[0] = r;
  s_clearColor[1] = g;
  s_clearColor[2] = b;
  s_clearColor[3] = a;
}

void ImmediateRenderer::drawSmokeTest()
{
  // Identity transforms -> vertices are already in clip space; no depth/cull so the
  // triangle always shows. Distinct per-vertex colors prove the color path too.
  SetMatrixMode(MatrixStackId::Projection);
  LoadIdentity();
  SetMatrixMode(MatrixStackId::ModelView);
  LoadIdentity();
  SetBlendEnabled(false);
  SetDepthTestEnabled(false);
  SetCullEnabled(false);
  BindTexture(0, nullptr);

  Begin(Primitive::Triangles);
  Color(1.0f, 0.0f, 0.0f, 1.0f);
  Vertex(0.0f, 0.5f, 0.0f);
  Color(0.0f, 1.0f, 0.0f, 1.0f);
  Vertex(-0.5f, -0.5f, 0.0f);
  Color(0.0f, 0.0f, 1.0f, 1.0f);
  Vertex(0.5f, -0.5f, 0.0f);
  End();
}

void ImmediateRenderer::Present()
{
  if (s_smokeTest)
    drawSmokeTest();

  Core::Present();
  BeginFrame();
}

void ImmediateRenderer::OnWindowSizeChanged()
{
  if (s_started)
    BeginFrame();
}

// ----- Immediate-mode geometry ------------------------------------------------

void ImmediateRenderer::Begin(Primitive mode)
{
  s_primitive = mode;
  s_spanVertices.clear();
  s_inBegin = true;
}

void ImmediateRenderer::End()
{
  s_inBegin = false;
  flush();
  s_spanVertices.clear();
}

void ImmediateRenderer::Vertex(float x, float y, float z)
{
  s_current.x = x;
  s_current.y = y;
  s_current.z = z;
  s_spanVertices.push_back(s_current);
}

void ImmediateRenderer::Normal(float x, float y, float z) noexcept
{
  s_current.nx = x;
  s_current.ny = y;
  s_current.nz = z;
}

void ImmediateRenderer::Color(float r, float g, float b, float a) noexcept
{
  auto toByte = [](float c) { return static_cast<uint8_t>(std::clamp(c, 0.0f, 1.0f) * 255.0f + 0.5f); };
  s_current.color = PackColor(toByte(r), toByte(g), toByte(b), toByte(a));
}

void ImmediateRenderer::TexCoord(float u, float v) noexcept
{
  s_current.u = u;
  s_current.v = v;
}

void ImmediateRenderer::TexCoord1(float u, float v) noexcept
{
  s_current.u2 = u;
  s_current.v2 = v;
}

void ImmediateRenderer::SetDrawColor(float r, float g, float b, float a) noexcept { s_perDraw.color = {r, g, b, a}; }

void ImmediateRenderer::SetGradientColors(float edgeR, float edgeG, float edgeB, float edgeA, float centerR, float centerG,
                                          float centerB, float centerA) noexcept
{
  s_perDraw.colorEdge = {edgeR, edgeG, edgeB, edgeA};
  s_perDraw.colorCenter = {centerR, centerG, centerB, centerA};
}

void ImmediateRenderer::buildDrawList(D3D11_PRIMITIVE_TOPOLOGY& topology, std::vector<ImmediateVertex>& out)
{
  const auto& v = s_spanVertices;
  const size_t n = v.size();

  switch (s_primitive)
  {
  case Primitive::Points:
    topology = D3D11_PRIMITIVE_TOPOLOGY_POINTLIST;
    out = v;
    break;

  case Primitive::Lines:
    topology = D3D11_PRIMITIVE_TOPOLOGY_LINELIST;
    out = v;
    break;

  case Primitive::LineStrip:
    topology = D3D11_PRIMITIVE_TOPOLOGY_LINESTRIP;
    out = v;
    break;

  case Primitive::LineLoop:
    topology = D3D11_PRIMITIVE_TOPOLOGY_LINESTRIP;
    out = v;
    if (n > 1)
      out.push_back(v[0]); // close the loop
    break;

  case Primitive::Triangles:
    topology = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
    out = v;
    break;

  case Primitive::TriangleStrip:
  case Primitive::QuadStrip: // a quad strip has the same vertex order as a tri strip
    topology = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP;
    out = v;
    break;

  case Primitive::TriangleFan:
    topology = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
    if (n >= 3)
    {
      out.reserve((n - 2) * 3);
      for (size_t i = 1; i + 1 < n; ++i)
      {
        out.push_back(v[0]);
        out.push_back(v[i]);
        out.push_back(v[i + 1]);
      }
    }
    break;

  case Primitive::Quads:
    topology = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
    out.reserve((n / 4) * 6);
    for (size_t i = 0; i + 3 < n; i += 4)
    {
      out.push_back(v[i + 0]);
      out.push_back(v[i + 1]);
      out.push_back(v[i + 2]);
      out.push_back(v[i + 0]);
      out.push_back(v[i + 2]);
      out.push_back(v[i + 3]);
    }
    break;
  }
}

void ImmediateRenderer::ensureVertexCapacity(size_t vertexCount)
{
  if (vertexCount <= s_vertexBufferCapacity && s_vertexBuffer)
    return;

  size_t newCapacity = std::max<size_t>(vertexCount, s_vertexBufferCapacity ? s_vertexBufferCapacity * 2 : 2048);

  D3D11_BUFFER_DESC desc{};
  desc.ByteWidth = static_cast<UINT>(newCapacity * sizeof(ImmediateVertex));
  desc.Usage = D3D11_USAGE_DYNAMIC;
  desc.BindFlags = D3D11_BIND_VERTEX_BUFFER;
  desc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;

  s_vertexBuffer = nullptr;
  check_hresult(Core::GetD3DDevice()->CreateBuffer(&desc, nullptr, s_vertexBuffer.put()));
  s_vertexBufferCapacity = newCapacity;
}

void ImmediateRenderer::updateConstantBuffers()
{
  PerViewConstants perView{};
  const XMMATRIX view = s_modelView.back();
  const XMMATRIX projection = s_projection.back();
  XMStoreFloat4x4(&perView.view, view);
  XMStoreFloat4x4(&perView.projection, projection);
  XMStoreFloat4x4(&perView.viewProjection, XMMatrixMultiply(view, projection));
  perView.normalMatrix0 = {1.0f, 0.0f, 0.0f};
  perView.normalMatrix1 = {0.0f, 1.0f, 0.0f};
  perView.normalMatrix2 = {0.0f, 0.0f, 1.0f};
  uploadConstant(s_cbPerView.get(), &perView, sizeof(perView));

  uploadConstant(s_cbFog.get(), &s_fog, sizeof(s_fog));
  uploadConstant(s_cbAlphaTest.get(), &s_alphaTest, sizeof(s_alphaTest));
  uploadConstant(s_cbTexEnv.get(), &s_texEnv, sizeof(s_texEnv));
  uploadConstant(s_cbPerDraw.get(), &s_perDraw, sizeof(s_perDraw));

  // b6 is large; only re-upload when a light/material setter changed it.
  if (s_lightingDirty)
  {
    uploadConstant(s_cbLighting.get(), &s_lighting, sizeof(s_lighting));
    s_lightingDirty = false;
  }
}

void ImmediateRenderer::applyProgram()
{
  auto* ctx = Core::GetD3DDeviceContext();

  ctx->IASetInputLayout(s_inputLayout.get());

  // Resolve the program to a VS/PS pair. Generic auto-picks colored/textured from
  // the bound texture; the named programs are the dedicated 2D/GUI shaders.
  ID3D11VertexShader* vs = s_coloredVS.get();
  ID3D11PixelShader* ps = s_coloredPS.get();
  bool wantsTexture = false;

  switch (s_program)
  {
  case ShaderProgram::Generic:
    if (s_state.texture[0])
    {
      vs = s_texturedVS.get();
      ps = s_texturedPS.get();
      wantsTexture = true;
    }
    break;
  case ShaderProgram::Colored3D:
    vs = s_colored3dVS.get();
    ps = s_colored3dPS.get();
    break;
  case ShaderProgram::Text:
    vs = s_textVS.get();
    ps = s_textPS.get();
    wantsTexture = true;
    break;
  case ShaderProgram::TextOverlay:
    vs = s_textOverlayVS.get();
    ps = s_textOverlayPS.get();
    wantsTexture = true;
    break;
  case ShaderProgram::GuiWindow:
    vs = s_guiWindowVS.get();
    ps = s_guiWindowPS.get();
    wantsTexture = true;
    break;
  }

  ctx->VSSetShader(vs, nullptr, 0);
  ctx->PSSetShader(ps, nullptr, 0);

  ID3D11Buffer* cbView = s_cbPerView.get();
  ID3D11Buffer* cbFog = s_cbFog.get();
  ID3D11Buffer* cbAlpha = s_cbAlphaTest.get();
  ID3D11Buffer* cbTexEnv = s_cbTexEnv.get();
  ID3D11Buffer* cbPerDraw = s_cbPerDraw.get();
  ID3D11Buffer* cbLighting = s_cbLighting.get();
  ctx->VSSetConstantBuffers(ConstantRegister::PER_VIEW, 1, &cbView);
  ctx->VSSetConstantBuffers(ConstantRegister::FOG, 1, &cbFog);
  ctx->VSSetConstantBuffers(ConstantRegister::LIGHTING, 1, &cbLighting);
  ctx->PSSetConstantBuffers(ConstantRegister::FOG, 1, &cbFog);
  ctx->PSSetConstantBuffers(ConstantRegister::LIGHTING, 1, &cbLighting);
  ctx->PSSetConstantBuffers(ConstantRegister::ALPHA_TEST, 1, &cbAlpha);
  ctx->PSSetConstantBuffers(ConstantRegister::TEX_ENV, 1, &cbTexEnv);
  ctx->PSSetConstantBuffers(ConstantRegister::PER_DRAW, 1, &cbPerDraw);

  const float blendFactor[4] = {0.0f, 0.0f, 0.0f, 0.0f};
  ctx->OMSetBlendState(blendState(), blendFactor, 0xFFFFFFFF);
  ctx->OMSetDepthStencilState(depthState(), 0);
  ctx->RSSetState(rasterState());
  if (s_state.scissorEnabled)
    ctx->RSSetScissorRects(1, &s_state.scissorRect);

  if (wantsTexture)
  {
    ID3D11ShaderResourceView* srv = s_state.texture[0];
    ID3D11SamplerState* sampler = samplerState(0);
    ctx->PSSetShaderResources(0, 1, &srv);
    ctx->PSSetSamplers(0, 1, &sampler);
  }
}

void ImmediateRenderer::flush()
{
  if (s_spanVertices.empty())
    return;

  D3D11_PRIMITIVE_TOPOLOGY topology = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
  std::vector<ImmediateVertex> out;
  buildDrawList(topology, out);
  if (out.empty())
    return;

  ensureVertexCapacity(out.size());

  auto* ctx = Core::GetD3DDeviceContext();
  D3D11_MAPPED_SUBRESOURCE mapped;
  check_hresult(ctx->Map(s_vertexBuffer.get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped));
  memcpy(mapped.pData, out.data(), out.size() * sizeof(ImmediateVertex));
  ctx->Unmap(s_vertexBuffer.get(), 0);

  updateConstantBuffers();
  applyProgram();

  UINT stride = sizeof(ImmediateVertex);
  UINT offset = 0;
  ID3D11Buffer* vb = s_vertexBuffer.get();
  ctx->IASetVertexBuffers(0, 1, &vb, &stride, &offset);
  ctx->IASetPrimitiveTopology(topology);
  ctx->Draw(static_cast<UINT>(out.size()), 0);
}

// ----- Matrix stack -----------------------------------------------------------

void ImmediateRenderer::PushMatrix()
{
  auto& stack = (s_matrixMode == MatrixStackId::ModelView) ? s_modelView : s_projection;
  stack.push_back(stack.back());
}

void ImmediateRenderer::PopMatrix()
{
  auto& stack = (s_matrixMode == MatrixStackId::ModelView) ? s_modelView : s_projection;
  if (stack.size() > 1)
    stack.pop_back();
}

void ImmediateRenderer::LoadIdentity()
{
  auto& stack = (s_matrixMode == MatrixStackId::ModelView) ? s_modelView : s_projection;
  stack.back() = XMMatrixIdentity();
}

void ImmediateRenderer::LoadMatrix(const XMMATRIX& m)
{
  auto& stack = (s_matrixMode == MatrixStackId::ModelView) ? s_modelView : s_projection;
  stack.back() = m;
}

void ImmediateRenderer::MultMatrix(const XMMATRIX& m)
{
  // GL post-multiplies the current matrix (C' = C * M, column-vector), so the new
  // transform applies to the vertex first. In the row-vector convention that is a
  // pre-multiply: M_new = m * M_old.
  auto& stack = (s_matrixMode == MatrixStackId::ModelView) ? s_modelView : s_projection;
  stack.back() = XMMatrixMultiply(m, stack.back());
}

void ImmediateRenderer::Translate(float x, float y, float z) { MultMatrix(XMMatrixTranslation(x, y, z)); }

void ImmediateRenderer::Scale(float x, float y, float z) { MultMatrix(XMMatrixScaling(x, y, z)); }

void ImmediateRenderer::Rotate(float degrees, float x, float y, float z)
{
  XMVECTOR axis = XMVector3Normalize(XMVectorSet(x, y, z, 0.0f));
  MultMatrix(XMMatrixRotationAxis(axis, XMConvertToRadians(degrees)));
}

void ImmediateRenderer::Perspective(float fovYDegrees, float aspect, float zNear, float zFar)
{
  // Right-handed to match the original GL world; [0,1] clip depth (D3D).
  MultMatrix(XMMatrixPerspectiveFovRH(XMConvertToRadians(fovYDegrees), aspect, zNear, zFar));
}

void ImmediateRenderer::Ortho2D(float left, float right, float bottom, float top)
{
  MultMatrix(XMMatrixOrthographicOffCenterRH(left, right, bottom, top, -1.0f, 1.0f));
}

void ImmediateRenderer::Frustum(float left, float right, float bottom, float top, float zNear, float zFar)
{
  MultMatrix(XMMatrixPerspectiveOffCenterRH(left, right, bottom, top, zNear, zFar));
}

void ImmediateRenderer::LookAt(float eyeX, float eyeY, float eyeZ, float atX, float atY, float atZ, float upX, float upY,
                               float upZ)
{
  XMVECTOR eye = XMVectorSet(eyeX, eyeY, eyeZ, 1.0f);
  XMVECTOR at = XMVectorSet(atX, atY, atZ, 1.0f);
  XMVECTOR up = XMVectorSet(upX, upY, upZ, 0.0f);
  MultMatrix(XMMatrixLookAtRH(eye, at, up));
}

XMMATRIX ImmediateRenderer::GetMatrix(MatrixStackId mode) noexcept
{
  return (mode == MatrixStackId::ModelView) ? s_modelView.back() : s_projection.back();
}

// ----- Render state -----------------------------------------------------------

void ImmediateRenderer::SetBlendEnabled(bool enabled) noexcept { s_state.blendEnabled = enabled; }

void ImmediateRenderer::SetBlendFunc(D3D11_BLEND src, D3D11_BLEND dst) noexcept
{
  s_state.blendSrc = src;
  s_state.blendDst = dst;
}

void ImmediateRenderer::SetDepthTestEnabled(bool enabled) noexcept { s_state.depthTestEnabled = enabled; }

void ImmediateRenderer::SetDepthWriteEnabled(bool enabled) noexcept { s_state.depthWriteEnabled = enabled; }

void ImmediateRenderer::SetDepthFunc(D3D11_COMPARISON_FUNC func) noexcept { s_state.depthFunc = func; }

void ImmediateRenderer::SetCullEnabled(bool enabled) noexcept { s_state.cullEnabled = enabled; }

void ImmediateRenderer::SetCullMode(D3D11_CULL_MODE mode) noexcept { s_state.cullMode = mode; }

void ImmediateRenderer::SetFrontFaceCounterClockwise(bool ccw) noexcept { s_state.frontCounterClockwise = ccw; }

void ImmediateRenderer::SetScissorEnabled(bool enabled) noexcept { s_state.scissorEnabled = enabled; }

void ImmediateRenderer::SetScissorRect(int left, int top, int right, int bottom) noexcept
{
  s_state.scissorRect = {left, top, right, bottom};
}

void ImmediateRenderer::SetColorLogicOpXor(bool enabled) noexcept { s_state.logicOpXor = enabled; }

void ImmediateRenderer::SetAlphaTestEnabled(bool enabled) noexcept
{
  // Alpha test is a shader feature (ENABLE_ALPHA_TEST permutation). The CPU mirror
  // tracks intent; the permutation is selected when the lit/textured shaders land.
  s_alphaTest.function = enabled ? s_alphaTest.function : AlphaFunc::ALWAYS;
}

void ImmediateRenderer::SetAlphaFunc(int func, float ref) noexcept
{
  s_alphaTest.function = func;
  s_alphaTest.clampValue = ref;
}

void ImmediateRenderer::SetFogEnabled(bool enabled) noexcept { s_fog.enable = enabled ? 1 : 0; }

void ImmediateRenderer::SetFog(float r, float g, float b, float a, float start, float end) noexcept
{
  s_fog.color = {r, g, b, a};
  s_fog.start = start;
  s_fog.end = end;
}

void ImmediateRenderer::SetFogInPixelEffect(bool enabled) noexcept { s_fog.inPixelEffect = enabled ? 1 : 0; }

void ImmediateRenderer::SetLightingEnabled(bool enabled) noexcept
{
  s_lighting.lightingEnable = enabled ? 1 : 0;
  s_lightingDirty = true;
}

void ImmediateRenderer::SetLightEnabled(int index, bool enabled) noexcept
{
  if (index >= 0 && index < MAX_LIGHTS)
  {
    s_lighting.lightEnable[index].x = enabled ? 1 : 0;
    s_lightingDirty = true;
  }
}

void ImmediateRenderer::SetLightDirection(int index, float x, float y, float z) noexcept
{
  if (index >= 0 && index < MAX_LIGHTS)
  {
    // The lit shaders evaluate lighting in view space (the normal is transformed there),
    // and the fixed-function pipeline this replaces transformed GL_POSITION by the
    // modelview at submit time. Match that: bring the world-space direction into view
    // space via the current modelview (the camera view when lights are set up), so the
    // shading stays anchored in the world instead of pinned to the screen.
    XMVECTOR viewDir = XMVector3Normalize(XMVector3TransformNormal(XMVectorSet(x, y, z, 0.0f), s_modelView.back()));
    XMFLOAT4 stored;
    XMStoreFloat4(&stored, viewDir);
    stored.w = 0.0f;
    s_lighting.lightPos[index] = stored;
    s_lightingDirty = true;
  }
}

void ImmediateRenderer::SetLightDiffuse(int index, float r, float g, float b, float a) noexcept
{
  if (index >= 0 && index < MAX_LIGHTS)
  {
    s_lighting.lightDiffuse[index] = {r, g, b, a};
    s_lightingDirty = true;
  }
}

void ImmediateRenderer::SetLightAmbient(int index, float r, float g, float b, float a) noexcept
{
  if (index >= 0 && index < MAX_LIGHTS)
  {
    s_lighting.lightAmbient[index] = {r, g, b, a};
    s_lightingDirty = true;
  }
}

void ImmediateRenderer::SetLightSpecular(int index, float r, float g, float b, float a) noexcept
{
  if (index >= 0 && index < MAX_LIGHTS)
  {
    s_lighting.lightSpecular[index] = {r, g, b, a};
    s_lightingDirty = true;
  }
}

void ImmediateRenderer::SetMaterialShininess(float shininess) noexcept
{
  s_lighting.materialShininess = shininess;
  s_lightingDirty = true;
}

void ImmediateRenderer::SetSpecularEnabled(bool enabled) noexcept
{
  s_lighting.specularEnable = enabled ? 1 : 0;
  s_lightingDirty = true;
}

void ImmediateRenderer::BindTexture(unsigned unit, ID3D11ShaderResourceView* srv) noexcept
{
  if (unit < MAX_TEXTURE_UNITS)
    s_state.texture[unit] = srv;
}

void ImmediateRenderer::BindTexture(unsigned unit, int textureHandle) noexcept
{
  const auto it = s_textureRegistry.find(textureHandle);
  BindTexture(unit, it != s_textureRegistry.end() ? it->second.get() : nullptr);
}

void ImmediateRenderer::RegisterTexture(int textureHandle, ID3D11ShaderResourceView* srv)
{
  com_ptr<ID3D11ShaderResourceView> held;
  held.copy_from(srv);
  s_textureRegistry[textureHandle] = std::move(held);
}

void ImmediateRenderer::SetTexEnv(unsigned unit, int mode, int combineRGB) noexcept
{
  if (unit < MAX_TEXTURE_UNITS)
    s_texEnv.unit[unit] = XMINT4(mode, combineRGB, 0, 0);
}

void ImmediateRenderer::SetSampler(unsigned unit, D3D11_FILTER filter, D3D11_TEXTURE_ADDRESS_MODE address) noexcept
{
  if (unit < MAX_TEXTURE_UNITS)
  {
    s_state.samplerFilter[unit] = filter;
    s_state.samplerAddress[unit] = address;
  }
}

// ----- Pipeline-state object caches -------------------------------------------

// Whether the device supports the output-merger logic op (needed for XOR draw). Cached
// after the first query.
static bool LogicOpSupported()
{
  static int cached = -1;
  if (cached < 0)
  {
    cached = 0;
    com_ptr<ID3D11Device1> dev1;
    if (SUCCEEDED(Core::GetD3DDevice()->QueryInterface(IID_PPV_ARGS(dev1.put()))))
    {
      D3D11_FEATURE_DATA_D3D11_OPTIONS opt{};
      if (SUCCEEDED(Core::GetD3DDevice()->CheckFeatureSupport(D3D11_FEATURE_D3D11_OPTIONS, &opt, sizeof(opt))) &&
          opt.OutputMergerLogicOp)
        cached = 1;
    }
  }
  return cached == 1;
}

ID3D11BlendState* ImmediateRenderer::blendState()
{
  const bool xorOp = s_state.logicOpXor && LogicOpSupported();

  uint32_t key = (s_state.blendEnabled ? 1u : 0u) | (static_cast<uint32_t>(s_state.blendSrc) << 1) |
    (static_cast<uint32_t>(s_state.blendDst) << 6) | (xorOp ? (1u << 12) : 0u);

  auto it = s_blendCache.find(key);
  if (it != s_blendCache.end())
    return it->second.get();

  // XOR raster-op path: a D3D11.1 blend state with the logic op enabled (RGB write
  // mask only, so the canvas alpha stays opaque), used for the chart cross-hairs.
  if (xorOp)
  {
    com_ptr<ID3D11Device1> dev1;
    Core::GetD3DDevice()->QueryInterface(IID_PPV_ARGS(dev1.put()));
    D3D11_BLEND_DESC1 desc{};
    auto& rt = desc.RenderTarget[0];
    rt.LogicOpEnable = TRUE;
    rt.LogicOp = D3D11_LOGIC_OP_XOR;
    rt.RenderTargetWriteMask =
      D3D11_COLOR_WRITE_ENABLE_RED | D3D11_COLOR_WRITE_ENABLE_GREEN | D3D11_COLOR_WRITE_ENABLE_BLUE;
    com_ptr<ID3D11BlendState1> state1;
    check_hresult(dev1->CreateBlendState1(&desc, state1.put()));
    com_ptr<ID3D11BlendState> state = state1.as<ID3D11BlendState>();
    return s_blendCache.emplace(key, std::move(state)).first->second.get();
  }

  D3D11_BLEND_DESC desc{};
  auto& rt = desc.RenderTarget[0];
  rt.BlendEnable = s_state.blendEnabled;
  rt.SrcBlend = s_state.blendSrc;
  rt.DestBlend = s_state.blendDst;
  rt.BlendOp = D3D11_BLEND_OP_ADD;
  rt.SrcBlendAlpha = alphaBlendFactor(s_state.blendSrc);
  rt.DestBlendAlpha = alphaBlendFactor(s_state.blendDst);
  rt.BlendOpAlpha = D3D11_BLEND_OP_ADD;
  rt.RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;

  com_ptr<ID3D11BlendState> state;
  check_hresult(Core::GetD3DDevice()->CreateBlendState(&desc, state.put()));
  return s_blendCache.emplace(key, std::move(state)).first->second.get();
}

ID3D11DepthStencilState* ImmediateRenderer::depthState()
{
  uint32_t key = (s_state.depthTestEnabled ? 1u : 0u) | (s_state.depthWriteEnabled ? 2u : 0u) |
    (static_cast<uint32_t>(s_state.depthFunc) << 2);

  auto it = s_depthCache.find(key);
  if (it != s_depthCache.end())
    return it->second.get();

  D3D11_DEPTH_STENCIL_DESC desc{};
  desc.DepthEnable = s_state.depthTestEnabled;
  desc.DepthWriteMask = s_state.depthWriteEnabled ? D3D11_DEPTH_WRITE_MASK_ALL : D3D11_DEPTH_WRITE_MASK_ZERO;
  desc.DepthFunc = s_state.depthFunc;
  desc.StencilEnable = FALSE;

  com_ptr<ID3D11DepthStencilState> state;
  check_hresult(Core::GetD3DDevice()->CreateDepthStencilState(&desc, state.put()));
  return s_depthCache.emplace(key, std::move(state)).first->second.get();
}

ID3D11RasterizerState* ImmediateRenderer::rasterState()
{
  D3D11_CULL_MODE cull = s_state.cullEnabled ? s_state.cullMode : D3D11_CULL_NONE;
  uint32_t key = static_cast<uint32_t>(cull) | (s_state.frontCounterClockwise ? 4u : 0u) |
    (s_state.scissorEnabled ? 8u : 0u);

  auto it = s_rasterCache.find(key);
  if (it != s_rasterCache.end())
    return it->second.get();

  D3D11_RASTERIZER_DESC desc{};
  desc.FillMode = D3D11_FILL_SOLID;
  desc.CullMode = cull;
  desc.FrontCounterClockwise = s_state.frontCounterClockwise;
  desc.DepthClipEnable = TRUE;
  desc.ScissorEnable = s_state.scissorEnabled;

  com_ptr<ID3D11RasterizerState> state;
  check_hresult(Core::GetD3DDevice()->CreateRasterizerState(&desc, state.put()));
  return s_rasterCache.emplace(key, std::move(state)).first->second.get();
}

ID3D11SamplerState* ImmediateRenderer::samplerState(unsigned unit)
{
  uint32_t key = static_cast<uint32_t>(s_state.samplerFilter[unit]) | (static_cast<uint32_t>(s_state.samplerAddress[unit]) << 16);

  auto it = s_samplerCache.find(key);
  if (it != s_samplerCache.end())
    return it->second.get();

  D3D11_SAMPLER_DESC desc{};
  desc.Filter = s_state.samplerFilter[unit];
  desc.AddressU = s_state.samplerAddress[unit];
  desc.AddressV = s_state.samplerAddress[unit];
  desc.AddressW = s_state.samplerAddress[unit];
  desc.ComparisonFunc = D3D11_COMPARISON_NEVER;
  desc.MinLOD = 0.0f;
  desc.MaxLOD = D3D11_FLOAT32_MAX;

  com_ptr<ID3D11SamplerState> state;
  check_hresult(Core::GetD3DDevice()->CreateSamplerState(&desc, state.put()));
  return s_samplerCache.emplace(key, std::move(state)).first->second.get();
}
