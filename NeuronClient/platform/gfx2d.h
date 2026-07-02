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

#include "ModelDraw.h" // Neuron::Render::ModelDraw (kept for transitive consumers)

// Replay this frame's 2D batch to the back buffer, and (once) the 3D scene pass under it.
// The game hands its 3D models straight to Scene3D (Scene3D::SubmitModel), not through here.
//
// Every screen redraws every frame now (flight HUD, charts, docked legacy screens, the 3D
// scene pass), so the batch is never empty during normal play and this always clears +
// draws + is present-ready. The one screen that draws nothing - a paused game - is handled
// by the caller (GameApp::RenderCanvas) simply not presenting, so the last frame stays on
// screen (FLIP_DISCARD keeps no retained content). There is no idle-frame gate here.
void gfx2d_flush(void);

#endif /* GFX2D_H */
