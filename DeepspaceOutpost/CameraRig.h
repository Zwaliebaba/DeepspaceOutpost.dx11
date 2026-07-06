#pragma once

// CameraRig - the game's glue around the NeuronClient free camera.
//
// Owns the two CameraControllers (first-person and orbit), gathers their input
// (mouse-look on the held right button, wheel, the arrow/PgUp/PgDn key axes,
// Shift boost), anchors the rig to the replicated ship on spawn / hyperspace,
// and publishes the per-frame floating origin the render path rebases the world
// around. Also provides the world -> camera-space transforms the remaining
// CPU-projected legacy paths use (frustum cull, explosion debris, firing beams,
// the scanner/compass mirror).

struct local_object;
struct vector;

/* Advance the rig once per frame in the flight state: gather input, update the
 * active controller and write the camera's view. No-ops (identity camera) until
 * the local ship has replicated at least once. */
void camera_rig_update (void);

/* Reset to the identity camera (eye at the origin looking down +z). Used by the
 * intro and game-over scenes, whose local objects animate in camera space. */
void camera_rig_reset (void);

/* F12: toggle first-person <-> orbit (observer). Orbit is the default; FPV is the
 * screenshot/observer mode. */
void camera_rig_toggle_mode (void);

/* Animate the focus-orbit camera onto the current selection (else the own ship)
 * and follow it - the Homeworld F-key / double-tap focus (input.md H3). */
void camera_rig_focus (void);

/* 1 once the rig has anchored to the replicated ship (before that there is no
 * meaningful floating origin and the world is not rendered). */
int camera_rig_ready (void);

/* The frame's floating origin (the camera eye, rounded to whole world units).
 * Entity snapshots are rebased around this before any float math. int64[3]. */
const long long* camera_rig_origin (void);

/* Transform a world-frame (origin-relative) point / object into camera space
 * through the main Camera's view matrix. The object form transforms the
 * location and the three basis rows in place. */
void camera_view_point (struct vector *v);
void camera_view_object (struct local_object *obj);
