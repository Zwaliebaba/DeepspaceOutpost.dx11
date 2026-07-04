#include <gtest/gtest.h>

#include <cmath>

#include "Quantization.h"
#include "Replication.h"

using namespace Neuron;

// --- the primitives -----------------------------------------------------------

TEST(Quantization, AxisAlignedUnitComponentsAreExact)
{
  // 0 and +/-1 are the common (unrotated) basis values and must survive exactly,
  // so an axis-aligned ship frame is lossless on the wire.
  EXPECT_EQ(Net::QuantizeUnit(0.0f), 0);
  EXPECT_EQ(Net::QuantizeUnit(1.0f), 32767);
  EXPECT_EQ(Net::QuantizeUnit(-1.0f), -32767);
  EXPECT_FLOAT_EQ(Net::DequantizeUnit(Net::QuantizeUnit(0.0f)), 0.0f);
  EXPECT_FLOAT_EQ(Net::DequantizeUnit(Net::QuantizeUnit(1.0f)), 1.0f);
  EXPECT_FLOAT_EQ(Net::DequantizeUnit(Net::QuantizeUnit(-1.0f)), -1.0f);
}

TEST(Quantization, OffAxisComponentsRoundTripWithinTheGrid)
{
  for (float v : { 0.5773f, -0.5773f, 0.25f, -0.9f, 0.123456f })
  {
    const float back = Net::DequantizeUnit(Net::QuantizeUnit(v));
    EXPECT_LT(std::fabs(back - v), 3.1e-5f);   // <= half a 1/32767 grid step
  }
}

TEST(Quantization, OutOfRangeComponentsClampToTheUnitInterval)
{
  EXPECT_EQ(Net::QuantizeUnit(2.5f), 32767);
  EXPECT_EQ(Net::QuantizeUnit(-2.5f), -32767);
}

TEST(Quantization, IntegerAndHalfSpeedsAreExactAndNegativesClampToZero)
{
  EXPECT_FLOAT_EQ(Net::DequantizeSpeed(Net::QuantizeSpeed(0.0f)), 0.0f);
  EXPECT_FLOAT_EQ(Net::DequantizeSpeed(Net::QuantizeSpeed(42.0f)), 42.0f);    // n*256/256 == n
  EXPECT_FLOAT_EQ(Net::DequantizeSpeed(Net::QuantizeSpeed(12.5f)), 12.5f);    // 12.5*256 = 3200, exact
  EXPECT_FLOAT_EQ(Net::DequantizeSpeed(Net::QuantizeSpeed(-5.0f)), 0.0f);     // clamped
}

TEST(Quantization, FractionalSpeedRoundTripsWithinAQuantum)
{
  const float back = Net::DequantizeSpeed(Net::QuantizeSpeed(12.3f));
  EXPECT_LT(std::fabs(back - 12.3f), 1.0f / Net::SPEED_QUANT_SCALE);
}

// --- the v2 snapshot wire format ----------------------------------------------

TEST(SnapshotV2, PositionsRoundTripExactlyAcrossTheGalaxyExtent)
{
  Net::WorldSnapshot in;
  in.tick = 12345;
  in.viewerId = 7;
  Net::EntitySnapshot e;
  e.id = 99;
  e.x = 200'000'000;    // near the +/-~2.2e8 galaxy edge - still exact in int32
  e.y = -150'000'000;
  e.z = 20'000'000;     // a witchspace-displacement-scale coordinate
  e.type = -1;          // a negative ship type (Planet) must survive the u16 round trip
  in.entities.push_back(e);

  Net::DataWriter w;
  Net::WriteSnapshot(w, in);
  Net::DataReader r(w.Bytes().data(), w.Size());
  Net::WorldSnapshot out;
  ASSERT_TRUE(Net::ReadSnapshot(r, out));

  ASSERT_EQ(out.entities.size(), 1u);
  const Net::EntitySnapshot& d = out.entities[0];
  EXPECT_EQ(out.tick, 12345u);
  EXPECT_EQ(out.viewerId, 7u);
  EXPECT_EQ(d.id, 99u);
  EXPECT_EQ(d.x, 200'000'000);   // exact - the whole point of int32 over quantized floats
  EXPECT_EQ(d.y, -150'000'000);
  EXPECT_EQ(d.z, 20'000'000);
  EXPECT_EQ(d.type, -1);
}

TEST(SnapshotV2, AnUnrotatedBasisAndIntegerSpeedAreLossless)
{
  Net::WorldSnapshot in;
  Net::EntitySnapshot e;
  e.id = 1;
  e.noseZ = 1.0f; e.roofY = 1.0f;   // the default axis-aligned frame
  e.speed = 15.0f;                  // whole units/tick
  in.entities.push_back(e);

  Net::DataWriter w;
  Net::WriteSnapshot(w, in);
  Net::DataReader r(w.Bytes().data(), w.Size());
  Net::WorldSnapshot out;
  ASSERT_TRUE(Net::ReadSnapshot(r, out));

  const Net::EntitySnapshot& d = out.entities[0];
  EXPECT_FLOAT_EQ(d.noseX, 0.0f);
  EXPECT_FLOAT_EQ(d.noseZ, 1.0f);
  EXPECT_FLOAT_EQ(d.roofY, 1.0f);
  EXPECT_FLOAT_EQ(d.speed, 15.0f);   // exact
}

TEST(SnapshotV2, ARotatedBasisRoundTripsWithinTheQuantizationGrid)
{
  Net::WorldSnapshot in;
  Net::EntitySnapshot e;
  e.id = 1;
  // A normalized off-axis nose + an orthogonal roof.
  const float inv = 1.0f / std::sqrt(3.0f);
  e.noseX = inv; e.noseY = inv; e.noseZ = inv;
  e.roofX = -inv; e.roofY = 2.0f * inv; e.roofZ = -inv;   // (roof . nose == 0)
  e.speed = 7.4f;
  in.entities.push_back(e);

  Net::DataWriter w;
  Net::WriteSnapshot(w, in);
  Net::DataReader r(w.Bytes().data(), w.Size());
  Net::WorldSnapshot out;
  ASSERT_TRUE(Net::ReadSnapshot(r, out));

  const Net::EntitySnapshot& d = out.entities[0];
  EXPECT_LT(std::fabs(d.noseX - inv), 3.1e-5f);
  EXPECT_LT(std::fabs(d.noseY - inv), 3.1e-5f);
  EXPECT_LT(std::fabs(d.noseZ - inv), 3.1e-5f);
  EXPECT_LT(std::fabs(d.roofY - 2.0f * inv), 3.1e-5f);
  EXPECT_LT(std::fabs(d.speed - 7.4f), 1.0f / Net::SPEED_QUANT_SCALE);
}

TEST(SnapshotV2, EntityWireSizeMatchesTheAdvertisedConstant)
{
  Net::WorldSnapshot in;
  in.entities.push_back(Net::EntitySnapshot{});
  Net::DataWriter w;
  Net::WriteSnapshot(w, in);
  EXPECT_EQ(w.Size(), Net::SNAPSHOT_HEADER_SIZE + Net::SNAPSHOT_ENTITY_SIZE);
}
