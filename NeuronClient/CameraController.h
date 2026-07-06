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
    float moveForward = 0.0f; // -1..1 key axes (first-person fly / orbit dolly)
    float moveRight = 0.0f;
    float moveUp = 0.0f;
    bool boost = false;       // speed modifier held (Shift)

    // Homeworld camera additions (orbit controller): pan drags the focus point in
    // the screen plane; the key-pan axes do the same from arrows/WASD. tanHalfFovY
    // + viewportH let the orbit controller scale pan to world units per pixel at
    // the focus depth (the rig fills them from the live projection/viewport).
    float panDX = 0.0f;       // pan drag delta, pixels (chord / two-finger / MMB)
    float panDY = 0.0f;
    bool panning = false;
    float keyPanRight = 0.0f; // -1..1 (arrows / WASD)
    float keyPanUp = 0.0f;
    float tanHalfFovY = 0.375f; // tan(fovY/2); default = the legacy scene fov
    float viewportH = 720.0f;   // viewport height, pixels

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

    // Snap the focus point to an absolute world position (spawn/teleport anchor,
    // or the frame-to-frame follow of a moving unit). Cancels any focus ease.
    void SetTarget(const double _targetWorld[3]);
    void SetOrbit(float _yaw, float _pitch, double _distance);

    // Begin an animated re-centre of the focus point onto an absolute world
    // position (the Homeworld F-key / double-tap focus). Update() eases it in.
    void FocusOn(const double _targetWorld[3]);

    // Settle any in-flight focus ease immediately (used by the rig's reset seam
    // so no animation survives a scene change).
    void CancelFocusAnim() { m_focusT = 1.0f; }

    [[nodiscard]] float YawAngle() const { return m_yaw; }
    [[nodiscard]] float PitchAngle() const { return m_pitch; }
    [[nodiscard]] double Distance() const { return m_distance; }
    [[nodiscard]] const double* TargetWorld() const { return m_targetWorld; }

  private:
    void RecomputeEye();

    double m_targetWorld[3] = {0.0, 0.0, 0.0}; // the live camera focus point
    float m_yaw = 3.14159265f; // start on the target's -z side
    float m_pitch = -0.35f;    // look direction tilts down -> the eye sits above
    double m_distance = 900.0;

    // Focus-point ease (F-key / double-tap): m_targetWorld lerps from ...From to
    // ...To as m_focusT climbs 0->1. Settled (==1) means no animation is running.
    double m_focusFrom[3] = {0.0, 0.0, 0.0};
    double m_focusTo[3] = {0.0, 0.0, 0.0};
    float m_focusT = 1.0f;
  };
}
