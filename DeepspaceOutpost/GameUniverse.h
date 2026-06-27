#pragma once

// GameUniverse - the live game-world instance (A2 flip).
//
// While the de-globalisation is in progress the world is reached through this
// single accessor (mirroring the RenderContext bridge). As clusters of legacy
// globals (myship, the flight state, local_objects[]) move onto it, the game
// reads/writes entities here instead. The instance becomes owned by the frame /
// session (and, on the server, the authoritative loop) once the migration and
// the GameLogic split (A4) are done.

#include "Universe.h"

Neuron::Universe& GameUniverse();
