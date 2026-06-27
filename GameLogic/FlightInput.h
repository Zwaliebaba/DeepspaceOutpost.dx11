#pragma once

// FlightInput - turn player intent into authoritative flight controls (GameLogic).
//
// The locked input model is a command/intent protocol: the client never sets the
// ship's motion directly, it sends what it *wants* (a normalized, device- and
// frame-rate-independent desire), and the server decides what actually happens by
// clamping that desire to the ship's performance envelope (FlightCaps). This is
// what keeps the server authoritative - a client cannot out-turn or out-run its
// hull - and it generalizes cleanly from twitch flight (axis = stick deflection)
// toward 4X/RTS orders later without changing the wire protocol.
//
// StepFlightInput() runs once per tick BEFORE StepFlight(): it writes each
// steerable ship's roll/pitch/speed, then the flight integrator consumes them.

#include "ECS.h"

#include "SimComponents.h"

namespace Neuron::GameLogic
{
  // The player's desired controls, normalized and unit-less. Replicated from the
  // client as a command; never trusted as absolute motion.
  struct FlightIntent
  {
    double rollAxis = 0.0;    // [-1, 1] desired roll  (right positive)
    double pitchAxis = 0.0;   // [-1, 1] desired pitch (climb positive)
    double throttle = 0.0;    // [ 0, 1] desired forward throttle
  };

  // A ship's flight envelope: the most roll/pitch it can apply per tick and its
  // top speed, in Flight's own units (roll/pitch are alpha/beta increments, i.e.
  // the legacy maxRoll/maxClimb divided by 256). Server-owned per ship type.
  struct FlightCaps
  {
    double maxRollRate = 31.0 / 256.0;    // ~0.121, a Cobra-class roll rate
    double maxPitchRate = 31.0 / 256.0;
    double maxSpeed = 100.0;
  };

  // Clamp `_v` into [_lo, _hi].
  [[nodiscard]] inline constexpr double ClampRange(double _v, double _lo, double _hi)
  {
    return _v < _lo ? _lo : (_v > _hi ? _hi : _v);
  }

  // Map one ship's intent to its Flight controls through its caps. Pure: the
  // axes are clamped to their valid range first, so an out-of-range (or hostile)
  // client request is silently bounded to the hull's envelope.
  inline void ResolveIntent(Flight& _flight, const FlightIntent& _intent, const FlightCaps& _caps)
  {
    _flight.roll = ClampRange(_intent.rollAxis, -1.0, 1.0) * _caps.maxRollRate;
    _flight.pitch = ClampRange(_intent.pitchAxis, -1.0, 1.0) * _caps.maxPitchRate;
    _flight.speed = ClampRange(_intent.throttle, 0.0, 1.0) * _caps.maxSpeed;
  }

  // Apply every steerable ship's intent to its Flight, using the ship's own
  // FlightCaps when present (otherwise a default envelope). Probes a third
  // component (caps) per entity, so it captures the registry.
  inline void StepFlightInput(ECS::Registry& _world)
  {
    _world.Each<Flight, FlightIntent>([&_world](ECS::EntityId _id, Flight& _flight, FlightIntent& _intent)
    {
      const FlightCaps* caps = _world.TryGet<FlightCaps>(_id);
      ResolveIntent(_flight, _intent, caps != nullptr ? *caps : FlightCaps{});
    });
  }
}
