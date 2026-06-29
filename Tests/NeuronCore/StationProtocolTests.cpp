#include <gtest/gtest.h>

#include <vector>

#include "StationProtocol.h"
#include "ReliableChannel.h"
#include "Messages/Reliable.h"

using namespace Neuron;

TEST(StationProto, RequestRoundTrips)
{
  Net::StationRequest req;
  req.kind = Net::StationRequestKind::Buy;
  req.commodity = 3;
  req.quantity = 12;
  req.stationId = 777;

  Net::ReliableMessage m{ Msg::Raw(Net::StationRequest::Id), Msg::Encode(req) };

  Net::StationRequest out;
  EXPECT_TRUE(Msg::TryDecode(m, out));
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

  Net::ReliableMessage m{ Msg::Raw(Net::StationResponse::Id), Msg::Encode(resp) };

  Net::StationResponse out;
  EXPECT_TRUE(Msg::TryDecode(m, out));
  EXPECT_TRUE(out.kind == Net::StationRequestKind::Sell);
  EXPECT_TRUE(out.status == Net::StationStatus::Ok);
  EXPECT_TRUE(out.credits == -1234);
  EXPECT_TRUE(out.commodity == 5);
  EXPECT_TRUE(out.cargo == 9);
}

// Byte-parity with the legacy hand-rolled StationProtocol codec (kind u8,
// commodity u16, quantity u16, stationId u32 - all little-endian).
TEST(StationProto, RequestPayloadMatchesLegacyByteLayout)
{
  Net::StationRequest req;
  req.kind = Net::StationRequestKind::Buy;   // = 3
  req.commodity = 3;
  req.quantity = 12;
  req.stationId = 777;

  Net::DataWriter golden;
  golden.WriteU8(3);
  golden.WriteU16(3);
  golden.WriteU16(12);
  golden.WriteU32(777);
  EXPECT_EQ(Msg::Encode(req), golden.Bytes());
}

TEST(StationProto, ResponsePayloadMatchesLegacyByteLayout)
{
  Net::StationResponse resp;
  resp.kind = Net::StationRequestKind::Sell;   // = 4
  resp.status = Net::StationStatus::Ok;        // = 0
  resp.credits = -1234;
  resp.commodity = 5;
  resp.cargo = 9;

  Net::DataWriter golden;
  golden.WriteU8(4);
  golden.WriteU8(0);
  golden.WriteI32(-1234);
  golden.WriteU16(5);
  golden.WriteU16(9);
  EXPECT_EQ(Msg::Encode(resp), golden.Bytes());
}

TEST(StationProto, DecodersRejectWrongType)
{
  Net::ReliableMessage resp{ Msg::Raw(Net::StationResponse::Id), Msg::Encode(Net::StationResponse{}) };
  Net::StationRequest req;
  EXPECT_TRUE(!Msg::TryDecode(resp, req));   // a response is not a request
}

TEST(StationProto, DeliversOverReliableChannel)
{
  Net::ReliableChannel client;
  Net::ReliableChannel server;

  Net::StationRequest req;
  req.kind = Net::StationRequestKind::Dock;
  req.stationId = 5;
  Msg::SendReliable(client, req);

  std::vector<uint8_t> pkt = client.WritePacket();
  EXPECT_TRUE(server.ReadPacket(pkt.data(), pkt.size()));

  Net::ReliableMessage m;
  EXPECT_TRUE(server.Receive(m));
  Net::StationRequest got;
  EXPECT_TRUE(Msg::TryDecode(m, got));
  EXPECT_TRUE(got.kind == Net::StationRequestKind::Dock);
  EXPECT_TRUE(got.stationId == 5);
}
