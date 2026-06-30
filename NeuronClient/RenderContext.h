#pragma once

// RenderContext - temporary client-side render-seam bridge.
//
// The render-seam migration (A1) moved the ported game code off direct gfx_*
// calls and onto a RenderQueue. While the per-frame render state is still global,
// the queue is reached through this single active-queue accessor. The migrated
// draw section records into it, then calls FlushRenderQueue() at the same point
// the drawing used to happen - so replay reproduces the original gfx_* sequence
// exactly (behaviour-preserving).
//
// NOTE: these are a *client* render concern, not headless Universe state - a
// RenderQueue + sink must NOT move into the (server-simulated, headless)
// Neuron::Universe. They go away when a client per-frame render context owns the
// queue + sink and is passed explicitly: that is part of the frame/session
// ownership work in the GameLogic split (A4), not A2 (which was the ECS
// de-globalisation, already done).

#include "RenderQueue.h"

// The active queue the (still-global) game render code records into.
Neuron::Render::RenderQueue& ActiveRenderQueue();

// Replay the active queue into the gfx (Direct3D 11) backend, then clear it.
// Call this where the recorded draws used to be issued, to preserve order.
void FlushRenderQueue();
