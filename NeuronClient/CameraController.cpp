#include "pch.h"

#include "CameraController.h"

#include <algorithm>
#include <cmath>

using namespace DirectX;

namespace Neuron::Client
{
  namespace
  {
    // Feel constants. World scale for reference: ships fly ~3000 units/s, a
    // station is ~1000 units across, the AOI spans a few hundred thousand.
    constexpr float kLookSensitivity = 0.0035f; // radians per pixel of mouse drag
    constexpr float kPitchLimit = 1.55f;        // just short of straight up/down
    constexpr double kMoveSpeed = 2500.0;       // units per second
    constexpr double kBoostFactor = 8.0;        // Shift multiplier
    constexpr double kWheelDolly = 400.0;       // units per wheel notch (first person)
    constexpr double kOrbitKeyRate = 1.2;       // radians per second on the key axes
    constexpr double kOrbitWheelFactor = 0.85;  // distance multiplier per wheel notch
    constexpr double kOrbitMinDistance = 150.0;
    constexpr double kOrbitMaxDistance = 60000.0;

    float ClampPitch(float _pitch)
    {
      return std::max(-kPitchLimit, std::min(kPitchLimit, _pitch));
    }

    // Unit look vector for a yaw/pitch pair in the engine's left-handed frame
    // (x right, y up, z forward): yaw 0 / pitch 0 looks down +z.
    void YawPitchToLook(float _yaw, float _pitch, float _out[3])
    {
      const float cp = cosf(_pitch);
      _out[0] = cp * sinf(_yaw);
      _out[1] = sinf(_pitch);
      _out[2] = cp * cosf(_yaw);
    }
  }

  // ---- FirstPersonCameraController --------------------------------------------

  void FirstPersonCameraController::LookVector(float _out[3]) const
  {
    YawPitchToLook(m_yaw, m_pitch, _out);
  }

  void FirstPersonCameraController::LookTowards(const double _targetWorld[3])
  {
    const double dx = _targetWorld[0] - m_eyeWorld[0];
    const double dy = _targetWorld[1] - m_eyeWorld[1];
    const double dz = _targetWorld[2] - m_eyeWorld[2];
    const double flat = std::sqrt(dx * dx + dz * dz);
    if (flat < 1e-6 && std::fabs(dy) < 1e-6)
      return;
    m_yaw = static_cast<float>(std::atan2(dx, dz));
    m_pitch = ClampPitch(static_cast<float>(std::atan2(dy, flat)));
  }

  void FirstPersonCameraController::Update(const CameraInput& _input)
  {
    if (_input.looking)
    {
      m_yaw += _input.lookDX * kLookSensitivity;
      m_pitch = ClampPitch(m_pitch - _input.lookDY * kLookSensitivity);
    }

    float look[3];
    LookVector(look);

    // Strafe axis: look x worldUp, flattened (fly like an editor camera).
    const float right[3] = {cosf(m_yaw), 0.0f, -sinf(m_yaw)};

    const double step = kMoveSpeed * (_input.boost ? kBoostFactor : 1.0) * static_cast<double>(_input.dt);
    const double dolly = static_cast<double>(_input.wheelSteps) * kWheelDolly;

    for (int i = 0; i < 3; ++i)
    {
      m_eyeWorld[i] += look[i] * (static_cast<double>(_input.moveForward) * step + dolly);
      m_eyeWorld[i] += right[i] * (static_cast<double>(_input.moveRight) * step);
    }
    m_eyeWorld[1] += static_cast<double>(_input.moveUp) * step; // world-up vertical
  }

  void FirstPersonCameraController::ApplyView(Camera& _camera, const double _originWorld[3]) const
  {
    const XMFLOAT3 eye(static_cast<float>(m_eyeWorld[0] - _originWorld[0]),
                       static_cast<float>(m_eyeWorld[1] - _originWorld[1]),
                       static_cast<float>(m_eyeWorld[2] - _originWorld[2]));

    float look[3];
    LookVector(look);
    const XMFLOAT3 lookAt(eye.x + look[0], eye.y + look[1], eye.z + look[2]);

    _camera.SetViewParams(eye, lookAt, XMFLOAT3(0.0f, 1.0f, 0.0f));
  }

  // ---- OrbitCameraController ---------------------------------------------------

  void OrbitCameraController::SetTarget(const double _targetWorld[3])
  {
    m_targetWorld[0] = _targetWorld[0];
    m_targetWorld[1] = _targetWorld[1];
    m_targetWorld[2] = _targetWorld[2];
    RecomputeEye();
  }

  void OrbitCameraController::SetOrbit(float _yaw, float _pitch, double _distance)
  {
    m_yaw = _yaw;
    m_pitch = ClampPitch(_pitch);
    m_distance = std::max(kOrbitMinDistance, std::min(kOrbitMaxDistance, _distance));
    RecomputeEye();
  }

  void OrbitCameraController::RecomputeEye()
  {
    // The eye sits at target - lookDir * distance, where lookDir points from the
    // eye toward the target.
    float look[3];
    YawPitchToLook(m_yaw, m_pitch, look);
    for (int i = 0; i < 3; ++i)
      m_eyeWorld[i] = m_targetWorld[i] - static_cast<double>(look[i]) * m_distance;
  }

  void OrbitCameraController::Update(const CameraInput& _input)
  {
    if (_input.looking)
    {
      m_yaw += _input.lookDX * kLookSensitivity;
      m_pitch = ClampPitch(m_pitch + _input.lookDY * kLookSensitivity);
    }

    // Key axes orbit too (left/right yaw, up/down pitch); the wheel dollies the
    // orbit distance multiplicatively so it feels the same at any scale.
    m_yaw += static_cast<float>(static_cast<double>(_input.moveRight) * kOrbitKeyRate * _input.dt);
    m_pitch = ClampPitch(m_pitch +
                         static_cast<float>(static_cast<double>(_input.moveUp) * kOrbitKeyRate * _input.dt));

    double dist = m_distance;
    if (_input.wheelSteps != 0.0f)
      dist *= std::pow(kOrbitWheelFactor, static_cast<double>(_input.wheelSteps));
    // Forward/back keys dolly as well (exponential, frame-rate independent).
    if (_input.moveForward != 0.0f)
      dist *= std::exp(-static_cast<double>(_input.moveForward) * _input.dt);
    m_distance = std::max(kOrbitMinDistance, std::min(kOrbitMaxDistance, dist));

    RecomputeEye();
  }

  void OrbitCameraController::ApplyView(Camera& _camera, const double _originWorld[3]) const
  {
    const XMFLOAT3 eye(static_cast<float>(m_eyeWorld[0] - _originWorld[0]),
                       static_cast<float>(m_eyeWorld[1] - _originWorld[1]),
                       static_cast<float>(m_eyeWorld[2] - _originWorld[2]));
    const XMFLOAT3 lookAt(static_cast<float>(m_targetWorld[0] - _originWorld[0]),
                          static_cast<float>(m_targetWorld[1] - _originWorld[1]),
                          static_cast<float>(m_targetWorld[2] - _originWorld[2]));

    _camera.SetViewParams(eye, lookAt, XMFLOAT3(0.0f, 1.0f, 0.0f));
  }
}
