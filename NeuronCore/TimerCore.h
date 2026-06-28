#pragma once

namespace Neuron::Timer
{
  // Helper for animation and simulation timing.
  class Core
  {
    public:
      static void Startup()
      {
        if (!QueryPerformanceFrequency(&m_qpcFrequency))
          winrt::throw_hresult(E_FAIL);

        if (!QueryPerformanceCounter(&m_qpcLastTime))
          winrt::throw_hresult(E_FAIL);

        // Initialize max delta to 1/10 of a second.
        m_qpcMaxDelta = m_qpcFrequency.QuadPart / 10;

        m_frameCount = m_totalTicks = m_elapsedTicks = 0;
      }

      // Get elapsed time since the previous Update call.
      static uint64_t GetElapsedTicks() { return m_elapsedTicks; }
      static float GetElapsedSeconds() { return static_cast<float>(TicksToSeconds(m_elapsedTicks)); }

      // Get total time since the start of the program.
      static uint64_t GetTotalTicks() { return m_totalTicks; }
      static double GetTotalSeconds() { return TicksToSeconds(m_totalTicks); }

      // Get total number of updates since start of the program.
      static uint32_t GetFrameCount() { return m_frameCount; }

      // Get the current framerate.
      static uint32_t GetFramesPerSecond() { return m_framesPerSecond; }

      // Integer format represents time using 10,000,000 ticks per second.
      static constexpr uint64_t TICKS_PER_SECOND = 10000000;

      static double TicksToSeconds(uint64_t _ticks) { return static_cast<double>(_ticks) / TICKS_PER_SECOND; }
      static uint64_t SecondsToTicks(double _seconds) { return static_cast<uint64_t>(_seconds * TICKS_PER_SECOND); }

      // After an intentional timing discontinuity (for instance a blocking IO operation)
      // call this to avoid fixed timestep logic attempting a string of catch-up Update calls.
      static void ResetElapsedTime()
      {
        if (!QueryPerformanceCounter(&m_qpcLastTime))
          winrt::throw_hresult(E_FAIL);

        m_leftOverTicks = 0;
        m_framesPerSecond = 0;
        m_framesThisSecond = 0;
        m_qpcSecondCounter = 0;
      }

      // Update timer state, calling the specified Update function the appropriate number of times.
      static float Update()
      {
        // Query the current time.
        LARGE_INTEGER currentTime;

        if (!QueryPerformanceCounter(&currentTime))
          winrt::throw_hresult(E_FAIL);

        uint64_t timeDelta = currentTime.QuadPart - m_qpcLastTime.QuadPart;

        m_qpcLastTime = currentTime;
        m_qpcSecondCounter += timeDelta;

        // Clamp excessively large time deltas (eg. after paused in the debugger).
        if (timeDelta > m_qpcMaxDelta)
          timeDelta = m_qpcMaxDelta;

        // Convert QPC units into our own canonical tick format. Cannot overflow due to the previous clamp.
        timeDelta *= TICKS_PER_SECOND;
        timeDelta /= m_qpcFrequency.QuadPart;

        const uint32_t lastFrameCount = m_frameCount;

        // Variable timestep update logic.
        m_elapsedTicks = timeDelta;
        m_totalTicks += timeDelta;
        m_leftOverTicks = 0;
        m_frameCount++;

        // Track the current framerate.
        if (m_frameCount != lastFrameCount)
          m_framesThisSecond++;

        if (m_qpcSecondCounter >= static_cast<uint64_t>(m_qpcFrequency.QuadPart))
        {
          m_framesPerSecond = m_framesThisSecond;
          m_framesThisSecond = 0;
          m_qpcSecondCounter %= m_qpcFrequency.QuadPart;
        }

        return GetElapsedSeconds();
      }

    private:
      // Source timing data uses QPC units.
      inline static LARGE_INTEGER m_qpcFrequency;
      inline static LARGE_INTEGER m_qpcLastTime;
      inline static uint64_t m_qpcMaxDelta;

      // Derived timing data uses our own canonical tick format.
      inline static uint64_t m_elapsedTicks{0};
      inline static uint64_t m_totalTicks{0};
      inline static uint64_t m_leftOverTicks{0};

      // For tracking the framerate.
      inline static uint32_t m_frameCount{0};
      inline static uint32_t m_framesPerSecond{0};
      inline static uint32_t m_framesThisSecond{0};
      inline static uint64_t m_qpcSecondCounter{0};
  };
}
