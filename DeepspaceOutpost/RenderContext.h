#pragma once

// RenderContext - temporary A1 bridge.
//
// The render-seam migration moves the ported game code off direct gfx_* calls
// and onto a RenderQueue. While the game state is still global (A1, before the
// A2 de-globalisation into Universe), the queue is reached through this single
// active-queue accessor. The migrated draw section records into it, then calls
// FlushRenderQueue() at the same point the drawing used to happen - so replay
// reproduces the original gfx_* sequence exactly (behaviour-preserving).
//
// In A2 this global goes away: the per-frame RenderQueue becomes owned by the
// frame/Universe and passed explicitly.

#include "RenderQueue.h"

// The active queue the (still-global) game render code records into.
Neuron::Render::RenderQueue& ActiveRenderQueue();

// Replay the active queue into the gfx (Direct3D 11) backend, then clear it.
// Call this where the recorded draws used to be issued, to preserve order.
void FlushRenderQueue();
