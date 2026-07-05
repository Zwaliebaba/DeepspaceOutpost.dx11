#pragma once

// GestureRecognizer - a device-neutral, dependency-free pointer state machine.
//
// This is the CI-testable core of Track I5's touch/gesture layer (docs/
// interaction.md §3.2, §5). It takes raw per-pointer samples (id, position,
// timestamp, phase) from ANY device - mouse or WM_POINTER touch - and emits the
// device-neutral gestures the interaction grammar is defined in: tap,
// double-tap, long-press, single-pointer drag (camera orbit), two-finger pan,
// and pinch (zoom). The consumer (input_win.cpp's pointer path) decides what
// each gesture *means*; the recognizer only classifies motion.
//
// It is deliberately pure C++ (no Win32, no DirectXMath, no NeuronClient link
// dependency) so it runs under the headless test suite - the plan flags the
// multi-pointer state machine as the one piece "CI never runs" in-app, so its
// edge cases (slop, capture, second-finger mid-drag, cancel) are pinned here
// instead of by inspection. Time is passed in explicitly (never read from a
// clock) so tests are deterministic.

#include <cmath>
#include <cstdint>
#include <vector>

namespace Neuron::Input
{
  // The phase of a raw pointer sample, mirroring WM_POINTERDOWN/UPDATE/UP and
  // pointer capture-loss (Cancel). One mouse maps to a single fixed-id pointer.
  enum class PointerPhase : uint8_t { Down, Move, Up, Cancel };

  struct PointerSample
  {
    uint32_t     id     = 0;   // device pointer id (mouse uses a fixed constant)
    float        x      = 0.f; // client-space pixels
    float        y      = 0.f;
    uint32_t     timeMs = 0;   // event timestamp (monotonic, milliseconds)
    PointerPhase phase  = PointerPhase::Move;
  };

  enum class GestureType : uint8_t
  {
    Tap,        // quick press+release within slop
    DoubleTap,  // second Tap within the double-tap window and near the first
    LongPress,  // held past longPressMs within slop (fires once, while down)
    DragBegin,  // single pointer moved beyond slop
    DragMove,   // continued single-pointer drag (dx/dy since last)
    DragEnd,    // single pointer released (or pre-empted by a second finger)
    PanBegin,   // two pointers down - centroid tracking starts
    PanMove,    // two-finger centroid moved (dx/dy)
    PanEnd,     // dropped back below two pointers
    Pinch,      // two-finger distance changed (value = signed zoom steps)
  };

  struct GestureEvent
  {
    GestureType type;
    float       x     = 0.f; // primary pointer / centroid position (client px)
    float       y     = 0.f;
    float       dx    = 0.f; // movement since the last event (Drag/Pan)
    float       dy    = 0.f;
    float       value = 0.f; // Pinch: signed zoom steps (+ = fingers apart / zoom in)
    uint32_t    id    = 0;   // primary pointer id
  };

  struct GestureConfig
  {
    float    slopPx         = 6.0f;  // click/tap vs drag threshold (§3.6)
    uint32_t longPressMs    = 350;   // §3.2 long-press / RMB-hold threshold
    uint32_t doubleTapMs    = 300;   // max gap between the two taps
    float    doubleTapSlopPx= 24.0f; // max distance between the two taps
    float    pinchStepPx    = 40.0f; // px of finger-distance change per zoom step
  };

  // The fixed pointer id the platform layer assigns to the mouse so mouse and
  // touch share one code path (touch pointer ids come from the OS, always != 0
  // in practice; the mouse claims 0).
  inline constexpr uint32_t MOUSE_POINTER_ID = 0;

  class GestureRecognizer
  {
  public:
    GestureRecognizer() = default;
    explicit GestureRecognizer(const GestureConfig& _cfg) : m_cfg(_cfg) {}

    const GestureConfig& Config() const { return m_cfg; }

    // Feed one raw pointer sample; returns the gestures it produced (usually 0-2).
    std::vector<GestureEvent> Push(const PointerSample& _s)
    {
      std::vector<GestureEvent> out;
      switch (_s.phase)
      {
      case PointerPhase::Down:   OnDown(_s, out);   break;
      case PointerPhase::Move:   OnMove(_s, out);   break;
      case PointerPhase::Up:     OnUp(_s, out);     break;
      case PointerPhase::Cancel: OnCancel(_s, out); break;
      }
      return out;
    }

