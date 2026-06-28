#pragma once

#include "GraphicsCore.h"
#include "ConstantBuffers.h"

#include <vector>
#include <unordered_map>

// Native Direct3D 11 immediate-mode renderer.
//
// This is the replacement for the deleted OpenGL-on-D3D9 layer: a genuine
// fixed-function-style pipeline implemented on top of Neuron::Graphics::Core and the
// HLSL shaders in NeuronGame/Shaders. Game code drives it with begin/vertex/end plus
// matrix and render-state calls (the native successors to glBegin/glVertex/glEnd,
// the matrix stack, glBlendFunc, glEnable, ...). It is not a wrapper: it batches
// geometry, owns the matrix/light/fog/texenv state, selects shader permutations, and
// builds and caches the pipeline-state objects.
//
// Modelled on Graphics::Core's all-static lifetime so the two siblings match.

namespace Neuron::Graphics
{
  // One interleaved immediate vertex; mirrors VSInput in immediate-vertex.hlsli.
  // Color is RGBA8 (R in the low byte) consumed as DXGI_FORMAT_R8G8B8A8_UNORM, which
  // is guaranteed-supported as an input-assembler vertex format.
  struct ImmediateVertex
  {
    float x, y, z;
    float nx, ny, nz;
    uint32_t color;
    float u, v;
    float u2, v2;
  };
  static_assert(sizeof(ImmediateVertex) == 44, "ImmediateVertex must match the input layout / VSInput");

  // Primitive modes. Quads, QuadStrip, TriangleFan and LineLoop have no D3D11
  // topology and are expanded on the CPU during a flush, as the old layer did.
  enum class Primitive
  {
    Points,
    Lines,
    LineLoop,
    LineStrip,
    Triangles,
    TriangleStrip,
    TriangleFan,
    Quads,
    QuadStrip,
  };

  enum class MatrixStackId
  {
    ModelView,
    Projection,
  };

  // Selects which shader program the immediate geometry flushes through (the engine's
  // glUseProgram equivalent). Generic auto-picks colored/textured from the bound
  // texture; the named programs are the dedicated 2D/GUI shaders.
  enum class ShaderProgram
  {
    Generic,     // auto colored/textured from the bound texture (2D and unlit 3D)
    Colored3D,   // lit, coloured 3D primitives (Shape models); uses normals + b6
    Text,        // world-space text (u_Color * glyph * 4)
    TextOverlay, // screen-space text (u_Color * vertexColor * glyph, crisp)
    GuiWindow,   // GUI panels (vertexColor * interface texture)
  };

  class ImmediateRenderer
  {
    public:
      static void Startup();
      static void Shutdown();

      // --- Frame lifecycle ---------------------------------------------------
      // Bind Core's back buffer + depth buffer and clear them. Called at startup
      // and again after each Present so every frame begins clean.
      static void BeginFrame();
      static void SetClearColor(float r, float g, float b, float a) noexcept;
      // Flush pending geometry, present via Core, then begin the next frame.
      static void Present();
      // Recreate window-size-dependent bindings after Core resized the swap chain.
      static void OnWindowSizeChanged();

      // P1 bring-up: draw a colored triangle every frame so a local build visibly
      // proves the native DX11 pipe (device + shaders + batching + present). Remove
      // once real content renders through the renderer.
      static void SetSmokeTestEnabled(bool enabled) noexcept { s_smokeTest = enabled; }

      // --- Immediate-mode geometry ------------------------------------------
      // Bind the shader program used by subsequent draws (sticky, like glUseProgram).
      static void UseProgram(ShaderProgram program) noexcept { s_program = program; }
      // Per-draw tint (b9 u_Color), consumed by the Text / TextOverlay programs.
      static void SetDrawColor(float r, float g, float b, float a) noexcept;
      // Window-background gradient colours (b9), consumed by a window-background program.
      static void SetGradientColors(float edgeR, float edgeG, float edgeB, float edgeA, float centerR, float centerG,
                                    float centerB, float centerA) noexcept;

      static void Begin(Primitive mode);
      static void End();
      static void Vertex(float x, float y, float z);
      static void Vertex(float x, float y) { Vertex(x, y, 0.0f); }
      static void Normal(float x, float y, float z) noexcept;
      static void Color(uint32_t rgba) noexcept { s_current.color = rgba; }
      static void Color(float r, float g, float b, float a) noexcept;
      static void ColorBytes(uint8_t r, uint8_t g, uint8_t b, uint8_t a) noexcept { s_current.color = PackColor(r, g, b, a); }
      static void TexCoord(float u, float v) noexcept;
      static void TexCoord1(float u, float v) noexcept;

