// WorldBuilder - bootstrap the authoritative world (Server). See WorldBuilder.h.

#include "WorldBuilder.h"

#include <cstdio>

#include "GameLogic.h"

using namespace Neuron;

namespace DSOServer
{
  WorldSetup BuildWorld(ECS::Registry& _world)
  {
    WorldSetup setup;

    // The home system's celestial bodies, ahead of the spawn so a launching
    // player sees them (typed so the client draws the planet/station models,
    // not a ship).
    const ECS::EntityId planet = _world.Create();
    _world.Add<GameLogic::WorldTransform>(planet, GameLogic::WorldTransform{ { 0, 0, 65536 } });
    _world.Add<GameLogic::NetType>(planet, GameLogic::NetType{ GameLogic::ShipType::Planet });
    setup.landmarks.push_back(planet);

    // One pirate ahead-right, out toward the planet. Placed well beyond its own
    // engage range from the spawn (and station behind), so a fresh launch isn't
    // sniped/farmed at the spawn point; you meet it as an opt-in fight on the
    // way to the planet. Its range is shortened too, so it only opens fire at
    // close quarters rather than from afar.
    const ECS::EntityId pirate = _world.Create();
    _world.Add<GameLogic::WorldTransform>(pirate, GameLogic::WorldTransform{ { 1500, 400, 14000 } });
    _world.Add<GameLogic::Flight>(pirate, GameLogic::Flight{});
    _world.Add<GameLogic::Combatant>(pirate, GameLogic::Combatant{ GameLogic::Team::Pirate, /*energy*/ 80, /*laser*/ 3, /*range*/ 3000, /*autoEngage*/ true });
    _world.Add<GameLogic::NetType>(pirate, GameLogic::NetType{ GameLogic::ShipType::Viper });
    _world.Add<GameLogic::Bounty>(pirate, GameLogic::Bounty{ GameLogic::PIRATE_BOUNTY });   // killing it pays out
    // G5: it flies by intent like the dynamic spawns - hunts, breaks off, flees.
    _world.Add<GameLogic::FlightIntent>(pirate, GameLogic::FlightIntent{});
    _world.Add<GameLogic::FlightCaps>(pirate, GameLogic::NpcFlightCaps());
    _world.Add<GameLogic::AiPilot>(pirate, GameLogic::AiPilot{ /*bravery*/ 96, /*missiles*/ 2, /*maxEnergy*/ 80 });

    // Home system station, BEHIND the spawn (negative z) so a launching player
    // faces the planet with the station at their back (classic Elite launch);
    // within docking range of spawn, and carrying its own market.
    const ECS::EntityId station = _world.Create();
    _world.Add<GameLogic::WorldTransform>(station, GameLogic::WorldTransform{ { 0, 0, -3000 } });
    _world.Add<GameLogic::NetType>(station, GameLogic::NetType{ GameLogic::ShipType::Coriolis });
    setup.landmarks.push_back(station);
    // The station is a (near-indestructible) combat target so firing on it is a
    // detectable crime; it never initiates fire (autoEngage = false).
    _world.Add<GameLogic::Combatant>(station, GameLogic::Combatant{ GameLogic::Team::Station, 1000000, 0, 1, false });
    {
      const GameLogic::PlanetData home = GameLogic::GeneratePlanet(GameLogic::BASE_GALAXY_SEED);
      GameLogic::ServerStation ss;
      ss.systemId = -1;   // the hand-placed home system
      GameLogic::GenerateMarket(home.economy, GameLogic::BASE_GALAXY_SEED.f, ss.market);
      _world.Add<GameLogic::ServerStation>(station, ss);
    }

    // The procedural galaxy: every system's planet + station, scattered far
    // across the int64 field (reachable by teleport, or a long flight). AOI
    // keeps them off the wire until a player is near one. Each station carries
    // its own market.
    constexpr GameLogic::GalaxyConfig GALAXY_CFG{};
    const std::vector<GameLogic::GalaxySystem> systems = GameLogic::GenerateGalaxy(GALAXY_CFG);

    for (const GameLogic::GalaxySystem& sys : systems)
    {
      const ECS::EntityId pl = _world.Create();
      _world.Add<GameLogic::WorldTransform>(pl, GameLogic::WorldTransform{ sys.planetPos });
      _world.Add<GameLogic::NetType>(pl, GameLogic::NetType{ GameLogic::ShipType::Planet });
      setup.landmarks.push_back(pl);

      const ECS::EntityId stn = _world.Create();
      _world.Add<GameLogic::WorldTransform>(stn, GameLogic::WorldTransform{ sys.stationPos });
      _world.Add<GameLogic::NetType>(stn, GameLogic::NetType{ GameLogic::ShipType::Coriolis });
      _world.Add<GameLogic::Combatant>(stn, GameLogic::Combatant{ GameLogic::Team::Station, 1000000, 0, 1, false });
      setup.landmarks.push_back(stn);
      GameLogic::ServerStation ss;
      ss.systemId = static_cast<int>(sys.id);
      GameLogic::GenerateMarket(sys.planet.economy, sys.marketSeed, ss.market);
      _world.Add<GameLogic::ServerStation>(stn, ss);
    }
    printf("Galaxy: %d systems generated.\n", GALAXY_CFG.planetCount);

    // The chart manifest shipped to every client on connect: the procedural
    // systems plus the hand-placed home system (id -1) so players can always
    // teleport back.
    setup.manifest = GameLogic::BuildManifest(systems);
    {
      const GameLogic::PlanetData home = GameLogic::GeneratePlanet(GameLogic::BASE_GALAXY_SEED);
      Net::GalaxySystemInfo h;
      h.id = static_cast<uint32_t>(-1);   // matches the home station's systemId (-1)
      h.x = 0; h.y = 0; h.z = 65536;      // the home planet

      const char* HOME_NAME = "HOME";
      for (std::size_t i = 0; HOME_NAME[i] != '\0' && i < Net::GALAXY_NAME_MAX - 1; ++i)
        h.name[i] = HOME_NAME[i];
      h.government = static_cast<uint8_t>(home.government);
      h.economy = static_cast<uint8_t>(home.economy);
      h.techLevel = static_cast<uint8_t>(home.techLevel);
      h.population = static_cast<uint16_t>(home.population);
      h.productivity = static_cast<uint16_t>(home.productivity);
      setup.manifest.push_back(h);
    }

    return setup;
  }
}
