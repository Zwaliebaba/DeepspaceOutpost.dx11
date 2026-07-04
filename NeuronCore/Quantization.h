#pragma once

// Quantization - compact fixed-point encodings for the snapshot wire (NeuronCore,
// Track E2).
//
// The v1 snapshot spent 4 full float32s each on the nose and roof basis vectors and
// the speed - 28 bytes of orientation/speed per entity, most of it wasted precision
// (a render frame doesn't need 24 significand bits on a unit vector). These pure
// helpers pack each into just enough bits:
//
//   * a unit-vector component -> int16, scaled by 32767 (a ~3e-5 grid over [-1,1]);
//   * a speed (world units/tick, always >= 0) -> uint16 at 1/256-unit resolution.
//
// Everything is DETERMINISTIC integer/round math (no fast-float reordering under
// /fp:strict), so the server and every client agree bit-for-bit on the decoded
// value - the "server-side rounding" the delta stage (E2b) will rely on. Axis-
// aligned components (0, +/-1) survive the round trip EXACTLY (0->0, 1->32767->1),
// so an unrotated basis is lossless; only off-axis components take the ~3e-5 grid
// error, which is imperceptible in the render.

#include <cmath>
#include <cstdint>

namespace Neuron::Net
{
  // A unit-vector component is scaled onto the full signed-16-bit range. 32767 (not
  // 32768) keeps the mapping symmetric so +1 and -1 are exact mirror images.
  inline constexpr float UNIT_QUANT_SCALE = 32767.0f;

  // Speed resolution: 1/256 of a world unit per tick. Max representable is
  // 65535/256 ~= 256 u/t, comfortably above the fastest hull (FlightCaps caps at
  // 100 u/t), so no real speed ever saturates.
  inline constexpr float SPEED_QUANT_SCALE = 256.0f;

  // Round-half-away-from-zero to the nearest integer (std::lround semantics),
  // returned as a long so the caller clamps before narrowing.
  [[nodiscard]] inline long RoundToLong(double _v)
  {
    return std::lround(_v);
  }

  // Pack a unit-vector component (expected in [-1,1]; clamped if not) into int16.
  [[nodiscard]] inline int16_t QuantizeUnit(float _f)
  {
    const float c = _f < -1.0f ? -1.0f : (_f > 1.0f ? 1.0f : _f);
    long q = RoundToLong(static_cast<double>(c) * static_cast<double>(UNIT_QUANT_SCALE));
    if (q > 32767) q = 32767;
    if (q < -32767) q = -32767;
    return static_cast<int16_t>(q);
  }

  // Recover the component. Exact for 0 and +/-32767 (i.e. 0 and +/-1).
  [[nodiscard]] inline float DequantizeUnit(int16_t _q)
  {
    return static_cast<float>(_q) / UNIT_QUANT_SCALE;
  }

  // Pack a non-negative speed into uint16 at 1/256-unit resolution. Negative inputs
  // clamp to 0; anything over the u16 ceiling saturates (never happens for real
  // hull speeds).
  [[nodiscard]] inline uint16_t QuantizeSpeed(float _s)
  {
    const float c = _s < 0.0f ? 0.0f : _s;
    long q = RoundToLong(static_cast<double>(c) * static_cast<double>(SPEED_QUANT_SCALE));
    if (q < 0) q = 0;
    if (q > 65535) q = 65535;
    return static_cast<uint16_t>(q);
  }

  // Recover the speed. An integer speed (the common dead-reckoning case) that is a
  // whole number of units is exact, since n*256/256 == n.
  [[nodiscard]] inline float DequantizeSpeed(uint16_t _q)
  {
    return static_cast<float>(_q) / SPEED_QUANT_SCALE;
  }
}