      // Pack RGBA bytes for DXGI_FORMAT_R8G8B8A8_UNORM (R in the low byte).
      static uint32_t PackColor(uint8_t r, uint8_t g, uint8_t b, uint8_t a) noexcept
      {
        return static_cast<uint32_t>(r) | (static_cast<uint32_t>(g) << 8) | (static_cast<uint32_t>(b) << 16) |
          (static_cast<uint32_t>(a) << 24);
      }

      // --- Matrix stack (DirectXMath row-vector; uploaded row-major) ---------
      static void SetMatrixMode(MatrixStackId mode) noexcept { s_matrixMode = mode; }
      static void PushMatrix();
      static void PopMatrix();
      static void LoadIdentity();
      static void LoadMatrix(const XMMATRIX& m);
      static void MultMatrix(const XMMATRIX& m);     // pre-multiply, matching GL semantics
      static void Translate(float x, float y, float z);
      static void Scale(float x, float y, float z);
      static void Rotate(float degrees, float x, float y, float z);
      static void Perspective(float fovYDegrees, float aspect, float zNear, float zFar);
      static void Ortho2D(float left, float right, float bottom, float top);
      static void Frustum(float left, float right, float bottom, float top, float zNear, float zFar);
      static void LookAt(float eyeX, float eyeY, float eyeZ, float atX, float atY, float atZ, float upX, float upY, float upZ);
      static XMMATRIX GetMatrix(MatrixStackId mode) noexcept;

      // --- Render state ------------------------------------------------------
      static void SetBlendEnabled(bool enabled) noexcept;
      static void SetBlendFunc(D3D11_BLEND src, D3D11_BLEND dst) noexcept;
      static void SetDepthTestEnabled(bool enabled) noexcept;
      static void SetDepthWriteEnabled(bool enabled) noexcept;
      static void SetDepthFunc(D3D11_COMPARISON_FUNC func) noexcept;
      static void SetCullEnabled(bool enabled) noexcept;
      static void SetCullMode(D3D11_CULL_MODE mode) noexcept;
      static void SetFrontFaceCounterClockwise(bool ccw) noexcept;

      static void SetAlphaTestEnabled(bool enabled) noexcept;
      static void SetAlphaFunc(int func, float ref) noexcept;

      static void SetFogEnabled(bool enabled) noexcept;
      static void SetFog(float r, float g, float b, float a, float start, float end) noexcept;
      static void SetFogInPixelEffect(bool enabled) noexcept;

      // Fixed-function directional lighting, consumed by the Colored3D program.
      static void SetLightingEnabled(bool enabled) noexcept;
      static void SetLightEnabled(int index, bool enabled) noexcept;
      static void SetLightDirection(int index, float x, float y, float z) noexcept;
      static void SetLightDiffuse(int index, float r, float g, float b, float a) noexcept;
      static void SetLightAmbient(int index, float r, float g, float b, float a) noexcept;
      static void SetLightSpecular(int index, float r, float g, float b, float a) noexcept;
      static void SetMaterialShininess(float shininess) noexcept;
      // Gates the lighting specular term (D3D9 D3DRS_SPECULARENABLE analog). Off by default.
      static void SetSpecularEnabled(bool enabled) noexcept;

      // Bind a texture (or nullptr) to a unit and configure its environment/sampler.
      static void BindTexture(unsigned unit, ID3D11ShaderResourceView* srv) noexcept;
      // Bind by integer handle (as returned by Resource::GetTexture / ConvertToTexture).
      static void BindTexture(unsigned unit, int textureHandle) noexcept;
      // Associate an integer texture handle with its shader-resource view. Called by the
      // texture loaders so handle-based call sites resolve to the native SRV.
      static void RegisterTexture(int textureHandle, ID3D11ShaderResourceView* srv);
      static void SetTexEnv(unsigned unit, int mode, int combineRGB) noexcept;
      static void SetSampler(unsigned unit, D3D11_FILTER filter, D3D11_TEXTURE_ADDRESS_MODE address) noexcept;

      static ID3D11DeviceContext1* Context() noexcept { return Core::GetD3DDeviceContext(); }

    private:
      static constexpr unsigned MAX_TEXTURE_UNITS = 2;

      static void drawSmokeTest();
      // Submit the accumulated vertices for the current Begin/End span.
      static void flush();
      // Expand the current primitive's vertices into a triangle/line list and
      // append the chosen D3D11 topology's vertices into the upload buffer.
      static void buildDrawList(D3D11_PRIMITIVE_TOPOLOGY& topology, std::vector<ImmediateVertex>& out);
      static void ensureVertexCapacity(size_t vertexCount);
      static void updateConstantBuffers();
      static void applyProgram();

      static ID3D11BlendState* blendState();
      static ID3D11DepthStencilState* depthState();
      static ID3D11RasterizerState* rasterState();
      static ID3D11SamplerState* samplerState(unsigned unit);

      static void createDeviceResources();
      static void loadShaders();

