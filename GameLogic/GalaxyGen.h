#pragma once

// GalaxyGen - a procedural galaxy scattered across the int64 world (GameLogic).
//
// In the seamless model there are no separate "systems" to load: a galaxy is
// simply N planets placed at hash-derived absolute int64 positions in the one
// continuous field, each with a static station beside it. Placement and every
// planet's attributes (economy/government/tech/name/market) are a pure function
// of one GalaxyConfig seed - reproducible, storage-free, identical on server and
// (via the manifest) client. The iconic per-planet attribute generation is reused
// by synthesizing a 48-bit GalaxySeed per planet and feeding the ported
// GeneratePlanet()/NamePlanet().

#include <cstdint>
#include <string>
#include <vector>

#include "Vector3i64.h"

#include "Galaxy.h"   // GalaxySeed, PlanetData, GeneratePlanet, NamePlanet

namespace Neuron::GameLogic
{
  struct GalaxyConfig
  {
    uint64_t seed = 0xC0FFEEULL;        // the whole galaxy derives from this
    int planetCount = 256;              // how many planets to scatter
    int64_t extent = 100'000'000;       // galaxy half-size on each axis (units)
    int64_t stationOrbit = 8000;        // station offset from its planet
  };

  struct GalaxySystem
  {
    uint32_t id = 0;
    GalaxySeed seed{};                  // synthesized; drives name/economy
    std::string name;
    PlanetData planet;
    Math::Vector3i64 planetPos{};
    Math::Vector3i64 stationPos{};
    int marketSeed = 0;
  };

  namespace Detail
  {
    // SplitMix64 finalizer - a fast, well-distributed integer hash.
    [[nodiscard]] inline uint64_t Mix64(uint64_t _x)
    {
      _x += 0x9E3779B97F4A7C15ull;
      _x = (_x ^ (_x >> 30)) * 0xBF58476D1CE4E5B9ull;
      _x = (_x ^ (_x >> 27)) * 0x94D049BB133111EBull;
      return _x ^ (_x >> 31);
    }

    // Map a hash uniformly into [-extent, extent].
    [[nodiscard]] inline int64_t Spread(uint64_t _h, int64_t _extent)
    {
      if (_extent <= 0)
        return 0;
      const uint64_t span = static_cast<uint64_t>(_extent) * 2ull + 1ull;
      return static_cast<int64_t>(_h % span) - _extent;
    }
  }

  // Generate one system deterministically from the config and its index.
  [[nodiscard]] inline GalaxySystem GenerateSystem(const GalaxyConfig& _cfg, uint32_t _index)
  {
    GalaxySystem s;
    s.id = _index;

    const uint64_t base = Detail::Mix64(_cfg.seed ^ Detail::Mix64(static_cast<uint64_t>(_index) + 1ull));

    s.planetPos = {
      Detail::Spread(Detail::Mix64(base + 1ull), _cfg.extent),
      Detail::Spread(Detail::Mix64(base + 2ull), _cfg.extent),
      Detail::Spread(Detail::Mix64(base + 3ull), _cfg.extent),
    };
    s.stationPos = { s.planetPos.x + _cfg.stationOrbit, s.planetPos.y, s.planetPos.z };

    // Synthesize a 48-bit GalaxySeed for the ported attribute/name generation.
    const uint64_t attr = Detail::Mix64(base + 7ull);
    s.seed = GalaxySeed{
      static_cast<uint8_t>(attr),
      static_cast<uint8_t>(attr >> 8),
      static_cast<uint8_t>(attr >> 16),
      static_cast<uint8_t>(attr >> 24),
      static_cast<uint8_t>(attr >> 32),
      static_cast<uint8_t>(attr >> 40),
    };
    s.planet = GeneratePlanet(s.seed);
    s.name = NamePlanet(s.seed);
    s.marketSeed = static_cast<int>((attr >> 48) & 0xFFFFull);

    return s;
  }

  // Generate the whole galaxy: `planetCount` systems, fully deterministic.
  [[nodiscard]] inline std::vector<GalaxySystem> GenerateGalaxy(const GalaxyConfig& _cfg)
  {
    std::vector<GalaxySystem> systems;
    if (_cfg.planetCount > 0)
      systems.reserve(static_cast<std::size_t>(_cfg.planetCount));
    for (int i = 0; i < _cfg.planetCount; ++i)
      systems.push_back(GenerateSystem(_cfg, static_cast<uint32_t>(i)));
    return systems;
  }
}
