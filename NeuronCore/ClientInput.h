#pragma once

// ClientInput - the client -> server command/intent packet (NeuronCore).
//
// The thin client never moves its own ship; it sends what it WANTS and the
// server decides. This is that wire message: a normalized, device-independent
// flight intent plus a sequence number. Input is sent unreliably and often -
// like snapshots it is self-superseding, so the server simply keeps the highest
// sequence it has seen and drops stale/duplicate datagrams. Shared protocol only
// (no behaviour): the server maps it onto its authoritative FlightIntent.

#include <cstdint>

#include "DataWriter.h"
#include "DataReader.h"

namespace Neuron::Net
{
  inline constexpr uint32_t INPUT_MAGIC = 0x4E434D44;   // 'NCMD'
  inline constexpr uint16_t INPUT_VERSION = 3;
  inline constexpr uint32_t NO_MISSILE_TARGET = 0xFFFFFFFFu;   // sentinel: nothing locked

  struct ClientInput
  {
    uint32_t sequence = 0;       // monotonically increasing; latest wins on the server
    float rollAxis = 0.0f;       // [-1, 1] desired roll  (right positive)
    float pitchAxis = 0.0f;      // [-1, 1] desired pitch (up positive)
    float throttle = 0.0f;       // [ 0, 1] desired forward throttle
    bool fire = false;           // fire the front laser this frame
    bool fireMissile = false;    // launch a missile this frame
    uint32_t missileTarget = NO_MISSILE_TARGET;   // entity index the missile is locked to
  };

  inline void WriteInput(DataWriter& _w, const ClientInput& _in)
  {
    _w.WriteU32(INPUT_MAGIC);
    _w.WriteU16(INPUT_VERSION);
    _w.WriteU32(_in.sequence);
    _w.WriteF32(_in.rollAxis);
    _w.WriteF32(_in.pitchAxis);
    _w.WriteF32(_in.throttle);
    _w.WriteU8(_in.fire ? 1 : 0);
    _w.WriteU8(_in.fireMissile ? 1 : 0);
    _w.WriteU32(_in.missileTarget);
  }

  [[nodiscard]] inline bool ReadInput(DataReader& _r, ClientInput& _out)
  {
    if (_r.ReadU32() != INPUT_MAGIC)
      return false;
    if (_r.ReadU16() != INPUT_VERSION)
      return false;
    _out.sequence = _r.ReadU32();
    _out.rollAxis = _r.ReadF32();
    _out.pitchAxis = _r.ReadF32();
    _out.throttle = _r.ReadF32();
    _out.fire = _r.ReadU8() != 0;
    _out.fireMissile = _r.ReadU8() != 0;
    _out.missileTarget = _r.ReadU32();
    return _r.Ok();
  }
}
