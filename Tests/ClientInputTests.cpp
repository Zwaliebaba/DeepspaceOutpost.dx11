#include "TestFramework.h"

#include "DataWriter.h"
#include "DataReader.h"
#include "ClientInput.h"
#include "ReliableChannel.h"
#include "GameEvents.h"

using namespace Neuron;

TEST(Input_RoundTrips)
{
  Net::ClientInput in;
  in.sequence = 7;
  in.rollAxis = -1.0f;
  in.pitchAxis = 0.5f;
  in.throttle = 0.25f;

  Net::DataWriter w;
  Net::WriteInput(w, in);

  Net::ClientInput out;
  Net::DataReader r(w.Data(), w.Size());
  CHECK(Net::ReadInput(r, out));
  CHECK(out.sequence == 7);
  CHECK(out.rollAxis == -1.0f);
  CHECK(out.pitchAxis == 0.5f);
  CHECK(out.throttle == 0.25f);
}

TEST(Input_RejectsForeignMagic)
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
  CHECK(!Net::ReadInput(r, out));
}

TEST(Input_AssignPlayerHandshakeRoundTrips)
{
  Net::ReliableMessage m{ static_cast<uint16_t>(Net::EventType::AssignPlayer),
                          Net::EncodeAssignPlayer(123) };
  uint32_t id = 0;
  CHECK(Net::DecodeAssignPlayer(m, id));
  CHECK(id == 123);

  // A non-assign message is not mistaken for one.
  Net::ReliableMessage despawn{ static_cast<uint16_t>(Net::EventType::EntityDespawn),
                                Net::EncodeDespawn(5) };
  CHECK(!Net::DecodeAssignPlayer(despawn, id));
}

TEST(Input_AssignPlayerDeliversOverTheReliableChannel)
{
  Net::ReliableChannel server;
  Net::ReliableChannel client;

  Net::SendAssignPlayer(server, 42);
  std::vector<uint8_t> pkt = server.WritePacket();
  CHECK(client.ReadPacket(pkt.data(), pkt.size()));

  Net::ReliableMessage m;
  CHECK(client.Receive(m));
  uint32_t id = 0;
  CHECK(Net::DecodeAssignPlayer(m, id));
  CHECK(id == 42);
}
