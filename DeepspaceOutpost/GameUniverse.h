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

// The player ship's capability component (replaces the legacy `myship` global).
// Valid once initialise_game has created the player entity.
inline Neuron::Game::ShipCaps& PlayerCaps()
{
  return GameUniverse().Reg().Get<Neuron::Game::ShipCaps>(GameUniverse().Player());
}

// The player ship's current flight rates (replaces flight_speed/roll/climb).
inline Neuron::Game::FlightRates& PlayerFlight()
{
  return GameUniverse().Reg().Get<Neuron::Game::FlightRates>(GameUniverse().Player());
}

// The player ship's defensive/power state (replaces front_shield/aft_shield/
// energy/laser_temp).
inline Neuron::Game::Defense& PlayerDefense()
{
  return GameUniverse().Reg().Get<Neuron::Game::Defense>(GameUniverse().Player());
}
