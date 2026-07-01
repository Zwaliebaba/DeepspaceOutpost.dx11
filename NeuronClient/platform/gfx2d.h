/*
 * DeepspaceOutpost - DirectX 11 / XAudio2.
 *
 * gfx2d.h
 *
 * Hook for the platform layer to flush the accumulated 2D primitive batch to the
 * back buffer once per frame (called from gfx_update_screen, before Core::Present()).
 * The batch is replayed through Neuron::Graphics::Render2D.
 */

#ifndef GFX2D_H
#define GFX2D_H

#include "RenderQueue.h" // Neuron::Render::ModelDraw

// Collect a 3D model instance for this frame's GPU scene pass. Called by the render
// sink when it replays a DrawModel command; the models are rendered (depth-tested)
// through Scene3D inside gfx2d_flush, composited between the 2D scene background and
// the HUD - matching the legacy "ships over the planet, under the HUD" draw order.
void gfx2d_submit_model(const Neuron::Render::ModelDraw& _model);

// Replay this frame's 2D batch to the back buffer. Returns true if a frame was
// painted (caller should present it), false if there was nothing to draw and the
// back buffer was left untouched.
//
// Every screen redraws every frame now (flight HUD, charts, docked legacy screens, the 3D
// scene pass), so the batch is never empty during normal play and this always clears +
// draws + is present-ready. The one screen that draws nothing - a paused game - is handled
// by the caller (GameApp::RenderCanvas) simply not presenting, so the last frame stays on
// screen (FLIP_DISCARD keeps no retained content). There is no idle-frame gate here.
void gfx2d_flush(void);

#endif /* GFX2D_H */
