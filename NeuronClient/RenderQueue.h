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
#include <string_view>
#include <vector>

namespace Neuron::Render
{
  // The primitives the live flight/space render path emits (gfx.h). Field
  // meaning per type is documented on the matching RenderQueue recording method.
  enum class CommandType : uint8_t
  {
    Pixel,
    FastPixel,
    Line,
    ColourLine,
    Circle,
    FilledCircle,
    Triangle,
    RenderPolygon,   // depth-sorted 3D polygon (gfx_render_polygon)
    RenderLine,      // depth-sorted 3D line (gfx_render_line)
    DrawModel,       // GPU 3D model instance (camera-space transform; see ModelDraw)
    Sprite,
    Text,
    CentreText,
    SetClipRegion,
    ClearArea,
    DrawScanner,
    StartRender,     // open the depth-sorted 3D batch
    FinishRender,    // flush the depth-sorted 3D batch
  };

  // A fixed-size command record. Variable-length payloads (3D polygon point
  // lists, text) live in the owning RenderQueue's side arenas and are referenced
  // by (offset, count); field meaning depends on `type`.
  struct Command
  {
    CommandType type{};
    int x0{}, y0{}, x1{}, y1{}, x2{}, y2{};
    int colour{};
    int radius{};
    int dist{};               // depth key for RenderPolygon / RenderLine
    int param{};              // sprite id / centre-text point size / 3D point count
    uint32_t dataOffset{};    // start index into the int arena (polygon points)
    uint32_t dataCount{};     // number of ints in the polygon point list
    uint32_t textOffset{};    // start index into the char arena
    uint32_t textCount{};     // text length (excluding the trailing '\0')
  };

  // A 3D model instance for the GPU scene pass - the successor to the
  // CPU-projected RenderPolygon stream. Instead of pre-projected 2D points, it
  // carries the object's identity and its camera-space transform (orientation
  // basis + position) plus presentation options; the client's Scene3D renderer
  // turns one of these into an indexed draw with a real perspective + z-buffer.
  // POD (no D3D, no game headers) so the seam stays portable and the sim stays
  // headless. Records live in the owning RenderQueue's model arena, referenced by
  // a DrawModel Command's dataOffset.
  struct ModelDraw
  {
    int      type = 0;        // legacy SHIP_* model id
    int      style = 0;       // render style: 0 = solid, 1 = wireframe
    int      colour = -1;     // palette override; < 0 keeps the model's own face colours
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
    virtual void FastPixel(int _x, int _y, int _colour) = 0;
    virtual void Line(int _x0, int _y0, int _x1, int _y1) = 0;
    virtual void ColourLine(int _x0, int _y0, int _x1, int _y1, int _colour) = 0;
    virtual void Circle(int _cx, int _cy, int _radius, int _colour) = 0;
    virtual void FilledCircle(int _cx, int _cy, int _radius, int _colour) = 0;
    virtual void Triangle(int _x0, int _y0, int _x1, int _y1, int _x2, int _y2, int _colour) = 0;
    virtual void RenderPolygon(int _numPoints, const int* _points, int _colour, int _dist) = 0;
    virtual void RenderLine(int _x0, int _y0, int _x1, int _y1, int _dist, int _colour) = 0;
    virtual void DrawModel(const ModelDraw& _model) = 0;
    virtual void Sprite(int _spriteId, int _x, int _y) = 0;
    virtual void Text(int _x, int _y, const char* _text) = 0;
    virtual void CentreText(int _y, const char* _text, int _pointSize, int _colour) = 0;
    virtual void SetClipRegion(int _tx, int _ty, int _bx, int _by) = 0;
    virtual void ClearArea(int _tx, int _ty, int _bx, int _by) = 0;
    virtual void DrawScanner() = 0;
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
    void FastPixel(int, int, int) override {}
    void Line(int, int, int, int) override {}
    void ColourLine(int, int, int, int, int) override {}
    void Circle(int, int, int, int) override {}
    void FilledCircle(int, int, int, int) override {}
    void Triangle(int, int, int, int, int, int, int) override {}
    void RenderPolygon(int, const int*, int, int) override {}
    void RenderLine(int, int, int, int, int, int) override {}
    void DrawModel(const ModelDraw&) override {}
    void Sprite(int, int, int) override {}
    void Text(int, int, const char*) override {}
    void CentreText(int, const char*, int, int) override {}
    void SetClipRegion(int, int, int, int) override {}
    void ClearArea(int, int, int, int) override {}
    void DrawScanner() override {}
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
    void FastPixel(int _x, int _y, int _colour);
    void Line(int _x0, int _y0, int _x1, int _y1);
    void ColourLine(int _x0, int _y0, int _x1, int _y1, int _colour);
    void Circle(int _cx, int _cy, int _radius, int _colour);
    void FilledCircle(int _cx, int _cy, int _radius, int _colour);
    void Triangle(int _x0, int _y0, int _x1, int _y1, int _x2, int _y2, int _colour);
    void RenderPolygon(int _numPoints, const int* _points, int _intCount, int _colour, int _dist);
    void RenderLine(int _x0, int _y0, int _x1, int _y1, int _dist, int _colour);
    void DrawModel(const ModelDraw& _model);
    void Sprite(int _spriteId, int _x, int _y);
    void Text(int _x, int _y, std::string_view _text);
    void CentreText(int _y, std::string_view _text, int _pointSize, int _colour);
    void SetClipRegion(int _tx, int _ty, int _bx, int _by);
    void ClearArea(int _tx, int _ty, int _bx, int _by);
    void DrawScanner();
    void StartRender();
    void FinishRender();

    // Replay every recorded command, in order, into the sink.
    void Replay(RenderSink& _sink) const;

    [[nodiscard]] std::size_t Size() const { return m_commands.size(); }
    [[nodiscard]] bool Empty() const { return m_commands.empty(); }

  private:
    Command& Push(CommandType _type);
    uint32_t AppendInts(const int* _values, int _count);
    uint32_t AppendText(std::string_view _text);

    std::vector<Command> m_commands;
    std::vector<int> m_points;       // arena: 3D polygon point lists
    std::vector<char> m_text;        // arena: NUL-terminated text bytes
    std::vector<ModelDraw> m_models; // arena: 3D model instances (DrawModel)
  };
}
