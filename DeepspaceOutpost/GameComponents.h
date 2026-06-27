#pragma once

// GameComponents - the ECS components the de-globalised game state becomes (A2).
//
// These deliberately mirror the fields of the legacy `struct local_object`
// (space.h) and `struct player_ship` (elite.h), grouped into a few "fat"
// components so the migration is a faithful field-for-field move first; they get
// split/refined only after behaviour is locked. They live here (the client
// project) for now and relocate into GameLogic when the sim becomes a library
// (A4).
//
// Plain data aggregates: PascalCase types, camelCase members, no behaviour.

#include "ECS.h"
#include "vector.h"

namespace Neuron::Game
{
  // local_object: type
  struct ShipType
  {
    int type = 0;
  };

  // local_object: location, rotmat, rotx, rotz, distance
  struct Transform
  {
    Vector location{};
    Matrix rotmat{};
    int rotX = 0;
    int rotZ = 0;
    int distance = 0;
  };

  // local_object: velocity, acceleration
  struct Motion
  {
    int velocity = 0;
    int acceleration = 0;
  };

  // local_object: energy, missiles, flags
  struct Combat
  {
    int energy = 0;
    int missiles = 0;
    int flags = 0;
  };

  // local_object: target, bravery (NPC behaviour)
  struct Ai
  {
    int target = 0;
    int bravery = 0;
  };

  // local_object: exp_delta, exp_seed
  struct Explosion
  {
    int delta = 0;
    int seed = 0;
  };

  // player_ship (myship): per-ship capability limits, on the player entity.
  struct ShipCaps
  {
    int maxSpeed = 0;
    int maxRoll = 0;
    int maxClimb = 0;
    int maxFuel = 0;
    int altitude = 0;
    int cabTemp = 0;
  };

  // The player's current flight rates (legacy globals flight_speed/roll/climb).
  // These are the player's flight intent each frame; the universe sim applies
  // roll/climb/speed to the player-relative coordinate frame.
  struct FlightRates
  {
    int speed = 0;
    int roll = 0;
    int climb = 0;
  };

  // Tag marking the player's own ship entity.
  struct PlayerTag {};
}
