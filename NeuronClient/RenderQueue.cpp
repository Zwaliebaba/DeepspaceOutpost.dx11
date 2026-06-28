#include "pch.h"

#include "RenderQueue.h"

namespace Neuron::Render
{
  void RenderQueue::Clear()
  {
    // Keep the allocated capacity; we refill every frame.
    m_commands.clear();
    m_points.clear();
    m_text.clear();
  }

  Command& RenderQueue::Push(CommandType _type)
  {
    Command& cmd = m_commands.emplace_back();
    cmd.type = _type;
    return cmd;
  }

  uint32_t RenderQueue::AppendInts(const int* _values, int _count)
  {
    const uint32_t offset = static_cast<uint32_t>(m_points.size());
    if (_values != nullptr && _count > 0)
      m_points.insert(m_points.end(), _values, _values + _count);
    return offset;
  }

  uint32_t RenderQueue::AppendText(std::string_view _text)
  {
    const uint32_t offset = static_cast<uint32_t>(m_text.size());
    m_text.insert(m_text.end(), _text.begin(), _text.end());
    m_text.push_back('\0');   // keep replay pointers NUL-terminated
    return offset;
  }

  void RenderQueue::Pixel(int _x, int _y, int _colour)
  {
    Command& c = Push(CommandType::Pixel);
    c.x0 = _x; c.y0 = _y; c.colour = _colour;
  }

  void RenderQueue::FastPixel(int _x, int _y, int _colour)
  {
    Command& c = Push(CommandType::FastPixel);
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

  void RenderQueue::Circle(int _cx, int _cy, int _radius, int _colour)
  {
    Command& c = Push(CommandType::Circle);
    c.x0 = _cx; c.y0 = _cy; c.radius = _radius; c.colour = _colour;
  }

  void RenderQueue::FilledCircle(int _cx, int _cy, int _radius, int _colour)
  {
    Command& c = Push(CommandType::FilledCircle);
    c.x0 = _cx; c.y0 = _cy; c.radius = _radius; c.colour = _colour;
  }

  void RenderQueue::Triangle(int _x0, int _y0, int _x1, int _y1, int _x2, int _y2, int _colour)
  {
    Command& c = Push(CommandType::Triangle);
    c.x0 = _x0; c.y0 = _y0; c.x1 = _x1; c.y1 = _y1; c.x2 = _x2; c.y2 = _y2; c.colour = _colour;
  }

  void RenderQueue::RenderPolygon(int _numPoints, const int* _points, int _intCount, int _colour, int _dist)
  {
    Command& c = Push(CommandType::RenderPolygon);
    c.param = _numPoints;
    c.colour = _colour;
    c.dist = _dist;
    c.dataOffset = AppendInts(_points, _intCount);
    c.dataCount = static_cast<uint32_t>(_intCount > 0 ? _intCount : 0);
  }

  void RenderQueue::RenderLine(int _x0, int _y0, int _x1, int _y1, int _dist, int _colour)
  {
    Command& c = Push(CommandType::RenderLine);
    c.x0 = _x0; c.y0 = _y0; c.x1 = _x1; c.y1 = _y1; c.dist = _dist; c.colour = _colour;
  }

  void RenderQueue::Sprite(int _spriteId, int _x, int _y)
  {
    Command& c = Push(CommandType::Sprite);
    c.param = _spriteId; c.x0 = _x; c.y0 = _y;
  }

  void RenderQueue::Text(int _x, int _y, std::string_view _text)
  {
    Command& c = Push(CommandType::Text);
    c.x0 = _x; c.y0 = _y;
    c.textOffset = AppendText(_text);
    c.textCount = static_cast<uint32_t>(_text.size());
  }

  void RenderQueue::CentreText(int _y, std::string_view _text, int _pointSize, int _colour)
  {
    Command& c = Push(CommandType::CentreText);
    c.y0 = _y; c.param = _pointSize; c.colour = _colour;
    c.textOffset = AppendText(_text);
    c.textCount = static_cast<uint32_t>(_text.size());
  }

  void RenderQueue::SetClipRegion(int _tx, int _ty, int _bx, int _by)
  {
    Command& c = Push(CommandType::SetClipRegion);
    c.x0 = _tx; c.y0 = _ty; c.x1 = _bx; c.y1 = _by;
  }

  void RenderQueue::ClearArea(int _tx, int _ty, int _bx, int _by)
  {
    Command& c = Push(CommandType::ClearArea);
    c.x0 = _tx; c.y0 = _ty; c.x1 = _bx; c.y1 = _by;
  }

  void RenderQueue::DrawScanner() { Push(CommandType::DrawScanner); }
  void RenderQueue::StartRender() { Push(CommandType::StartRender); }
  void RenderQueue::FinishRender() { Push(CommandType::FinishRender); }

  void RenderQueue::Replay(RenderSink& _sink) const
  {
    // Arenas are stable for the whole replay (no recording happens during it),
    // so resolving offsets to pointers here is safe.
    for (const Command& c : m_commands)
    {
      switch (c.type)
      {
        case CommandType::Pixel:        _sink.Pixel(c.x0, c.y0, c.colour); break;
        case CommandType::FastPixel:    _sink.FastPixel(c.x0, c.y0, c.colour); break;
        case CommandType::Line:         _sink.Line(c.x0, c.y0, c.x1, c.y1); break;
        case CommandType::ColourLine:   _sink.ColourLine(c.x0, c.y0, c.x1, c.y1, c.colour); break;
        case CommandType::Circle:       _sink.Circle(c.x0, c.y0, c.radius, c.colour); break;
        case CommandType::FilledCircle: _sink.FilledCircle(c.x0, c.y0, c.radius, c.colour); break;
        case CommandType::Triangle:     _sink.Triangle(c.x0, c.y0, c.x1, c.y1, c.x2, c.y2, c.colour); break;
        case CommandType::RenderPolygon:
          _sink.RenderPolygon(c.param, m_points.data() + c.dataOffset, c.colour, c.dist);
          break;
        case CommandType::RenderLine:   _sink.RenderLine(c.x0, c.y0, c.x1, c.y1, c.dist, c.colour); break;
        case CommandType::Sprite:       _sink.Sprite(c.param, c.x0, c.y0); break;
        case CommandType::Text:         _sink.Text(c.x0, c.y0, m_text.data() + c.textOffset); break;
        case CommandType::CentreText:   _sink.CentreText(c.y0, m_text.data() + c.textOffset, c.param, c.colour); break;
        case CommandType::SetClipRegion:_sink.SetClipRegion(c.x0, c.y0, c.x1, c.y1); break;
        case CommandType::ClearArea:    _sink.ClearArea(c.x0, c.y0, c.x1, c.y1); break;
        case CommandType::DrawScanner:  _sink.DrawScanner(); break;
        case CommandType::StartRender:  _sink.StartRender(); break;
        case CommandType::FinishRender: _sink.FinishRender(); break;
      }
    }
  }
}
