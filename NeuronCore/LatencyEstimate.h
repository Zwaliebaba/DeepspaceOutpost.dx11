#pragma once

// LatencyEstimate - smoothed round-trip-time estimation (NeuronCore, Track E1).
//
// The client folds each Ping/Pong round trip into a running RTT estimate here, then
// reports it to the server (which uses half of it, plus the render interpolation
// delay, to rewind targets for lag-compensated fire). Kept a pure arithmetic struct
// - the caller supplies every clock reading - so it is unit-testable without a
// socket, exactly like Net::InterpolationAlpha.

namespace Neuron::Net
{
  struct LatencyEstimate
  {
    // Fraction of each new sample folded into the running estimate (EWMA). Low
    // enough to reject per-probe jitter, high enough to track a genuine latency
    // change within a few probes at the ~1 Hz Ping cadence.
    static constexpr double RTT_BLEND = 0.25;

    // A sample above this is treated as a spike - typically a reliable-lane resend
    // that inflated one round trip - and ignored, so a single late Pong can't
    // poison the estimate.
    static constexpr double MAX_SANE_RTT_MS = 2000.0;

    double rttMs = 0.0;   // the current smoothed round-trip time (ms)
    bool valid = false;   // false until the first accepted sample

    // Fold one measured round trip (ms) into the estimate. The first accepted
    // sample seeds the estimate directly; later ones blend. Returns false, leaving
    // the estimate untouched, for a non-positive or spike sample.
    bool AddRttSample(double _sampleMs)
    {
      if (_sampleMs <= 0.0 || _sampleMs > MAX_SANE_RTT_MS)
        return false;
      rttMs = valid ? (rttMs + RTT_BLEND * (_sampleMs - rttMs)) : _sampleMs;
      valid = true;
      return true;
    }
  };
}
