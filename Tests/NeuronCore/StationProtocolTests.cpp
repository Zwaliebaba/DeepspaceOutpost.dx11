#include <gtest/gtest.h>

#include "StationProtocol.h"
#include "ReliableChannel.h"

using namespace Neuron;

TEST(StationProto, RequestRoundTrips)
{
  Net::StationRequest req;
  req.kind = Net::StationRequestKind::Buy;
  req.commodity = 3;
  req.quantity = 12;
  req.stationId = 777;

  Net::ReliableMessage m{ static_cast<uint16_t>(Net::EventType::StationRequest),
                          Net::EncodeStationRequest(req) };

  Net::StationRequest out;
  EXPECT_TRUE(Net::DecodeStationRequest(m, out));
  EXPECT_TRUE(out.kind == Net::StationRequestKind::Buy);
  EXPECT_TRUE(out.commodity == 3);
  EXPECT_TRUE(out.quantity == 12);
  EXPECT_TRUE(out.stationId == 777);
}

TEST(StationProto, ResponseRoundTrips)
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
  EXPECT_TRUE(Net::DecodeStationResponse(m, out));
  EXPECT_TRUE(out.kind == Net::StationRequestKind::Sell);
  EXPECT_TRUE(out.status == Net::StationStatus::Ok);
  EXPECT_TRUE(out.credits == -1234);
  EXPECT_TRUE(out.commodity == 5);
  EXPECT_TRUE(out.cargo == 9);
}

TEST(StationProto, DecodersRejectWrongType)
{
  Net::ReliableMessage resp{ static_cast<uint16_t>(Net::EventType::StationResponse),
                             Net::EncodeStationResponse(Net::StationResponse{}) };
  Net::StationRequest req;
  EXPECT_TRUE(!Net::DecodeStationRequest(resp, req));   // a response is not a request
}

TEST(StationProto, DeliversOverReliableChannel)
{
  Net::ReliableChannel client;
  Net::ReliableChannel server;

  Net::StationRequest req;
  req.kind = Net::StationRequestKind::Dock;
  req.stationId = 5;
  Net::SendStationRequest(client, req);

  std::vector<uint8_t> pkt = client.WritePacket();
  EXPECT_TRUE(server.ReadPacket(pkt.data(), pkt.size()));

  Net::ReliableMessage m;
  EXPECT_TRUE(server.Receive(m));
  Net::StationRequest got;
  EXPECT_TRUE(Net::DecodeStationRequest(m, got));
  EXPECT_TRUE(got.kind == Net::StationRequestKind::Dock);
  EXPECT_TRUE(got.stationId == 5);
}
