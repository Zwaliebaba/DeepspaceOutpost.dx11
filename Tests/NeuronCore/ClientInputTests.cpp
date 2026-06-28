#include <gtest/gtest.h>

#include "DataWriter.h"
#include "DataReader.h"
#include "ClientInput.h"
#include "ReliableChannel.h"
#include "GameEvents.h"

using namespace Neuron;

TEST(Input, RoundTrips)
{
  Net::ClientInput in;
  in.sequence = 7;
  in.rollAxis = -1.0f;
  in.pitchAxis = 0.5f;
  in.throttle = 0.25f;
  in.fire = true;
  in.fireMissile = true;

  Net::DataWriter w;
  Net::WriteInput(w, in);

  Net::ClientInput out;
  Net::DataReader r(w.Data(), w.Size());
  EXPECT_TRUE(Net::ReadInput(r, out));
  EXPECT_TRUE(out.sequence == 7);
  EXPECT_TRUE(out.rollAxis == -1.0f);
  EXPECT_TRUE(out.pitchAxis == 0.5f);
  EXPECT_TRUE(out.throttle == 0.25f);
  EXPECT_TRUE(out.fire == true);
  EXPECT_TRUE(out.fireMissile == true);
}

TEST(Input, RejectsForeignMagic)
{
  Net::DataWriter w;
  w.WriteU32(0xDEADBEEF);
  w.WriteU16(1);
  w.WriteU32(0);
  w.WriteF32(0.0f);
  w.WriteF32(0.0f);
  w.WriteF32(0.0f);

  Net::ClientInput out;
  Net::DataReader r(w.Data(), w.Size());
  EXPECT_TRUE(!Net::ReadInput(r, out));
}

TEST(Input, AssignPlayerHandshakeRoundTrips)
{
  Net::ReliableMessage m{ static_cast<uint16_t>(Net::EventType::AssignPlayer),
                          Net::EncodeAssignPlayer(123) };
  uint32_t id = 0;
  EXPECT_TRUE(Net::DecodeAssignPlayer(m, id));
  EXPECT_TRUE(id == 123);

  // A non-assign message is not mistaken for one.
  Net::ReliableMessage despawn{ static_cast<uint16_t>(Net::EventType::EntityDespawn),
                                Net::EncodeDespawn(5) };
  EXPECT_TRUE(!Net::DecodeAssignPlayer(despawn, id));
}

TEST(Input, AssignPlayerDeliversOverTheReliableChannel)
{
  Net::ReliableChannel server;
  Net::ReliableChannel client;

  Net::SendAssignPlayer(server, 42);
  std::vector<uint8_t> pkt = server.WritePacket();
  EXPECT_TRUE(client.ReadPacket(pkt.data(), pkt.size()));

  Net::ReliableMessage m;
  EXPECT_TRUE(client.Receive(m));
  uint32_t id = 0;
  EXPECT_TRUE(Net::DecodeAssignPlayer(m, id));
  EXPECT_TRUE(id == 42);
}
