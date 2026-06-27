#include "TestFramework.h"

#include "CameraFollow.h"

using namespace Neuron;

namespace
{
  // Identity ship orientation (nose = +z), the default facing.
  const Math::Vector3d kSide{ 1.0, 0.0, 0.0 };
  const Math::Vector3d kRoof{ 0.0, 1.0, 0.0 };
  const Math::Vector3d kNose{ 0.0, 0.0, 1.0 };
}

TEST(CameraFollow_CockpitSitsExactlyOnTheShip)
{
  Client::CameraPose pose = Client::FollowShip(
    Math::Vector3i64{ 1000, -2000, 3000 }, kSide, kRoof, kNose,
    Client::ViewOffset{ 0.0, 0.0, 0.0 });

  CHECK((pose.eye == Math::Vector3i64{ 1000, -2000, 3000 }));
}

TEST(CameraFollow_ChaseOffsetsBehindAndAbove)
{
  // Up 12, back 45 (forward = -45) with the ship facing +z.
  Client::CameraPose pose = Client::FollowShip(
    Math::Vector3i64{ 0, 0, 0 }, kSide, kRoof, kNose,
    Client::ViewOffset{ /*right*/ 0.0, /*up*/ 12.0, /*forward*/ -45.0 });

  CHECK((pose.eye == Math::Vector3i64{ 0, 12, -45 }));
}

TEST(CameraFollow_OffsetRotatesWithTheShip)
{
  // Ship yawed to face +x: nose=(1,0,0), roof=(0,1,0), side=roof x nose=(0,0,-1).
  const Math::Vector3d nose{ 1.0, 0.0, 0.0 };
  const Math::Vector3d roof{ 0.0, 1.0, 0.0 };
  const Math::Vector3d side{ 0.0, 0.0, -1.0 };

  Client::CameraPose pose = Client::FollowShip(
    Math::Vector3i64{ 100, 0, 0 }, side, roof, nose,
    Client::ViewOffset{ /*right*/ 0.0, /*up*/ 12.0, /*forward*/ -45.0 });

  // "Behind" is now along -x, so the eye trails the ship on the x axis.
  CHECK((pose.eye == Math::Vector3i64{ 55, 12, 0 }));
}

TEST(CameraFollow_RoundsToNearestWorldUnit)
{
  Client::CameraPose pose = Client::FollowShip(
    Math::Vector3i64{ 0, 0, 0 }, kSide, kRoof, kNose,
    Client::ViewOffset{ /*right*/ 0.0, /*up*/ 12.4, /*forward*/ -45.6 });

  // llround: 12.4 -> 12, -45.6 -> -46.
  CHECK((pose.eye == Math::Vector3i64{ 0, 12, -46 }));
}

TEST(CameraFollow_PoseCarriesShipFacing)
{
  Client::CameraPose pose = Client::FollowShip(
    Math::Vector3i64{ 0, 0, 0 }, kSide, kRoof, kNose,
    Client::ViewOffset{ 0.0, 0.0, 0.0 });

  // A rigid follow looks along the ship's nose.
  CHECK(pose.nose.x == kNose.x);
  CHECK(pose.nose.y == kNose.y);
  CHECK(pose.nose.z == kNose.z);
}
