#include <gtest/gtest.h>

#include <cmath>

#include <DirectXMath.h>

#include "Camera.h"

using namespace DirectX;
using namespace Neuron;

namespace
{
  constexpr double kEps = 1e-4;

  // Project a camera-space point through the camera's Projection() and the
  // standard D3D11 viewport transform, mirroring what the GPU does.
  void ProjectThroughCamera(Client::Camera& _cam, double _x, double _y, double _z,
                            int _w, int _h, double& _sx, double& _sy)
  {
    XMFLOAT4X4 p;
    XMStoreFloat4x4(&p, _cam.Projection());

    // Row-vector convention: clip = pos * P.
    const double cx = _x * p._11;
    const double cy = _y * p._22;
    const double cw = _z;   // P._34 == 1 (left-handed perspective)

    _sx = (cx / cw * 0.5 + 0.5) * _w;
    _sy = (0.5 - cy / cw * 0.5) * _h;
  }
}

TEST(Camera, DefaultViewIsIdentity)
{
  Client::Camera cam;   // local instance: eye at origin looking +z, up +y

  XMFLOAT4X4 v;
  XMStoreFloat4x4(&v, cam.View());

  EXPECT_NEAR(v._11, 1.0f, kEps);
  EXPECT_NEAR(v._22, 1.0f, kEps);
  EXPECT_NEAR(v._33, 1.0f, kEps);
  EXPECT_NEAR(v._41, 0.0f, kEps);
  EXPECT_NEAR(v._42, 0.0f, kEps);
  EXPECT_NEAR(v._43, 0.0f, kEps);

  EXPECT_NEAR(cam.Yaw(), 0.0f, kEps);
  EXPECT_NEAR(cam.Pitch(), 0.0f, kEps);
}

TEST(Camera, ViewParamsRoundTripAndYawPitch)
{
  Client::Camera cam;
  cam.SetViewParams(XMFLOAT3(10.0f, 20.0f, 30.0f), XMFLOAT3(110.0f, 20.0f, 30.0f), XMFLOAT3(0.0f, 1.0f, 0.0f));

  const XMFLOAT3 eye = cam.Eye();
  EXPECT_NEAR(eye.x, 10.0f, kEps);
  EXPECT_NEAR(eye.y, 20.0f, kEps);
  EXPECT_NEAR(eye.z, 30.0f, kEps);

  // Looking down +x: yaw = atan2(x, z) = pi/2, pitch level.
  EXPECT_NEAR(cam.Yaw(), 1.5707963f, 1e-3f);
  EXPECT_NEAR(cam.Pitch(), 0.0f, 1e-3f);
}

TEST(Camera, WorldIsTheInverseOfView)
{
  Client::Camera cam;
  cam.SetViewParams(XMFLOAT3(5.0f, -3.0f, 12.0f), XMFLOAT3(6.0f, -2.0f, 20.0f), XMFLOAT3(0.0f, 1.0f, 0.0f));

  XMFLOAT4X4 id;
  XMStoreFloat4x4(&id, XMMatrixMultiply(cam.View(), cam.World()));

  for (int r = 0; r < 4; ++r)
    for (int c = 0; c < 4; ++c)
      EXPECT_NEAR(id.m[r][c], (r == c) ? 1.0f : 0.0f, 1e-3f);
}

TEST(Camera, ViewTransformsWorldPointsIntoCameraSpace)
{
  // Eye at (0,0,-10) looking +z: a world point at the origin lands 10 ahead.
  Client::Camera cam;
  cam.SetViewParams(XMFLOAT3(0.0f, 0.0f, -10.0f), XMFLOAT3(0.0f, 0.0f, 0.0f), XMFLOAT3(0.0f, 1.0f, 0.0f));

  const XMVECTOR p = XMVector3TransformCoord(XMVectorZero(), cam.View());
  EXPECT_NEAR(XMVectorGetX(p), 0.0f, kEps);
  EXPECT_NEAR(XMVectorGetY(p), 0.0f, kEps);
  EXPECT_NEAR(XMVectorGetZ(p), 10.0f, kEps);
}

TEST(Camera, ProjectionReproducesTheLegacyOptics)
{
  // The pixel-fidelity guarantee the retired SceneProjection tests carried: at
  // the retro 512x384 canvas with the legacy vertical field of view, a camera
  // point must land on the legacy focal-512 mapping sx = x*512/z + 256,
  // sy = -y*512/z + 192.
  Client::Camera cam;
  cam.SetProjParams(Client::LEGACY_SCENE_FOV_Y, 512.0f / 384.0f, Client::SCENE_NEAR_Z, Client::SCENE_FAR_Z);

  const double pts[][3] = {
    {0.0, 0.0, 100.0},
    {30.0, -20.0, 250.0},
    {-75.0, 40.0, 1000.0},
    {5.0, 5.0, 2.0},
  };

  for (const double* p : pts)
  {
    double sx = 0.0, sy = 0.0;
    ProjectThroughCamera(cam, p[0], p[1], p[2], 512, 384, sx, sy);

    const double refX = (p[0] * 512.0) / p[2] + 256.0;
    const double refY = -(p[1] * 512.0) / p[2] + 192.0;
    EXPECT_NEAR(sx, refX, 0.01);
    EXPECT_NEAR(sy, refY, 0.01);
  }
}

TEST(Camera, HelpersDeriveFromTheProjection)
{
  Client::Camera cam;
  cam.SetProjParams(Client::LEGACY_SCENE_FOV_Y, 512.0f / 384.0f, Client::SCENE_NEAR_Z, Client::SCENE_FAR_Z);

  // Legacy focal: 512 px at a 384-high canvas (tan(fovY/2) = 192/512).
  EXPECT_NEAR(Client::CameraFocalPixels(cam, 384.0f), 512.0f, 0.05f);
  EXPECT_NEAR(Client::CameraTanHalfFovY(cam), 0.375f, 1e-4f);
  // Same focal on both axes: tanX = tanY * aspect.
  EXPECT_NEAR(Client::CameraTanHalfFovX(cam), 0.375f * (512.0f / 384.0f), 1e-4f);

  // The pixel helper agrees with the matrix path.
  double sx = 0.0, sy = 0.0;
  EXPECT_TRUE(Client::CameraSpaceToPixels(cam, 30.0, -20.0, 250.0, 512, 384, sx, sy));
  EXPECT_NEAR(sx, (30.0 * 512.0) / 250.0 + 256.0, 0.05);
  EXPECT_NEAR(sy, -(-20.0 * 512.0) / 250.0 + 192.0, 0.05);

  // Behind the eye -> rejected.
  EXPECT_FALSE(Client::CameraSpaceToPixels(cam, 0.0, 0.0, -5.0, 512, 384, sx, sy));
}

TEST(Camera, LookDirectionAndEyeSettersRecomputeTheView)
{
  Client::Camera cam;
  cam.SetViewParams(XMFLOAT3(0.0f, 0.0f, 0.0f), XMFLOAT3(0.0f, 0.0f, 1.0f), XMFLOAT3(0.0f, 1.0f, 0.0f));

  cam.LookDirection(XMFLOAT3(1.0f, 0.0f, 0.0f));
  EXPECT_NEAR(cam.Yaw(), 1.5707963f, 1e-3f);

  cam.Eye(XMFLOAT3(0.0f, 7.0f, 0.0f));
  const XMFLOAT3 eye = cam.Eye();
  EXPECT_NEAR(eye.y, 7.0f, kEps);
}
