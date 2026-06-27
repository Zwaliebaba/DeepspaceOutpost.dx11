#include "GameLogic.h"

// The systems are header-inline for now; this translation unit anchors the
// static library and is where heavier ported systems (AI/combat/economy) will
// be implemented as they move out of the legacy client.

namespace Neuron::GameLogic
{
  constexpr uint32_t ENGINE_VERSION = 0x00000001;

  uint32_t Version()
  {
    return ENGINE_VERSION;
  }
}
