#include <gtest/gtest.h>

// NeuronServer is still a placeholder engine layer (see AGENTS.md): its public
// umbrella header pulls in NeuronServer.h -> NeuronCore.h. This smoke test gives
// the server engine a GoogleTest project to grow into and verifies the library
// links and its public header compiles in a consumer translation unit. Replace
// it with real coverage as server-only systems (sessions, AOI/replication,
// persistence) land here.

#include "NeuronServer.h"

TEST(NeuronServer, LinksAndHeaderCompiles)
{
  SUCCEED();
}
