#include "pch.h"

#include "RenderContext.h"

#include "GfxRenderSink.h"

// One process-wide active queue and the gfx backend sink it replays into.
// (Temporary A1 globals - removed when the queue moves into the Universe in A2.)
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
