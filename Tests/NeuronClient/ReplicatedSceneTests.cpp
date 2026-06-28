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
  // Render records use the legacy starfield convention (the world rotates around a
  // fixed cockpit: offset rebuilt as x*side + y*roof + z*nose), so a prop directly
  // "above" in the world (+y) is carried onto the +roof axis, which now points to
  // world +x -> camera location.x = +100. This is the transpose of a textbook view
  // matrix, and it is what keeps replicated objects rotating WITH the local stars.
  std::vector<Net::EntitySnapshot> ents;
  Net::EntitySnapshot me = At(1, 0, 0, 0);
  me.noseX = 0.0f; me.noseY = 0.0f; me.noseZ = 1.0f;   // nose +z
  me.roofX = 1.0f; me.roofY = 0.0f; me.roofZ = 0.0f;   // roof +x (rolled)
  ents.push_back(me);
  ents.push_back(At(2, 0, 100, 0));                    // 100 "up" in the world

  std::vector<Client::RenderRecord> recs = Client::BuildRenderRecords(ents, /*localPlayer*/ 1);
  EXPECT_TRUE(recs.size() == 1);
  EXPECT_TRUE(recs[0].location.x == 100.0);    // world-up carried onto +roof (now world +x)
  EXPECT_TRUE(recs[0].location.y == 0.0);
  EXPECT_TRUE(recs[0].location.z == 0.0);
  EXPECT_TRUE(recs[0].distance == 100.0);      // distance is frame-independent
}

TEST(Scene, RollMatchesTheLegacyStarfieldDirection)
{
  // A small right-roll tilts the roof toward +x (roof = (a,1,0)). A prop straight
  // "up" in the world (+y) must then shift to camera +x, exactly as the legacy
  // move_local_object()/starfield rotates it: (x,y) -> (x + a*y, y - a*x). A
  // textbook view projection would shift it to -x instead, i.e. counter to the
  // local stars - the bug this convention fixes.
  const double a = 0.1;
  std::vector<Net::EntitySnapshot> ents;
  Net::EntitySnapshot me = At(1, 0, 0, 0);
  me.noseX = 0.0f;        me.noseY = 0.0f; me.noseZ = 1.0f;   // nose +z
  me.roofX = (float)a;    me.roofY = 1.0f; me.roofZ = 0.0f;   // roof tilted toward +x (rolled)
  ents.push_back(me);
  ents.push_back(At(2, 0, 100, 0));                           // 100 "up" in the world

  std::vector<Client::RenderRecord> recs = Client::BuildRenderRecords(ents, /*localPlayer*/ 1);
  EXPECT_TRUE(recs.size() == 1);
  EXPECT_TRUE(recs[0].location.x > 0.0);   // world-up -> camera +x (with the stars), not -x
  EXPECT_TRUE(recs[0].location.y > 0.0);
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

  // nose and roof preserved, side = roof x nose = (0,1,0). rotmat rows are
  // side (_1x), roof (_2x), nose (_3x) in the XMFLOAT4X4.
  EXPECT_TRUE(recs[0].rotmat._31 == 1.0f);   // nose.x
  EXPECT_TRUE(recs[0].rotmat._23 == 1.0f);   // roof.z
  EXPECT_TRUE(recs[0].rotmat._11 == 0.0f);   // side.x
  EXPECT_TRUE(recs[0].rotmat._12 == 1.0f);   // side.y
  EXPECT_TRUE(recs[0].rotmat._13 == 0.0f);   // side.z
}
