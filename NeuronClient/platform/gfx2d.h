/*
 * DeepspaceOutpost - DirectX 11 / XAudio2.
 *
 * gfx2d.h
 *
 * Hook for the platform layer to flush the accumulated 2D primitive batch to the
 * back buffer once per frame (called from gfx_update_screen, before Renderer::swap()).
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
// The menu/station screens only repaint on demand (not every frame), so on an idle
// frame the batch is empty. The swap chain uses FLIP_DISCARD, which keeps no retained
// content, so painting+presenting an empty batch would clear the screen to black.
// Instead, an empty batch is a no-op (returns false) and the previously presented
// frame stays on screen - the role the old off-screen canvas used to fill. Pass
// forcePresent=true to clear and present a frame even when the batch is empty (the GUI
// overlay needs a fresh frame to composite onto).
bool gfx2d_flush(bool forcePresent);

#endif /* GFX2D_H */
