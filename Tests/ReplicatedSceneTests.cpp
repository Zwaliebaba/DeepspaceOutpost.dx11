#include "TestFramework.h"

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

TEST(Scene_LocalPlayerIsTheOriginAndIsNotDrawn)
{
  std::vector<Net::EntitySnapshot> ents;
  ents.push_back(At(1, 1000, 0, 0));   // local player
  ents.push_back(At(2, 1100, 0, 0));   // 100 ahead on x
  ents[1].type = 2;                    // Coriolis - should pass through

  std::vector<Client::RenderRecord> recs = Client::BuildRenderRecords(ents, /*localPlayer*/ 1);

  CHECK(recs.size() == 1);
  CHECK(recs[0].id == 2);
  CHECK(recs[0].type == 2);             // replicated type carried to the render record
  CHECK(recs[0].location.x == 100.0);   // rebased relative to the local player
  CHECK(recs[0].location.y == 0.0);
  CHECK(recs[0].location.z == 0.0);
  CHECK(recs[0].distance == 100.0);
}

TEST(Scene_UnknownLocalPlayerRendersNothing)
{
  std::vector<Net::EntitySnapshot> ents;
  ents.push_back(At(2, 5, 6, 7));

  // We are not in the snapshot yet (unknown local player) -> draw nothing, rather
  // than rebase against a bogus origin.
  std::vector<Client::RenderRecord> recs = Client::BuildRenderRecords(ents, /*localPlayer*/ 999);
  CHECK(recs.empty());
}

TEST(Scene_OnlyTheLocalPlayerIsSkipped)
{
  std::vector<Net::EntitySnapshot> ents;
  ents.push_back(At(1, 0, 0, 0));
  ents.push_back(At(2, 10, 0, 0));   // local player
  ents.push_back(At(3, 20, 0, 0));

  std::vector<Client::RenderRecord> recs = Client::BuildRenderRecords(ents, 2);

  CHECK(recs.size() == 2);
  for (const Client::RenderRecord& r : recs)
    CHECK(r.id != 2);
}

TEST(Scene_RotmatIsBuiltFromTheOrientationBasis)
{
  std::vector<Net::EntitySnapshot> ents;
  ents.push_back(At(99, 0, 0, 0));                  // the local player (origin)
  Net::EntitySnapshot e = At(5, 0, 0, 0);
  e.noseX = 1.0f; e.noseY = 0.0f; e.noseZ = 0.0f;   // nose +x
  e.roofX = 0.0f; e.roofY = 0.0f; e.roofZ = 1.0f;   // roof +z
  ents.push_back(e);

  std::vector<Client::RenderRecord> recs = Client::BuildRenderRecords(ents, /*localPlayer*/ 99);
  CHECK(recs.size() == 1);

  // nose and roof preserved, side = roof x nose = (0,1,0).
  CHECK(recs[0].rotmat[2].x == 1.0);   // nose
  CHECK(recs[0].rotmat[1].z == 1.0);   // roof
  CHECK(recs[0].rotmat[0].x == 0.0);   // side
  CHECK(recs[0].rotmat[0].y == 1.0);
  CHECK(recs[0].rotmat[0].z == 0.0);
}
