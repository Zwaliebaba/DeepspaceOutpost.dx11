#pragma once

// Camera - the client's single viewpoint (view + projection), decoupled from the ship.
//
// The legacy renderer fused the camera to the player's hull: the world was rebased
// AND rotated into the ship's basis on the CPU, the "view" was implicit, and the
// projection was a focal-length pixel mapping (the retired ViewMetrics /
// SceneProjection pair). This class replaces all of that with an explicit
// eye/lookAt/up view matrix and a fieldOfView/aspect perspective projection; the
// renderer (Scene3D) consumes View() and Projection() directly.
//
// Conventions:
//   - Left-handed, matching D3D11 and the engine's camera frame (x right, y up,
//     z forward): XMMatrixLookAtLH / XMMatrixPerspectiveFovLH.
//   - XMMATRIX results are DirectXMath row-vector matrices (p' = p * M). The
//     shaders consume column-vector matrices stored row-major, so Scene3D uploads
//     the transpose - see Scene3D::RenderModels.
//   - Floating origin: the world is an unbounded int64 space, so the game rebases
//     every entity around the camera's absolute eye each frame and hands this
//     class a SMALL origin-relative eye (usually the sub-unit fraction). Float
//     precision therefore never degrades far from the world origin (§3.2).
//
// The interface follows the classic DirectX sample camera. The per-eye stereo
// projections are part of that interface; with no stereo swap chain they return
// the mono projection.

#include <DirectXMath.h>

namespace Neuron::Client
{
  class Camera
  {
  public:
    Camera();
    Camera(Camera const&) = delete;
    void operator=(Camera const&) = delete;

    void SetViewParams(_In_ DirectX::XMFLOAT3 eye, _In_ DirectX::XMFLOAT3 lookAt, _In_ DirectX::XMFLOAT3 up);
    void SetProjParams(_In_ float fieldOfView, _In_ float aspectRatio, _In_ float nearPlane, _In_ float farPlane);

    void LookDirection(_In_ DirectX::XMFLOAT3 lookDirection);
    void Eye(_In_ DirectX::XMFLOAT3 position);

    DirectX::XMMATRIX View();
    DirectX::XMMATRIX Projection();
    DirectX::XMMATRIX LeftEyeProjection();
    DirectX::XMMATRIX RightEyeProjection();
    DirectX::XMMATRIX World();
    DirectX::XMFLOAT3 Eye();
    DirectX::XMFLOAT3 LookAt();
    DirectX::XMFLOAT3 Up();
    float NearClipPlane();
    float FarClipPlane();
    float Pitch();
    float Yaw();

  private:
    DirectX::XMFLOAT4X4 m_viewMatrix;
    DirectX::XMFLOAT4X4 m_projectionMatrix;
    DirectX::XMFLOAT4X4 m_projectionMatrixLeft;
    DirectX::XMFLOAT4X4 m_projectionMatrixRight;

    DirectX::XMFLOAT4X4 m_inverseView;

    DirectX::XMFLOAT3 m_eye;
    DirectX::XMFLOAT3 m_lookAt;
    DirectX::XMFLOAT3 m_up;
    float             m_cameraYawAngle;
    float             m_cameraPitchAngle;

    float             m_fieldOfView;
    float             m_aspectRatio;
    float             m_nearPlane;
    float             m_farPlane;
  };

  // The client's one live camera. Camera is non-copyable by design; every consumer
  // (the game's camera rig, gfx2d's projection setup, Scene3D, the CPU-projected
  // HUD bits) reads and writes this instance.
  [[nodiscard]] Camera& MainCamera();

  // ---- Scene optics constants (the legacy Elite look) -------------------------
  //
  // The legacy software projection was focal 512 px over a 192 px half-height:
  // tan(fovY/2) = 192/512 = 0.375, i.e. a vertical field of view of ~41.11 deg.
  // Rendering through XMMatrixPerspectiveFovLH with this fovY (and the live
  // aspect ratio) lands every point on exactly the same pixel the old focal
  // mapping produced. Near/far bracket the flight scene: near just in front of
  // the eye (the legacy path clamped z <= 0 to 1); far past the display cull
  // range (space.cpp removes local objects beyond 57344).
  inline constexpr float kLegacySceneFovY = 0.71754134f; // 2 * atan(0.375)
  inline constexpr float kSceneNearZ = 1.0f;
  inline constexpr float kSceneFarZ = 131072.0f;

  // ---- CPU projection helpers (the single camera path) ------------------------
  //
  // A few legacy presentation effects still project on the CPU (the explosion
  // debris, the firing beams, the missile-lock reticle, the dust starfield).
  // They derive their pixel math from the SAME projection matrix the GPU uses,
  // so there is one source of truth for the optics.

  // tan of the half field of view, horizontal / vertical, read back from the
  // projection matrix (P._11 = cot(fovX/2), P._22 = cot(fovY/2)). A camera-space
  // point is inside the frustum when |x| <= z * tanX and |y| <= z * tanY.
  [[nodiscard]] float CameraTanHalfFovX(Camera& _cam);
  [[nodiscard]] float CameraTanHalfFovY(Camera& _cam);

  // Focal length in pixels for a viewport of height _viewportH: the pixel scale
  // of the projection (viewportH/2 * cot(fovY/2)). At the legacy 512x384 retro
  // canvas this is exactly the legacy focal 512.
  [[nodiscard]] float CameraFocalPixels(Camera& _cam, float _viewportH);

  // Project a CAMERA-SPACE point (x right, y up, z forward) to viewport pixels
  // (x right, y DOWN), through the camera's projection and the standard D3D11
  // viewport transform. Returns false when the point is at or behind the eye.
  bool CameraSpaceToPixels(Camera& _cam, double _x, double _y, double _z,
                           int _viewportW, int _viewportH, double& _outSx, double& _outSy);
}
