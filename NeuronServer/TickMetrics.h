#pragma once

// TickMetrics - always-on "is the server keeping up" counters (NeuronServer).
//
// The minimum set from ARCHITECTURE.md §13.3-E8, cheap enough to run every tick:
// tick-duration (avg/max), overrun count, live entity/session counts, broadphase
// candidate-pair count (validates D1's grid conversion), and bytes sent. The
// server feeds one TickSample per tick and periodically snapshots a rolling window
// for a console line and for the D5 BotClient harness to read. Pure data +
// arithmetic - no clock, no sockets - so it is unit-tested directly; the timing
// source (QPC, off the sim's determinism path) lives in the server loop.

#include <cstdint>

namespace Neuron::Server
{
  struct TickSample
  {
    double   durationMs = 0.0;    // whole-tick wall time
    uint32_t entityCount = 0;     // live entities in the world
    uint32_t sessionCount = 0;    // connected sessions
    uint64_t candidatePairs = 0;  // broadphase pairs considered this tick (0 before D1)
    uint64_t bytesSent = 0;       // datagram bytes sent this tick
  };

  struct TickSummary
  {
    uint64_t ticks = 0;
    uint64_t overruns = 0;
    double   avgMs = 0.0;
    double   maxMs = 0.0;
    uint32_t entities = 0;
    uint32_t sessions = 0;
    uint64_t avgCandidatePairs = 0;
    uint64_t bytesPerSecond = 0;
  };

  class TickMetrics
  {
  public:
    void Record(const TickSample& _s)
    {
      ++m_ticks;
      m_sumMs += _s.durationMs;
      if (_s.durationMs > m_maxMs)
        m_maxMs = _s.durationMs;
      m_lastEntities = _s.entityCount;
      m_lastSessions = _s.sessionCount;
      m_sumPairs += _s.candidatePairs;
      m_sumBytes += _s.bytesSent;
    }

    void NoteOverrun() { ++m_overruns; }

    // A summary over the window recorded so far. `_windowSeconds` converts the
    // accumulated byte total into a rate; pass the real elapsed wall seconds.
    [[nodiscard]] TickSummary Snapshot(double _windowSeconds) const
    {
      TickSummary out;
      out.ticks = m_ticks;
      out.overruns = m_overruns;
      out.avgMs = m_ticks != 0 ? m_sumMs / static_cast<double>(m_ticks) : 0.0;
      out.maxMs = m_maxMs;
      out.entities = m_lastEntities;
      out.sessions = m_lastSessions;
      out.avgCandidatePairs = m_ticks != 0 ? m_sumPairs / m_ticks : 0;
      out.bytesPerSecond = _windowSeconds > 0.0
        ? static_cast<uint64_t>(static_cast<double>(m_sumBytes) / _windowSeconds) : 0;
      return out;
    }

    // Start a fresh window (call after emitting a summary).
    void Reset()
    {
      m_ticks = 0;
      m_overruns = 0;
      m_sumMs = 0.0;
      m_maxMs = 0.0;
      m_sumPairs = 0;
      m_sumBytes = 0;
      // m_lastEntities / m_lastSessions carry forward (they are levels, not sums).
    }

    [[nodiscard]] uint64_t WindowTicks() const { return m_ticks; }

  private:
    uint64_t m_ticks = 0;
    uint64_t m_overruns = 0;
    double   m_sumMs = 0.0;
    double   m_maxMs = 0.0;
    uint32_t m_lastEntities = 0;
    uint32_t m_lastSessions = 0;
    uint64_t m_sumPairs = 0;
    uint64_t m_sumBytes = 0;
  };
}
