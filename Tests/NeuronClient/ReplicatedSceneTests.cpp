#include <gtest/gtest.h>

#include <vector>

#include "Replication.h"
#include "ReplicatedScene.h"

using namespace Neuron;

namespace
{
  Net::EntitySnapshot At(uint32_t _id, int64_t _x, int64_t _y, int64_t _z)
  {
    Net::EntitySnapshot e;
    e.id = _id;
    e.x = _x;
    e.y = _y;
    e.z = _z;
    e.noseZ = 1.0f;   // default facing +z
    e.roofY = 1.0f;
    return e;
  }
}

TEST(Scene, RecordsAreRebasedAboutTheFloatingOrigin)
{
  // The camera is decoupled from the ship: records rebase about an arbitrary
  // floating origin (the camera eye) with NO rotation - the view transform is
  // the Camera's job at render time.
  std::vector<Net::EntitySnapshot> ents;
  ents.push_back(At(2, 1100, 50, -20));
  ents[0].type = 2;   // Coriolis - should pass through

  std::vector<Client::RenderRecord> recs = Client::BuildRenderRecords(ents, 1000, 50, -20);

  EXPECT_TRUE(recs.size() == 1);
  EXPECT_TRUE(recs[0].id == 2);
  EXPECT_TRUE(recs[0].type == 2);             // replicated type carried to the render record
  EXPECT_TRUE(recs[0].location.x == 100.0);   // world offset from the origin
  EXPECT_TRUE(recs[0].location.y == 0.0);
  EXPECT_TRUE(recs[0].location.z == 0.0);
  EXPECT_TRUE(recs[0].distance == 100.0);
}

TEST(Scene, EveryEntityIsIncludedIncludingTheLocalShip)
{
  // With no cockpit view, the player's own hull renders like any other entity:
  // nothing is skipped.
  std::vector<Net::EntitySnapshot> ents;
  ents.push_back(At(1, 0, 0, 0));
  ents.push_back(At(2, 10, 0, 0));   // the local player's ship
  ents.push_back(At(3, 20, 0, 0));

  std::vector<Client::RenderRecord> recs = Client::BuildRenderRecords(ents, 0, 0, 0);

  EXPECT_TRUE(recs.size() == 3);
  bool sawLocal = false;
  for (const Client::RenderRecord& r : recs)
    sawLocal = sawLocal || (r.id == 2);
  EXPECT_TRUE(sawLocal);
}

TEST(Scene, RebaseIsExactAtLargeCoordinates)
{
  // Floating-origin doctrine: the int64 subtraction happens BEFORE any float
  // math, so precision holds however far from the world origin the camera is.
  const int64_t big = 90000000;   // ~galaxy scale (systems span +/-100M)
  std::vector<Net::EntitySnapshot> ents;
  ents.push_back(At(7, big + 3, big - 4, big + 12));

  std::vector<Client::RenderRecord> recs = Client::BuildRenderRecords(ents, big, big, big);

  EXPECT_TRUE(recs.size() == 1);
  EXPECT_TRUE(recs[0].location.x == 3.0);
  EXPECT_TRUE(recs[0].location.y == -4.0);
  EXPECT_TRUE(recs[0].location.z == 12.0);
}

TEST(Scene, OrientationStaysInTheWorldFrame)
{
  // No rotation happens here: the record's basis is the entity's WORLD basis
  // (nose/roof replicated, side = roof x nose). The camera's view matrix
  // rotates it at render time.
  std::vector<Net::EntitySnapshot> ents;
  Net::EntitySnapshot e = At(5, 0, 0, 0);
  e.noseX = 1.0f; e.noseY = 0.0f; e.noseZ = 0.0f;   // nose +x
  e.roofX = 0.0f; e.roofY = 0.0f; e.roofZ = 1.0f;   // roof +z
  ents.push_back(e);

  std::vector<Client::RenderRecord> recs = Client::BuildRenderRecords(ents, 0, 0, 0);
  EXPECT_TRUE(recs.size() == 1);

  // nose and roof preserved, side = roof x nose = (0,1,0).
  EXPECT_TRUE(recs[0].rotmat[2].x == 1.0);   // nose
  EXPECT_TRUE(recs[0].rotmat[1].z == 1.0);   // roof
  EXPECT_TRUE(recs[0].rotmat[0].x == 0.0);   // side
  EXPECT_TRUE(recs[0].rotmat[0].y == 1.0);
  EXPECT_TRUE(recs[0].rotmat[0].z == 0.0);
}
