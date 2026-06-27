#include "pch.h"

#include "GameUniverse.h"

// One process-wide world instance for now (temporary A2 global, like the
// RenderContext bridge - removed once the world is owned by the frame/session).
namespace
{
  Neuron::Universe g_universe;
}

Neuron::Universe& GameUniverse()
{
  return g_universe;
}
