#include "pch.h"

#include "Camera.h"

#include <cmath>

using namespace DirectX;

namespace Neuron::Client
{
  Camera::Camera()
  {
    // Faithful defaults: an identity view at the origin looking down +z, and the
    // legacy Elite optics at the retro 4:3 aspect. gfx_set_scene_fullwindow
    // re-issues SetProjParams with the live aspect every frame.
    SetViewParams(XMFLOAT3(0.0f, 0.0f, 0.0f), XMFLOAT3(0.0f, 0.0f, 1.0f), XMFLOAT3(0.0f, 1.0f, 0.0f));
    SetProjParams(LEGACY_SCENE_FOV_Y, 4.0f / 3.0f, SCENE_NEAR_Z, SCENE_FAR_Z);
  }

  void Camera::SetViewParams(XMFLOAT3 eye, XMFLOAT3 lookAt, XMFLOAT3 up)
  {
    m_eye = eye;
    m_lookAt = lookAt;
    m_up = up;

    // A degenerate look target (== eye) would make LookAtLH blow up; nudge it
    // forward so the camera always has a valid view.
    if (m_lookAt.x == m_eye.x && m_lookAt.y == m_eye.y && m_lookAt.z == m_eye.z)
      m_lookAt.z += 1.0f;

    const XMMATRIX view = XMMatrixLookAtLH(XMLoadFloat3(&m_eye), XMLoadFloat3(&m_lookAt), XMLoadFloat3(&m_up));
    XMStoreFloat4x4(&m_viewMatrix, view);

    const XMMATRIX inverseView = XMMatrixInverse(nullptr, view);
    XMStoreFloat4x4(&m_inverseView, inverseView);

    // The axis basis vectors and camera position live in the rows of the camera's
    // world (inverse view) matrix; yaw/pitch fall out of the Z basis vector.
    XMFLOAT3 zBasis;
    XMStoreFloat3(&zBasis, inverseView.r[2]);
    m_cameraYawAngle = atan2f(zBasis.x, zBasis.z);
    const float len = sqrtf(zBasis.z * zBasis.z + zBasis.x * zBasis.x);
    m_cameraPitchAngle = atan2f(zBasis.y, len);
  }

  void Camera::SetProjParams(float fieldOfView, float aspectRatio, float nearPlane, float farPlane)
  {
    m_fieldOfView = fieldOfView;
    m_aspectRatio = aspectRatio;
    m_nearPlane = nearPlane;
    m_farPlane = farPlane;

    XMStoreFloat4x4(&m_projectionMatrix,
                    XMMatrixPerspectiveFovLH(m_fieldOfView, m_aspectRatio, m_nearPlane, m_farPlane));

    // No stereo swap chain: the per-eye projections are the mono projection (the
    // interface slot is kept for a future stereo device).
    m_projectionMatrixLeft = m_projectionMatrix;
    m_projectionMatrixRight = m_projectionMatrix;
  }

  void Camera::LookDirection(XMFLOAT3 lookDirection)
  {
    XMFLOAT3 lookAt;
    lookAt.x = m_eye.x + lookDirection.x;
    lookAt.y = m_eye.y + lookDirection.y;
    lookAt.z = m_eye.z + lookDirection.z;
    SetViewParams(m_eye, lookAt, m_up);
  }

  void Camera::Eye(XMFLOAT3 position)
  {
    SetViewParams(position, m_lookAt, m_up);
  }

  XMMATRIX Camera::View() { return XMLoadFloat4x4(&m_viewMatrix); }
  XMMATRIX Camera::Projection() { return XMLoadFloat4x4(&m_projectionMatrix); }
  XMMATRIX Camera::LeftEyeProjection() { return XMLoadFloat4x4(&m_projectionMatrixLeft); }
  XMMATRIX Camera::RightEyeProjection() { return XMLoadFloat4x4(&m_projectionMatrixRight); }
  XMMATRIX Camera::World() { return XMLoadFloat4x4(&m_inverseView); }
  XMFLOAT3 Camera::Eye() { return m_eye; }
  XMFLOAT3 Camera::LookAt() { return m_lookAt; }
  XMFLOAT3 Camera::Up() { return m_up; }
  float Camera::NearClipPlane() { return m_nearPlane; }
  float Camera::FarClipPlane() { return m_farPlane; }
  float Camera::Pitch() { return m_cameraPitchAngle; }
  float Camera::Yaw() { return m_cameraYawAngle; }

  Camera& MainCamera()
  {
    static Camera s_camera;
    return s_camera;
  }

  float CameraTanHalfFovX(Camera& _cam)
  {
    XMFLOAT4X4 p;
    XMStoreFloat4x4(&p, _cam.Projection());
    return (p._11 != 0.0f) ? 1.0f / p._11 : 0.5f;
  }

  float CameraTanHalfFovY(Camera& _cam)
  {
    XMFLOAT4X4 p;
    XMStoreFloat4x4(&p, _cam.Projection());
    return (p._22 != 0.0f) ? 1.0f / p._22 : 0.375f;
  }

  float CameraFocalPixels(Camera& _cam, float _viewportH)
  {
    const float tanY = CameraTanHalfFovY(_cam);
    return (tanY > 0.0f) ? (_viewportH * 0.5f) / tanY : _viewportH;
  }

  bool CameraSpaceToPixels(Camera& _cam, double _x, double _y, double _z,
                           int _viewportW, int _viewportH, double& _outSx, double& _outSy)
  {
    if (_z <= 0.0)
      return false;

    const double f = CameraFocalPixels(_cam, static_cast<float>(_viewportH));
    _outSx = (_x * f) / _z + _viewportW * 0.5;
    _outSy = -(_y * f) / _z + _viewportH * 0.5;
    return true;
  }
}
