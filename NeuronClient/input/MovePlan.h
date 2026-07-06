#pragma once

// MovePlan - the pure, CI-testable state machine behind the Homeworld-style
// two-step movement grid (input.md §0.1, §4.2). It classifies the transitions
// of the tactical move gizmo so they are pinned by the headless suite rather
// than living only in the DX11 command glue (main.cpp's HandleMovementGrid).
//
// The two steps of a 3D move command:
//   1. PlacingXZ - the cursor ray meets the HORIZONTAL plane through the unit;
//      the hit is the X/Z destination coordinate.
//   2. AdjustingY - holding the vertical modifier (Shift on mouse, a second
//      finger on touch) slides the marker along +Y (the altitude/height).
// Confirm sends UnitOrder{Move}; Cancel drops the gizmo. Releasing the modifier
// returns to PlacingXZ with the elevation kept.
//
// It carries NO geometry, NO input, NO DirectXMath - the glue does the ray/
// plane math (MoveGizmo.h) and feeds device events in; this header owns only
// the mode transitions. OrderMenu-style: one source of truth, one switch.

#include <cstdint>

namespace Neuron::Input
{
  // The movement grid's mode (the spec's `movement_plane_active` is any non-Off
  // state; `vertical_modifier_active` is AdjustingY).
  enum class GridMode : uint8_t
  {
    Off,        // no grid
    PlacingXZ,  // placing the X/Z destination on the horizontal plane
    AdjustingY, // sliding the marker up/down along +Y (vertical modifier held)
  };

  // A device event the grid reacts to (device-neutral: mouse or touch feed it).
  enum class GridEvent : uint8_t
  {
    Activate,   // M pressed / RMB-on-empty / touch long-press on empty space
    ShiftDown,  // the vertical modifier engaged (Shift / second finger)
    ShiftUp,    // the vertical modifier released
    Confirm,    // LMB click (M-mode) / drag release (fast path) / finger lift
    Cancel,     // M again / Esc / RMB / cancel chip
  };

  // What the glue should do after a transition, alongside moving to `next`.
  struct GridTransition
  {
    GridMode next    = GridMode::Off;
    bool     confirm = false; // send UnitOrder{Move} at the current marker
    bool     cancel  = false; // clear the gizmo render state
  };

  // The one transition table. `_hasOwnUnit` gates activation (you can only order
  // a unit you own); it is ignored for every other event. Unhandled (mode,event)
  // pairs are no-ops that stay in the current mode.
  constexpr GridTransition StepGrid(GridMode _cur, GridEvent _ev, bool _hasOwnUnit)
  {
    switch (_cur)
    {
    case GridMode::Off:
      if (_ev == GridEvent::Activate && _hasOwnUnit)
        return {GridMode::PlacingXZ, false, false};
      return {GridMode::Off, false, false};

    case GridMode::PlacingXZ:
      switch (_ev)
      {
      case GridEvent::ShiftDown: return {GridMode::AdjustingY, false, false};
      case GridEvent::Confirm:   return {GridMode::Off, true,  false};
      case GridEvent::Cancel:    return {GridMode::Off, false, true};
      default:                   return {GridMode::PlacingXZ, false, false};
      }

    case GridMode::AdjustingY:
      switch (_ev)
      {
      case GridEvent::ShiftUp:  return {GridMode::PlacingXZ, false, false}; // elevation kept
      case GridEvent::Confirm:  return {GridMode::Off, true,  false};
      case GridEvent::Cancel:   return {GridMode::Off, false, true};
      default:                  return {GridMode::AdjustingY, false, false};
      }
    }
    return {GridMode::Off, false, false};
  }

  // Convenience: is the grid currently claiming pointer input (any non-Off mode)?
  constexpr bool GridActive(GridMode _m) { return _m != GridMode::Off; }
}