    // Drive time-based transitions (long-press fires without a raw event).
    // Call once per frame with the current timestamp.
    std::vector<GestureEvent> Tick(uint32_t _nowMs)
    {
      std::vector<GestureEvent> out;
      if (m_state == State::One && !m_p0.moved &&
          _nowMs - m_p0.startTime >= m_cfg.longPressMs)
      {
        out.push_back(Make(GestureType::LongPress, m_p0.curX, m_p0.curY, m_p0.id));
        m_state = State::OneLong;   // consumed: no tap will follow this press
      }
      return out;
    }

    // Test/inspection helper: how many pointers are currently down.
    int ActivePointers() const
    {
      return (m_p0.active ? 1 : 0) + (m_p1.active ? 1 : 0);
    }

  private:
    enum class State : uint8_t
    {
      Idle,       // nothing down
      One,        // one pointer down, unclassified (tap/long-press/drag pending)
      OneDrag,    // one pointer, moved beyond slop
      OneLong,    // long-press already fired for this press
      Two,        // two pointers down (pan/pinch)
      Suppressed, // >2 fingers seen, or the tail of a two-finger gesture: wait for all up
    };

    struct Pointer
    {
      bool     active    = false;
      uint32_t id        = 0;
      float    startX    = 0.f, startY = 0.f;
      float    curX      = 0.f, curY = 0.f;
      uint32_t startTime = 0;
      bool     moved     = false; // ever exceeded slop
    };

    GestureConfig m_cfg{};
    State         m_state = State::Idle;
    Pointer       m_p0{}; // primary (first down)
    Pointer       m_p1{}; // secondary
    float         m_lastDist = 0.f;
    float         m_lastCx = 0.f, m_lastCy = 0.f;

    // double-tap memory
    bool     m_haveLastTap = false;
    uint32_t m_lastTapTime = 0;
    float    m_lastTapX = 0.f, m_lastTapY = 0.f;

    static GestureEvent Make(GestureType _t, float _x, float _y, uint32_t _id)
    {
      GestureEvent e; e.type = _t; e.x = _x; e.y = _y; e.id = _id; return e;
    }

    static float Dist(float _ax, float _ay, float _bx, float _by)
    {
      const float dx = _ax - _bx, dy = _ay - _by;
      return static_cast<float>(std::sqrt(static_cast<double>(dx) * dx + static_cast<double>(dy) * dy));
    }

    Pointer* Find(uint32_t _id)
    {
      if (m_p0.active && m_p0.id == _id) return &m_p0;
      if (m_p1.active && m_p1.id == _id) return &m_p1;
      return nullptr;
    }

    void SeedPointer(Pointer& _p, const PointerSample& _s)
    {
      _p.active = true; _p.id = _s.id;
      _p.startX = _p.curX = _s.x;
      _p.startY = _p.curY = _s.y;
      _p.startTime = _s.timeMs;
      _p.moved = false;
    }

    void BeginTwoFinger(std::vector<GestureEvent>& _out)
    {
      m_lastDist = Dist(m_p0.curX, m_p0.curY, m_p1.curX, m_p1.curY);
      m_lastCx = (m_p0.curX + m_p1.curX) * 0.5f;
      m_lastCy = (m_p0.curY + m_p1.curY) * 0.5f;
      m_state = State::Two;
      _out.push_back(Make(GestureType::PanBegin, m_lastCx, m_lastCy, m_p0.id));
    }

    void OnDown(const PointerSample& _s, std::vector<GestureEvent>& _out)
    {
      if (m_state == State::Idle)
      {
        SeedPointer(m_p0, _s);
        m_state = State::One;
        return;
      }
      if ((m_state == State::One || m_state == State::OneDrag || m_state == State::OneLong)
          && !m_p1.active)
      {
        // A second finger arrives: end any single-pointer drag, drop pending
        // tap/long-press, and switch to two-finger pan/pinch.
        if (m_state == State::OneDrag)
          _out.push_back(Make(GestureType::DragEnd, m_p0.curX, m_p0.curY, m_p0.id));
        SeedPointer(m_p1, _s);
        BeginTwoFinger(_out);
        return;
      }
      // A third pointer (or a down we can't place): suppress until all lift.
      if (Find(_s.id) == nullptr)
        m_state = State::Suppressed;
    }

