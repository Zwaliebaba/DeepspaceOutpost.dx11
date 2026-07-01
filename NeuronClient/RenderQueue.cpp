#include "pch.h"

#include "RenderQueue.h"

namespace Neuron::Render
{
  void RenderQueue::Clear()
  {
    // Keep the allocated capacity; we refill every frame.
    m_commands.clear();
    m_models.clear();
  }

  Command& RenderQueue::Push(CommandType _type)
  {
    Command& cmd = m_commands.emplace_back();
    cmd.type = _type;
    return cmd;
  }

  void RenderQueue::Pixel(int _x, int _y, int _colour)
  {
    Command& c = Push(CommandType::Pixel);
    c.x0 = _x; c.y0 = _y; c.colour = _colour;
  }

  void RenderQueue::Line(int _x0, int _y0, int _x1, int _y1)
  {
    Command& c = Push(CommandType::Line);
    c.x0 = _x0; c.y0 = _y0; c.x1 = _x1; c.y1 = _y1;
  }

  void RenderQueue::ColourLine(int _x0, int _y0, int _x1, int _y1, int _colour)
  {
    Command& c = Push(CommandType::ColourLine);
    c.x0 = _x0; c.y0 = _y0; c.x1 = _x1; c.y1 = _y1; c.colour = _colour;
  }

  void RenderQueue::Triangle(int _x0, int _y0, int _x1, int _y1, int _x2, int _y2, int _colour)
  {
    Command& c = Push(CommandType::Triangle);
    c.x0 = _x0; c.y0 = _y0; c.x1 = _x1; c.y1 = _y1; c.x2 = _x2; c.y2 = _y2; c.colour = _colour;
  }

  void RenderQueue::RenderLine(int _x0, int _y0, int _x1, int _y1, int _dist, int _colour)
  {
    Command& c = Push(CommandType::RenderLine);
    c.x0 = _x0; c.y0 = _y0; c.x1 = _x1; c.y1 = _y1; c.dist = _dist; c.colour = _colour;
  }

  void RenderQueue::DrawModel(const ModelDraw& _model)
  {
    Command& c = Push(CommandType::DrawModel);
    // The fixed-size payload lives in the model arena; reference it by index.
    c.dataOffset = static_cast<uint32_t>(m_models.size());
    m_models.push_back(_model);
  }

  void RenderQueue::StartRender() { Push(CommandType::StartRender); }
  void RenderQueue::FinishRender() { Push(CommandType::FinishRender); }

  void RenderQueue::Replay(RenderSink& _sink) const
  {
    // The model arena is stable for the whole replay (no recording happens during
    // it), so resolving offsets to models here is safe.
    for (const Command& c : m_commands)
    {
      switch (c.type)
      {
        case CommandType::Pixel:        _sink.Pixel(c.x0, c.y0, c.colour); break;
        case CommandType::Line:         _sink.Line(c.x0, c.y0, c.x1, c.y1); break;
        case CommandType::ColourLine:   _sink.ColourLine(c.x0, c.y0, c.x1, c.y1, c.colour); break;
        case CommandType::Triangle:     _sink.Triangle(c.x0, c.y0, c.x1, c.y1, c.x2, c.y2, c.colour); break;
        case CommandType::RenderLine:   _sink.RenderLine(c.x0, c.y0, c.x1, c.y1, c.dist, c.colour); break;
        case CommandType::DrawModel:    _sink.DrawModel(m_models[c.dataOffset]); break;
        case CommandType::StartRender:  _sink.StartRender(); break;
        case CommandType::FinishRender: _sink.FinishRender(); break;
      }
    }
  }
}