      // Compiled shaders + the single shared input layout.
      inline static com_ptr<ID3D11VertexShader> s_coloredVS;
      inline static com_ptr<ID3D11PixelShader> s_coloredPS;
      inline static com_ptr<ID3D11VertexShader> s_texturedVS;
      inline static com_ptr<ID3D11PixelShader> s_texturedPS;
      inline static com_ptr<ID3D11VertexShader> s_textVS;
      inline static com_ptr<ID3D11PixelShader> s_textPS;
      inline static com_ptr<ID3D11VertexShader> s_textOverlayVS;
      inline static com_ptr<ID3D11PixelShader> s_textOverlayPS;
      inline static com_ptr<ID3D11VertexShader> s_guiWindowVS;
      inline static com_ptr<ID3D11PixelShader> s_guiWindowPS;
      inline static com_ptr<ID3D11VertexShader> s_colored3dVS;
      inline static com_ptr<ID3D11PixelShader> s_colored3dPS;
      inline static com_ptr<ID3D11InputLayout> s_inputLayout;

      // Constant buffers (b0/b3/b6/b7/b8/b9).
      inline static com_ptr<ID3D11Buffer> s_cbPerView;
      inline static com_ptr<ID3D11Buffer> s_cbFog;
      inline static com_ptr<ID3D11Buffer> s_cbLighting;
      inline static com_ptr<ID3D11Buffer> s_cbAlphaTest;
      inline static com_ptr<ID3D11Buffer> s_cbTexEnv;
      inline static com_ptr<ID3D11Buffer> s_cbPerDraw;

      // Dynamic vertex buffer (grown on demand).
      inline static com_ptr<ID3D11Buffer> s_vertexBuffer;
      inline static size_t s_vertexBufferCapacity = 0;

      // Pipeline-state object caches, keyed by a packed description.
      inline static std::unordered_map<uint32_t, com_ptr<ID3D11BlendState>> s_blendCache;
      inline static std::unordered_map<uint32_t, com_ptr<ID3D11DepthStencilState>> s_depthCache;
      inline static std::unordered_map<uint32_t, com_ptr<ID3D11RasterizerState>> s_rasterCache;
      inline static std::unordered_map<uint32_t, com_ptr<ID3D11SamplerState>> s_samplerCache;

      // Integer texture handle -> shader-resource view, populated by the texture loaders.
      inline static std::unordered_map<int, com_ptr<ID3D11ShaderResourceView>> s_textureRegistry;

      // CPU-side render state.
      struct State
      {
        bool blendEnabled = false;
        D3D11_BLEND blendSrc = D3D11_BLEND_SRC_ALPHA;
        D3D11_BLEND blendDst = D3D11_BLEND_INV_SRC_ALPHA;

        bool depthTestEnabled = true;
        bool depthWriteEnabled = true;
        D3D11_COMPARISON_FUNC depthFunc = D3D11_COMPARISON_LESS_EQUAL;

        bool cullEnabled = true;
        D3D11_CULL_MODE cullMode = D3D11_CULL_BACK;
        bool frontCounterClockwise = true; // GL default is GL_CCW

        D3D11_FILTER samplerFilter[MAX_TEXTURE_UNITS] = {D3D11_FILTER_MIN_MAG_MIP_LINEAR, D3D11_FILTER_MIN_MAG_MIP_LINEAR};
        D3D11_TEXTURE_ADDRESS_MODE samplerAddress[MAX_TEXTURE_UNITS] = {D3D11_TEXTURE_ADDRESS_CLAMP, D3D11_TEXTURE_ADDRESS_CLAMP};
        ID3D11ShaderResourceView* texture[MAX_TEXTURE_UNITS] = {};
      };
      inline static State s_state;

      // Constant-buffer mirrors (uploaded on flush when dirty).
      inline static FogConstants s_fog{};
      inline static AlphaTestConstants s_alphaTest{};
      inline static TexEnvConstants s_texEnv{};
      inline static PerDrawConstants s_perDraw{};
      inline static LightingConstants s_lighting{};
      inline static bool s_lightingDirty = true;

      // Matrix stacks (row-vector). back() is the current matrix.
      inline static std::vector<XMMATRIX> s_modelView;
      inline static std::vector<XMMATRIX> s_projection;
      inline static MatrixStackId s_matrixMode = MatrixStackId::ModelView;

      // Current vertex attributes + the open Begin/End span.
      inline static ImmediateVertex s_current{};
      inline static Primitive s_primitive = Primitive::Triangles;
      inline static ShaderProgram s_program = ShaderProgram::Generic;
      inline static std::vector<ImmediateVertex> s_spanVertices;
      inline static bool s_inBegin = false;

      inline static float s_clearColor[4] = {0.05f, 0.0f, 0.05f, 1.0f};
      inline static bool s_started = false;
      inline static bool s_smokeTest = false; // P1 pipe verified; real content now renders
  };
}