    void OnMove(const PointerSample& _s, std::vector<GestureEvent>& _out)
    {
      Pointer* p = Find(_s.id);
      if (!p) return;
      const float prevX = p->curX, prevY = p->curY;
      p->curX = _s.x; p->curY = _s.y;

      if (m_state == State::Two)
      {
        const float newDist = Dist(m_p0.curX, m_p0.curY, m_p1.curX, m_p1.curY);
        const float cx = (m_p0.curX + m_p1.curX) * 0.5f;
        const float cy = (m_p0.curY + m_p1.curY) * 0.5f;

        const float dCentroidX = cx - m_lastCx;
        const float dCentroidY = cy - m_lastCy;
        if (dCentroidX != 0.f || dCentroidY != 0.f)
        {
          GestureEvent e = Make(GestureType::PanMove, cx, cy, m_p0.id);
          e.dx = dCentroidX; e.dy = dCentroidY;
          _out.push_back(e);
        }

        const float dDist = newDist - m_lastDist;
        if (m_cfg.pinchStepPx > 0.f && dDist != 0.f)
        {
          GestureEvent e = Make(GestureType::Pinch, cx, cy, m_p0.id);
          e.value = dDist / m_cfg.pinchStepPx; // signed zoom steps
          _out.push_back(e);
        }

        m_lastDist = newDist;
        m_lastCx = cx; m_lastCy = cy;
        return;
      }

      if (!p->moved && Dist(p->startX, p->startY, p->curX, p->curY) > m_cfg.slopPx)
        p->moved = true;

      if (m_state == State::One && p == &m_p0 && p->moved)
      {
        m_state = State::OneDrag;
        _out.push_back(Make(GestureType::DragBegin, p->curX, p->curY, p->id));
        return;
      }
      if (m_state == State::OneDrag && p == &m_p0)
      {
        GestureEvent e = Make(GestureType::DragMove, p->curX, p->curY, p->id);
        e.dx = p->curX - prevX; e.dy = p->curY - prevY;
        _out.push_back(e);
      }
      // State::One before slop, State::OneLong, State::Suppressed: swallow moves.
    }

    void OnUp(const PointerSample& _s, std::vector<GestureEvent>& _out)
    {
      Pointer* p = Find(_s.id);
      if (!p)
      {
        if (ActivePointers() == 0) m_state = State::Idle;
        return;
      }
      p->curX = _s.x; p->curY = _s.y; // release position is the tap/drag-end point

      if (m_state == State::Two)
      {
        // First finger up ends the two-finger gesture; the other is held but
        // must not produce a tap.
        _out.push_back(Make(GestureType::PanEnd, m_lastCx, m_lastCy, m_p0.id));
        p->active = false;
        m_state = State::Suppressed;
        return;
      }

      if (m_state == State::OneDrag && p == &m_p0)
      {
        _out.push_back(Make(GestureType::DragEnd, p->curX, p->curY, p->id));
      }
      else if (m_state == State::One && p == &m_p0 && !p->moved)
      {
        EmitTapOrDouble(*p, _s.timeMs, _out);
      }
      // OneLong / Suppressed: release is consumed silently.

      p->active = false;
      if (ActivePointers() == 0) m_state = State::Idle;
    }

    void OnCancel(const PointerSample& _s, std::vector<GestureEvent>& _out)
    {
      if (m_state == State::Two)
        _out.push_back(Make(GestureType::PanEnd, m_lastCx, m_lastCy, m_p0.id));
      else if (m_state == State::OneDrag)
        _out.push_back(Make(GestureType::DragEnd, m_p0.curX, m_p0.curY, m_p0.id));

      if (Pointer* p = Find(_s.id)) p->active = false;
      else { m_p0.active = m_p1.active = false; }

      if (ActivePointers() == 0) m_state = State::Idle;
      else m_state = State::Suppressed; // a partial cancel: wait for a clean slate
    }

    void EmitTapOrDouble(const Pointer& _p, uint32_t _timeMs, std::vector<GestureEvent>& _out)
    {
      const bool doubleTap =
        m_haveLastTap &&
        (_timeMs - m_lastTapTime) <= m_cfg.doubleTapMs &&
        Dist(m_lastTapX, m_lastTapY, _p.curX, _p.curY) <= m_cfg.doubleTapSlopPx;

      if (doubleTap)
      {
        _out.push_back(Make(GestureType::DoubleTap, _p.curX, _p.curY, _p.id));
        m_haveLastTap = false; // a double consumes the pair
      }
      else
      {
        _out.push_back(Make(GestureType::Tap, _p.curX, _p.curY, _p.id));
        m_haveLastTap = true;
        m_lastTapTime = _timeMs;
        m_lastTapX = _p.curX; m_lastTapY = _p.curY;
      }
    }
  };
}
