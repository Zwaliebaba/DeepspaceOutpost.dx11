#pragma once

// StationProtocol - the docked request/response wire schema (NeuronCore).
//
// Docking, trading and equip are turn-based and must be reliable and ordered, so
// unlike the realtime snapshot stream they ride the ReliableChannel as
// request->response: the client asks (Dock/Buy/Sell), the server validates
// against its AUTHORITATIVE wallet/cargo/market and replies with the result.
// Shared data/protocol only - the structs and their encoding - so both ends agree
// on the bytes; the actual validation lives server-side (GameLogic StationServices).

#include <cstdint>
#include <vector>

#include "DataWriter.h"
#include "DataReader.h"
#include "ReliableChannel.h"
#include "GameEvents.h"

namespace Neuron::Net
{
  enum class StationRequestKind : uint8_t
  {
    Dock = 1,
    Undock = 2,
    Buy = 3,
    Sell = 4,
    Equip = 5,      // buy equipment; the item id travels in StationRequest::commodity
    Teleport = 6,   // jump to another system; the target system id is StationRequest::stationId
  };

  // Equipment the player can buy at a station (id carried in a request's
  // `commodity` field). Shared so client and server agree on the catalog.
  enum class EquipItem : uint16_t
  {
    Missile = 1,
    LargeCargoBay = 2,
    Ecm = 3,
    FuelScoop = 4,
    EnergyBomb = 5,
    EscapePod = 6,
  };

  // Result of a station request (also returned by the server-side services).
  enum class StationStatus : uint8_t
  {
    Ok = 0,
    NotDocked = 1,
    NoStock = 2,
    NotEnoughCredits = 3,
    HoldFull = 4,
    NoCargo = 5,
    BadCommodity = 6,
    CantDock = 7,
    AlreadyOwned = 8,
  };

  struct StationRequest
  {
    StationRequestKind kind = StationRequestKind::Dock;
    uint16_t commodity = 0;
    uint16_t quantity = 0;
    uint32_t stationId = 0;
  };

  struct StationResponse
  {
    StationRequestKind kind = StationRequestKind::Dock;
    StationStatus status = StationStatus::Ok;
    int32_t credits = 0;
    uint16_t commodity = 0;
    uint16_t cargo = 0;     // resulting held quantity of `commodity`
  };

  // --- Request --------------------------------------------------------------

  [[nodiscard]] inline std::vector<uint8_t> EncodeStationRequest(const StationRequest& _r)
  {
    DataWriter w;
    w.WriteU8(static_cast<uint8_t>(_r.kind));
    w.WriteU16(_r.commodity);
    w.WriteU16(_r.quantity);
    w.WriteU32(_r.stationId);
    return w.Bytes();
  }

  [[nodiscard]] inline bool DecodeStationRequest(const ReliableMessage& _m, StationRequest& _out)
  {
    if (_m.type != static_cast<uint16_t>(EventType::StationRequest))
      return false;
    DataReader r(_m.payload.data(), _m.payload.size());
    _out.kind = static_cast<StationRequestKind>(r.ReadU8());
    _out.commodity = r.ReadU16();
    _out.quantity = r.ReadU16();
    _out.stationId = r.ReadU32();
    return r.Ok();
  }

  // --- Response -------------------------------------------------------------

  [[nodiscard]] inline std::vector<uint8_t> EncodeStationResponse(const StationResponse& _r)
  {
    DataWriter w;
    w.WriteU8(static_cast<uint8_t>(_r.kind));
    w.WriteU8(static_cast<uint8_t>(_r.status));
    w.WriteI32(_r.credits);
    w.WriteU16(_r.commodity);
    w.WriteU16(_r.cargo);
    return w.Bytes();
  }

  [[nodiscard]] inline bool DecodeStationResponse(const ReliableMessage& _m, StationResponse& _out)
  {
    if (_m.type != static_cast<uint16_t>(EventType::StationResponse))
      return false;
    DataReader r(_m.payload.data(), _m.payload.size());
    _out.kind = static_cast<StationRequestKind>(r.ReadU8());
    _out.status = static_cast<StationStatus>(r.ReadU8());
    _out.credits = r.ReadI32();
    _out.commodity = r.ReadU16();
    _out.cargo = r.ReadU16();
    return r.Ok();
  }

  // --- Convenience: queue onto a reliable channel ---------------------------

  inline uint32_t SendStationRequest(ReliableChannel& _ch, const StationRequest& _r)
  {
    return _ch.Send(static_cast<uint16_t>(EventType::StationRequest), EncodeStationRequest(_r));
  }

  inline uint32_t SendStationResponse(ReliableChannel& _ch, const StationResponse& _r)
  {
    return _ch.Send(static_cast<uint16_t>(EventType::StationResponse), EncodeStationResponse(_r));
  }
}
