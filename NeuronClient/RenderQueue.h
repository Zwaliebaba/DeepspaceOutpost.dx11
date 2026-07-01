#pragma once

// RenderQueue - the A1 "render seam".
//
// The faithfully ported game logic used to call the gfx_* contract (gfx.h)
// directly while it updated the world, fusing simulation and rendering. The
// MMO migration needs the simulation to be runnable headless (dedicated server,
// BotClient, golden-run tests), so the sim must stop drawing.
//
// RenderQueue is that boundary: during its render pass the game *records*
// drawing commands here; the platform layer later *replays* them into a
// RenderSink. The Direct3D 11 sink forwards each command to the existing gfx_*
// backend (pixel-identical); a null sink (headless) ignores them.
//
// This is a genuine abstraction (deferral, variable-length payload arenas,
// headless replay), not a thin wrapper around gfx_*, so it is consistent with
// the Native-First rule in .github/coding-standards.md.

#include <cstddef>
#include <cstdint>
#include <vector>

namespace Neuron::Render
{
  // The primitives the live flight/space render path emits (gfx.h). Field
  // meaning per type is documented on the matching RenderQueue recording method.
  enum class CommandType : uint8_t
  {
    Pixel,
    Line,
    ColourLine,
    Triangle,
    RenderLine,      // depth-keyed 2D line (gfx_render_line) - the laser bolt
    DrawModel,       // GPU 3D model instance (camera-space transform; see ModelDraw)
    StartRender,     // open the 3D scene batch (delimiter for the GPU Scene3D pass)
    FinishRender,    // flush the 3D scene batch (runs the Scene3D pass)
  };

  // A fixed-size command record. The DrawModel payload lives in the owning
  // RenderQueue's model arena, referenced by dataOffset; field meaning depends on
  // `type`.
  struct Command
  {
    CommandType type{};
    int x0{}, y0{}, x1{}, y1{}, x2{}, y2{};
    int colour{};
    int dist{};               // depth key for RenderLine
    uint32_t dataOffset{};    // DrawModel: index into the model arena
  };

  // A 3D model instance for the GPU scene pass - the successor to the retired
  // CPU-projected polygon stream. Instead of pre-projected 2D points, it
  // carries the object's identity and its camera-space transform (orientation
  // basis + position) plus presentation options; the client's Scene3D renderer
  // turns one of these into an indexed draw with a real perspective + z-buffer.
  // POD (no D3D, no game headers) so the seam stays portable and the sim stays
  // headless. Records live in the owning RenderQueue's model arena, referenced by
  // a DrawModel Command's dataOffset.
  struct ModelDraw
  {
    int      type = 0;        // legacy SHIP_* model id (also SHIP_PLANET / SHIP_SUN billboards)
    int      style = 0;       // render style: ships 0=solid/1=wireframe; planet = planet_render_style
    int      colour = -1;     // palette index: ships < 0 keep face colours; planet/sun primary colour
    int      colour2 = -1;    // secondary palette index (banded planet styles); < 0 if unused
    uint32_t flags = 0;       // legacy local_object flags (e.g. FLG_FIRING)
    double   location[3] = {};         // camera-space position (x right, y up, z forward)
    double   rotmat[3][3] = {};        // orientation basis: row 0 = side, 1 = roof, 2 = nose
    double   distance = 0.0;  // camera distance (LOD / tie-break)
  };

  // Consumer contract. A backend implements this to execute recorded commands;
  // the method set mirrors the RenderQueue recording API one-to-one. The D3D11
  // platform sink forwards to gfx_*; the null sink (headless) is all no-ops.
  // Text pointers are NUL-terminated and only valid for the duration of the call.
  class RenderSink
  {
  public:
    virtual ~RenderSink() = default;

    virtual void Pixel(int _x, int _y, int _colour) = 0;
    virtual void Line(int _x0, int _y0, int _x1, int _y1) = 0;
    virtual void ColourLine(int _x0, int _y0, int _x1, int _y1, int _colour) = 0;
    virtual void Triangle(int _x0, int _y0, int _x1, int _y1, int _x2, int _y2, int _colour) = 0;
    virtual void RenderLine(int _x0, int _y0, int _x1, int _y1, int _dist, int _colour) = 0;
    virtual void DrawModel(const ModelDraw& _model) = 0;
    virtual void StartRender() = 0;
    virtual void FinishRender() = 0;
  };

  // A sink that discards everything - for the headless dedicated server, the
  // BotClient, and golden-run tests, where the simulation still "renders" into a
  // queue but nothing is drawn.
  class NullRenderSink final : public RenderSink
  {
  public:
    void Pixel(int, int, int) override {}
    void Line(int, int, int, int) override {}
    void ColourLine(int, int, int, int, int) override {}
    void Triangle(int, int, int, int, int, int, int) override {}
    void RenderLine(int, int, int, int, int, int) override {}
    void DrawModel(const ModelDraw&) override {}
    void StartRender() override {}
    void FinishRender() override {}
  };

  // Records draw commands during the sim's render pass, then replays them in
  // record order into a RenderSink. Owns the deferral and the variable-length
  // payload arenas; reused across frames via Clear().
  class RenderQueue
  {
  public:
    void Clear();

    // Recording API - mirrors the gfx.h primitives the flight/space path emits.
    void Pixel(int _x, int _y, int _colour);
    void Line(int _x0, int _y0, int _x1, int _y1);
    void ColourLine(int _x0, int _y0, int _x1, int _y1, int _colour);
    void Triangle(int _x0, int _y0, int _x1, int _y1, int _x2, int _y2, int _colour);
    void RenderLine(int _x0, int _y0, int _x1, int _y1, int _dist, int _colour);
    void DrawModel(const ModelDraw& _model);
    void StartRender();
    void FinishRender();

    // Replay every recorded command, in order, into the sink.
    void Replay(RenderSink& _sink) const;

    [[nodiscard]] std::size_t Size() const { return m_commands.size(); }
    [[nodiscard]] bool Empty() const { return m_commands.empty(); }

  private:
    Command& Push(CommandType _type);

    std::vector<Command> m_commands;
    std::vector<ModelDraw> m_models; // arena: 3D model instances (DrawModel)
  };
}
