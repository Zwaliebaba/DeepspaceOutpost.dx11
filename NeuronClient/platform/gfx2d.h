/*
 * DeepspaceOutpost - DirectX 11 / XAudio2.
 *
 * gfx2d.h
 *
 * The 2D primitive batch this header used to drive is gone - all 2D drawing is native now
 * (Neuron::Graphics::Render2D via RenderGameHud / the GUI overlay), so there is no
 * gfx2d_flush() any more. The scene/viewport seam that remains in gfx2d.cpp is declared in
 * gfx.h. This header now only re-exports ModelDraw for its transitive consumers.
 */

#ifndef GFX2D_H
#define GFX2D_H

#include "ModelDraw.h" // Neuron::Render::ModelDraw (kept for transitive consumers)

#endif /* GFX2D_H */
