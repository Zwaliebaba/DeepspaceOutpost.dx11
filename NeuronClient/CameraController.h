#pragma once

// CameraController - drives the free Camera from player input.
//
// The camera is decoupled from the ship: the player flies the CAMERA, not the
// hull (the ship is server-simulated and just renders like any other entity).
// Two controller types exist, toggled by the game:
//
//   FirstPersonCameraController - the default: mouse-look (hold the right mouse
//     button) plus key axes to fly the eye freely; the wheel dollies along the
//     look direction.
//
//   OrbitCameraController - rotates the camera around a selected object (the
//     game feeds the target each frame: the missile-locked entity, or the
//     player's own ship); drag rotates, the wheel changes the orbit distance.
//
// Controllers keep the ABSOLUTE eye (and orbit target) in doubles: the world is
// an unbounded int64 space and float would degrade past ~16M units, while a
// double carries it exactly. Each frame the game rounds the eye to the int64
// floating origin, rebases the world around it, and calls ApplyView() with that
// origin so the Camera only ever sees small origin-relative floats.

#include <DirectXMath.h>

#include "Camera.h"

namespace Neuron::Client
{
  // One frame of camera input, gathered by the game (mouse deltas while the
  // look button is held, wheel notches, -1..1 key axes, and the frame dt).
  struct CameraInput
  {
    float lookDX = 0.0f;      // mouse drag delta, pixels (valid while `looking`)
    float lookDY = 0.0f;
    bool looking = false;     // look/drag button held (RMB)
    float wheelSteps = 0.0f;  // wheel notches this frame (+ = wheel up)
    float moveForward = 0.0f; // -1..1 key axes
    float moveRight = 0.0f;
    float moveUp = 0.0f;
    bool boost = false;       // speed modifier held (Shift)
    float dt = 1.0f / 60.0f;  // seconds since the previous update
  };

  class CameraController
  {
  public:
    virtual ~CameraController() = default;

    // Advance the rig from this frame's input (mutates the absolute eye/angles).
    virtual void Update(const CameraInput& _input) = 0;

    // Write the rig's pose into the Camera, relative to the floating origin the
    // frame's world is rebased around (usually llround of the eye itself).
    virtual void ApplyView(Camera& _camera, const double _originWorld[3]) const = 0;

    [[nodiscard]] const double* EyeWorld() const { return m_eyeWorld; }
    void SetEyeWorld(double _x, double _y, double _z)
    {
      m_eyeWorld[0] = _x;
      m_eyeWorld[1] = _y;
      m_eyeWorld[2] = _z;
    }

  protected:
    double m_eyeWorld[3] = {0.0, 0.0, 0.0};
  };

  // The default first-person free camera: yaw/pitch mouse-look, key axes to fly.
  class FirstPersonCameraController : public CameraController
  {
  public:
    void Update(const CameraInput& _input) override;
    void ApplyView(Camera& _camera, const double _originWorld[3]) const override;

    // Point the camera at an absolute world position (used to (re)anchor the rig
    // behind the ship on spawn / hyperspace).
    void LookTowards(const double _targetWorld[3]);

    [[nodiscard]] float YawAngle() const { return m_yaw; }
    [[nodiscard]] float PitchAngle() const { return m_pitch; }

  private:
    void LookVector(float _out[3]) const;

    float m_yaw = 0.0f;   // radians about +y; 0 looks down +z
    float m_pitch = 0.0f; // radians; + looks up
  };

  // Rotate the camera around a selected object; the game feeds the target's
  // absolute world position every frame (it may be a moving ship).
  class OrbitCameraController : public CameraController
  {
  public:
    void Update(const CameraInput& _input) override;
    void ApplyView(Camera& _camera, const double _originWorld[3]) const override;

    void SetTarget(const double _targetWorld[3]);
    void SetOrbit(float _yaw, float _pitch, double _distance);

    [[nodiscard]] float YawAngle() const { return m_yaw; }
    [[nodiscard]] float PitchAngle() const { return m_pitch; }
    [[nodiscard]] double Distance() const { return m_distance; }
    [[nodiscard]] const double* TargetWorld() const { return m_targetWorld; }

  private:
    void RecomputeEye();

    double m_targetWorld[3] = {0.0, 0.0, 0.0};
    float m_yaw = 3.14159265f; // start on the target's -z side
    float m_pitch = -0.35f;    // look direction tilts down -> the eye sits above
    double m_distance = 900.0;
  };
}
