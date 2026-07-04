#pragma once

// TickPacer - fixed-timestep accumulator for the server loop (NeuronServer).
//
// Replaces a bare Sleep(step) with real pacing: feed it the wall time elapsed
// since the last pump and it returns how many fixed steps to run NOW - bounded, so
// a hitch (a debugger pause, a GC-less stall) can never trigger an unbounded string
// of catch-up ticks (the "spiral of death") - and how long to sleep when the loop
// is running ahead. Pure: it reads no clock itself (the caller supplies the elapsed
// time), so the pacing logic is unit-tested directly without touching QPC.

#include <cstdint>

namespace Neuron::Server
{
  class TickPacer
  {
  public:
    TickPacer(double _stepMs, int _maxCatchUp)
      : m_stepMs(_stepMs > 0.0 ? _stepMs : 1.0)
      , m_maxCatchUp(_maxCatchUp < 1 ? 1 : _maxCatchUp)
    {
    }

    // Accumulate `_elapsedMs` and return the number of fixed steps to run now
    // (0.._maxCatchUp). `_overran` is set true when the backlog still exceeds one
    // step after the cap - the excess is then dropped (accumulator reset) so the
    // simulation falls behind gracefully instead of spiralling.
    int Pump(double _elapsedMs, bool& _overran)
    {
      if (_elapsedMs > 0.0)
        m_accumMs += _elapsedMs;

      int steps = 0;
      while (m_accumMs >= m_stepMs && steps < m_maxCatchUp)
      {
        m_accumMs -= m_stepMs;
        ++steps;
      }

      _overran = (m_accumMs >= m_stepMs);
      if (_overran)
        m_accumMs = 0.0;   // drop the backlog rather than spiral
      return steps;
    }

    // Milliseconds to sleep when the loop is ahead of schedule (0 when a step is
    // already due). The caller sleeps at most this long before pumping again.
    [[nodiscard]] double SleepMs() const
    {
      return m_accumMs >= m_stepMs ? 0.0 : (m_stepMs - m_accumMs);
    }

    [[nodiscard]] double StepMs() const { return m_stepMs; }

  private:
    double m_stepMs;
    int m_maxCatchUp;
    double m_accumMs = 0.0;
  };
}
