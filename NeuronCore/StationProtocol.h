#pragma once

// StationProtocol - the docked request/response wire schema (NeuronCore).
//
// Docking, trading and equip are turn-based and must be reliable and ordered, so
// unlike the realtime snapshot stream they ride a reliable lane as request->response:
// the client asks (Dock/Buy/Sell), the server validates against its AUTHORITATIVE
// wallet/cargo/market and replies with the result. Shared data/protocol only - the
// structs and their encoding - so both ends agree on the bytes; the actual
// validation lives server-side (GameLogic StationServices).
//
// StationRequest/StationResponse are catalog messages (typed structs described once
// via Fields(); generic Serialize codec). They are also exposed under their historic
// Net:: names so existing call sites (GameLogic, client) are unchanged. The generic
// encoding is byte-identical to the old hand-rolled codec (see the parity tests).

#include <cstdint>

#include "Messages/Registry.h"   // MessageId/Traits/Serialize + REGISTER_MESSAGE

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
}

namespace Neuron::Msg
{
  // client -> server: dock/undock/buy/sell/equip/teleport.
  struct StationRequest
  {
    static constexpr MessageId    Id    = static_cast<MessageId>(0x0400);   // station/economy
    static constexpr MessageScope Scope = MessageScope::Wire;
    static constexpr MessageKind  Kind  = MessageKind::Command;
    static constexpr MessageLane  Lane  = MessageLane::Gameplay;
    static constexpr Direction    Dir   = Direction::ClientToServer;

    Net::StationRequestKind kind = Net::StationRequestKind::Dock;
    uint16_t commodity = 0;
    uint16_t quantity = 0;
    uint32_t stationId = 0;

    auto Fields()       { return std::tie(kind, commodity, quantity, stationId); }
    auto Fields() const { return std::tie(kind, commodity, quantity, stationId); }
  };

  // server -> client: the authoritative result.
  struct StationResponse
  {
    static constexpr MessageId    Id    = static_cast<MessageId>(0x0401);
    static constexpr MessageScope Scope = MessageScope::Wire;
    static constexpr MessageKind  Kind  = MessageKind::Event;
    static constexpr MessageLane  Lane  = MessageLane::Gameplay;
    static constexpr Direction    Dir   = Direction::ServerToClient;

    Net::StationRequestKind kind = Net::StationRequestKind::Dock;
    Net::StationStatus status = Net::StationStatus::Ok;
    int32_t credits = 0;
    uint16_t commodity = 0;
    uint16_t cargo = 0;     // resulting held quantity of `commodity`

    auto Fields()       { return std::tie(kind, status, credits, commodity, cargo); }
    auto Fields() const { return std::tie(kind, status, credits, commodity, cargo); }
  };
}

namespace Neuron::Net
{
  // Historic names; the structs themselves are catalog messages in Neuron::Msg.
  using StationRequest = Msg::StationRequest;
  using StationResponse = Msg::StationResponse;
}

REGISTER_MESSAGE(StationRequest);
REGISTER_MESSAGE(StationResponse);
