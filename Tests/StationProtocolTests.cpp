#include "TestFramework.h"

#include "StationProtocol.h"
#include "ReliableChannel.h"

using namespace Neuron;

TEST(StationProto_RequestRoundTrips)
{
  Net::StationRequest req;
  req.kind = Net::StationRequestKind::Buy;
  req.commodity = 3;
  req.quantity = 12;
  req.stationId = 777;

  Net::ReliableMessage m{ static_cast<uint16_t>(Net::EventType::StationRequest),
                          Net::EncodeStationRequest(req) };

  Net::StationRequest out;
  CHECK(Net::DecodeStationRequest(m, out));
  CHECK(out.kind == Net::StationRequestKind::Buy);
  CHECK(out.commodity == 3);
  CHECK(out.quantity == 12);
  CHECK(out.stationId == 777);
}

TEST(StationProto_ResponseRoundTrips)
{
  Net::StationResponse resp;
  resp.kind = Net::StationRequestKind::Sell;
  resp.status = Net::StationStatus::Ok;
  resp.credits = -1234;            // signed
  resp.commodity = 5;
  resp.cargo = 9;

  Net::ReliableMessage m{ static_cast<uint16_t>(Net::EventType::StationResponse),
                          Net::EncodeStationResponse(resp) };

  Net::StationResponse out;
  CHECK(Net::DecodeStationResponse(m, out));
  CHECK(out.kind == Net::StationRequestKind::Sell);
  CHECK(out.status == Net::StationStatus::Ok);
  CHECK(out.credits == -1234);
  CHECK(out.commodity == 5);
  CHECK(out.cargo == 9);
}

TEST(StationProto_DecodersRejectWrongType)
{
  Net::ReliableMessage resp{ static_cast<uint16_t>(Net::EventType::StationResponse),
                             Net::EncodeStationResponse(Net::StationResponse{}) };
  Net::StationRequest req;
  CHECK(!Net::DecodeStationRequest(resp, req));   // a response is not a request
}

TEST(StationProto_DeliversOverReliableChannel)
{
  Net::ReliableChannel client;
  Net::ReliableChannel server;

  Net::StationRequest req;
  req.kind = Net::StationRequestKind::Dock;
  req.stationId = 5;
  Net::SendStationRequest(client, req);

  std::vector<uint8_t> pkt = client.WritePacket();
  CHECK(server.ReadPacket(pkt.data(), pkt.size()));

  Net::ReliableMessage m;
  CHECK(server.Receive(m));
  Net::StationRequest got;
  CHECK(Net::DecodeStationRequest(m, got));
  CHECK(got.kind == Net::StationRequestKind::Dock);
  CHECK(got.stationId == 5);
}
