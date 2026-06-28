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

TEST(Scene, LocalPlayerIsTheOriginAndIsNotDrawn)
{
  std::vector<Net::EntitySnapshot> ents;
  ents.push_back(At(1, 1000, 0, 0));   // local player
  ents.push_back(At(2, 1100, 0, 0));   // 100 ahead on x
  ents[1].type = 2;                    // Coriolis - should pass through

  std::vector<Client::RenderRecord> recs = Client::BuildRenderRecords(ents, /*localPlayer*/ 1);

  EXPECT_TRUE(recs.size() == 1);
  EXPECT_TRUE(recs[0].id == 2);
  EXPECT_TRUE(recs[0].type == 2);             // replicated type carried to the render record
  EXPECT_TRUE(recs[0].location.x == 100.0);   // rebased relative to the local player
  EXPECT_TRUE(recs[0].location.y == 0.0);
  EXPECT_TRUE(recs[0].location.z == 0.0);
  EXPECT_TRUE(recs[0].distance == 100.0);
}

TEST(Scene, UnknownLocalPlayerRendersNothing)
{
  std::vector<Net::EntitySnapshot> ents;
  ents.push_back(At(2, 5, 6, 7));

  // We are not in the snapshot yet (unknown local player) -> draw nothing, rather
  // than rebase against a bogus origin.
  std::vector<Client::RenderRecord> recs = Client::BuildRenderRecords(ents, /*localPlayer*/ 999);
  EXPECT_TRUE(recs.empty());
}

TEST(Scene, OnlyTheLocalPlayerIsSkipped)
{
  std::vector<Net::EntitySnapshot> ents;
  ents.push_back(At(1, 0, 0, 0));
  ents.push_back(At(2, 10, 0, 0));   // local player
  ents.push_back(At(3, 20, 0, 0));

  std::vector<Client::RenderRecord> recs = Client::BuildRenderRecords(ents, 2);

  EXPECT_TRUE(recs.size() == 2);
  for (const Client::RenderRecord& r : recs)
    EXPECT_TRUE(r.id != 2);
}

TEST(Scene, CameraRotatesWithTheLocalShipRoll)
{
  // The local ship is rolled 90 degrees: nose still +z, but roof points +x.
  // A prop directly "above" in the world (+y) must therefore appear to the
  // camera-LEFT (negative side axis), proving the view rotates with the ship.
  std::vector<Net::EntitySnapshot> ents;
  Net::EntitySnapshot me = At(1, 0, 0, 0);
  me.noseX = 0.0f; me.noseY = 0.0f; me.noseZ = 1.0f;   // nose +z
  me.roofX = 1.0f; me.roofY = 0.0f; me.roofZ = 0.0f;   // roof +x (rolled)
  ents.push_back(me);
  ents.push_back(At(2, 0, 100, 0));                    // 100 "up" in the world

  std::vector<Client::RenderRecord> recs = Client::BuildRenderRecords(ents, /*localPlayer*/ 1);
  EXPECT_TRUE(recs.size() == 1);
  EXPECT_TRUE(recs[0].location.x == -100.0);   // world-up -> camera-left
  EXPECT_TRUE(recs[0].location.y == 0.0);
  EXPECT_TRUE(recs[0].location.z == 0.0);
  EXPECT_TRUE(recs[0].distance == 100.0);      // distance is frame-independent
}

TEST(Scene, RotmatIsBuiltFromTheOrientationBasis)
{
  std::vector<Net::EntitySnapshot> ents;
  ents.push_back(At(99, 0, 0, 0));                  // the local player (origin)
  Net::EntitySnapshot e = At(5, 0, 0, 0);
  e.noseX = 1.0f; e.noseY = 0.0f; e.noseZ = 0.0f;   // nose +x
  e.roofX = 0.0f; e.roofY = 0.0f; e.roofZ = 1.0f;   // roof +z
  ents.push_back(e);

  std::vector<Client::RenderRecord> recs = Client::BuildRenderRecords(ents, /*localPlayer*/ 99);
  EXPECT_TRUE(recs.size() == 1);

  // nose and roof preserved, side = roof x nose = (0,1,0).
  EXPECT_TRUE(recs[0].rotmat[2].x == 1.0);   // nose
  EXPECT_TRUE(recs[0].rotmat[1].z == 1.0);   // roof
  EXPECT_TRUE(recs[0].rotmat[0].x == 0.0);   // side
  EXPECT_TRUE(recs[0].rotmat[0].y == 1.0);
  EXPECT_TRUE(recs[0].rotmat[0].z == 0.0);
}
