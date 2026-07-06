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
    constexpr float LOOK_SENSITIVITY = 0.0035f; // radians per pixel of mouse drag
    constexpr float PITCH_LIMIT = 1.55f;        // just short of straight up/down
    constexpr double MOVE_SPEED = 2500.0;       // units per second
    constexpr double BOOST_FACTOR = 8.0;        // Shift multiplier
    constexpr double WHEEL_DOLLY = 400.0;       // units per wheel notch (first person)
    constexpr double ORBIT_KEY_RATE = 1.2;       // radians per second on the key axes
    constexpr double ORBIT_WHEEL_FACTOR = 0.85;  // distance multiplier per wheel notch
    constexpr double ORBIT_MIN_DISTANCE = 150.0;
    constexpr double ORBIT_MAX_DISTANCE = 60000.0;
    constexpr float  FOCUS_ANIM_SECONDS = 0.5f;  // ease-out duration of the F-key focus
    constexpr double KEY_PAN_RATE = 1.5;         // focus-widths/sec pan on the key axes

    float ClampPitch(float _pitch)
    {
      return std::max(-PITCH_LIMIT, std::min(PITCH_LIMIT, _pitch));
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
      m_yaw += _input.lookDX * LOOK_SENSITIVITY;
      m_pitch = ClampPitch(m_pitch - _input.lookDY * LOOK_SENSITIVITY);
    }

    float look[3];
    LookVector(look);

    // Strafe axis: look x worldUp, flattened (fly like an editor camera).
    const float right[3] = {cosf(m_yaw), 0.0f, -sinf(m_yaw)};

    const double step = MOVE_SPEED * (_input.boost ? BOOST_FACTOR : 1.0) * static_cast<double>(_input.dt);
    const double dolly = static_cast<double>(_input.wheelSteps) * WHEEL_DOLLY;

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
    m_focusT = 1.0f;   // a snap cancels any in-flight focus ease
    RecomputeEye();
  }

  void OrbitCameraController::FocusOn(const double _targetWorld[3])
  {
    for (int i = 0; i < 3; ++i)
    {
      m_focusFrom[i] = m_targetWorld[i];
      m_focusTo[i] = _targetWorld[i];
    }
    m_focusT = 0.0f;   // Update() eases m_targetWorld From -> To
  }

  void OrbitCameraController::SetOrbit(float _yaw, float _pitch, double _distance)
  {
    m_yaw = _yaw;
    m_pitch = ClampPitch(_pitch);
    m_distance = std::max(ORBIT_MIN_DISTANCE, std::min(ORBIT_MAX_DISTANCE, _distance));
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
    // 1) Rotate: RMB-drag pitches/yaws the camera about the focus point.
    if (_input.looking)
    {
      m_yaw += _input.lookDX * LOOK_SENSITIVITY;
      m_pitch = ClampPitch(m_pitch + _input.lookDY * LOOK_SENSITIVITY);
    }

    // 2) Focus animation: ease the focus point onto its target (F / double-tap).
    if (m_focusT < 1.0f)
    {
      m_focusT = std::min(1.0f, m_focusT + static_cast<float>(_input.dt) / FOCUS_ANIM_SECONDS);
      const double ease = 1.0 - (1.0 - m_focusT) * (1.0 - m_focusT); // ease-out
      for (int i = 0; i < 3; ++i)
        m_targetWorld[i] = m_focusFrom[i] + (m_focusTo[i] - m_focusFrom[i]) * ease;
    }

    // 3) Pan: slide the focus point in the screen plane. World-units-per-pixel is
    //    derived from the orbit distance so panning feels the same at any zoom;
    //    the key axes (arrows/WASD) pan proportionally to the distance too.
    const double perPixel = (_input.viewportH > 1.0f)
        ? m_distance * static_cast<double>(_input.tanHalfFovY) * 2.0 / static_cast<double>(_input.viewportH)
        : 0.0;
    const double panRight = -static_cast<double>(_input.panDX) * perPixel
        + static_cast<double>(_input.keyPanRight) * KEY_PAN_RATE * m_distance * _input.dt;
    const double panUp = static_cast<double>(_input.panDY) * perPixel
        + static_cast<double>(_input.keyPanUp) * KEY_PAN_RATE * m_distance * _input.dt;
    if (panRight != 0.0 || panUp != 0.0)
    {
      float look[3];
      YawPitchToLook(m_yaw, m_pitch, look);
      const float right[3] = {cosf(m_yaw), 0.0f, -sinf(m_yaw)};   // flat screen-right
      const float up[3] = {look[1] * right[2] - look[2] * right[1], // up = look x right
                           look[2] * right[0] - look[0] * right[2],
                           look[0] * right[1] - look[1] * right[0]};
      for (int i = 0; i < 3; ++i)
        m_targetWorld[i] += static_cast<double>(right[i]) * panRight + static_cast<double>(up[i]) * panUp;
      m_focusT = 1.0f;   // a manual pan cancels an in-flight focus ease
    }

    // 4) Zoom toward the focus point: the wheel (and the forward/back keys) dolly
    //    the orbit distance multiplicatively so it feels the same at any scale.
    double dist = m_distance;
    if (_input.wheelSteps != 0.0f)
      dist *= std::pow(ORBIT_WHEEL_FACTOR, static_cast<double>(_input.wheelSteps));
    if (_input.moveForward != 0.0f)
      dist *= std::exp(-static_cast<double>(_input.moveForward) * _input.dt);
    m_distance = std::max(ORBIT_MIN_DISTANCE, std::min(ORBIT_MAX_DISTANCE, dist));

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
