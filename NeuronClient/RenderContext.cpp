#include "pch.h"

#include "RenderContext.h"

#include "GfxRenderSink.h"

// One process-wide active queue and the gfx backend sink it replays into.
// (Temporary client-side globals from the A1 render seam. A RenderQueue + sink are
// a client render concern, not headless Universe state; they go away when a client
// per-frame render context owns them and is passed explicitly - the frame/session
// ownership work in the GameLogic split (A4).)
namespace
{
  Neuron::Render::RenderQueue g_activeQueue;
  GfxRenderSink g_gfxSink;
}

Neuron::Render::RenderQueue& ActiveRenderQueue()
{
  return g_activeQueue;
}

void FlushRenderQueue()
{
  g_activeQueue.Replay(g_gfxSink);
  g_activeQueue.Clear();
}
