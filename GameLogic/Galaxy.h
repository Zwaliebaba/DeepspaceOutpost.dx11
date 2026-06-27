#pragma once

// Galaxy - the deterministic, seed-driven universe generator (GameLogic, A4).
//
// A faithful port of the legacy planet.cpp generation (waggle_galaxy,
// generate_planet_data, name_planet) and space.cpp's galaxy twist
// (rotate_byte_left / enter_next_galaxy). It is the iconic Elite "Tribbles in a
// 6-byte seed" algorithm: every system's name, economy, government, tech level,
// population, productivity and radius fall out of one 48-bit seed.
//
// In the MMO the *server* owns the canonical galaxy: it is generated once from a
// fixed seed and is identical for every client, so this code is pure and free of
// global state. GeneratePlanet()/NamePlanet() take a seed and return a value;
// nothing here mutates hidden globals (the legacy carry_flag side effect of
// waggle_galaxy is dropped - it never fed back into generation, only the legacy
// hyperspace dust effect).

#include <cstdint>
#include <string>

namespace Neuron::GameLogic
{
  // The 6-byte (48-bit) system seed. Twisting it (Waggle) walks the 256 systems
  // of a galaxy; rotating it (NextGalaxy) jumps to the next of the 8 galaxies.
  struct GalaxySeed
  {
    uint8_t a = 0;
    uint8_t b = 0;
    uint8_t c = 0;
    uint8_t d = 0;
    uint8_t e = 0;
    uint8_t f = 0;

    [[nodiscard]] friend bool operator==(const GalaxySeed&, const GalaxySeed&) = default;
  };

  // The canonical Elite galaxy 1 seed (legacy cmdr.galaxy default). System 0 of
  // this seed is "Tibedied".
  inline constexpr GalaxySeed BASE_GALAXY_SEED{ 0x4a, 0x5a, 0x48, 0x02, 0x53, 0xb7 };

  // Derived per-system facts (legacy struct planet_data).
  struct PlanetData
  {
    int government = 0;     // 0..7
    int economy = 0;        // 0..7 (0 = rich industrial .. 7 = poor agricultural)
    int techLevel = 0;
    int population = 0;     // in hundreds of millions (legacy units)
    int productivity = 0;
    int radius = 0;
  };

  // Advance a seed one "twist" - the Fibonacci-style 16-bit add-with-carry over
  // (a,c)/(b,d) then shift-in of (e,f). Walks to the next system in the galaxy.
  inline void Waggle(GalaxySeed& _seed)
  {
    unsigned int x = _seed.a + _seed.c;
    unsigned int y = _seed.b + _seed.d;

    if (x > 0xFF)
      ++y;

    x &= 0xFF;
    y &= 0xFF;

    _seed.a = _seed.c;
    _seed.b = _seed.d;
    _seed.c = _seed.e;
    _seed.d = _seed.f;

    x += _seed.c;
    y += _seed.d;

    if (x > 0xFF)
      ++y;

    // (legacy set carry_flag here when y > 0xFF; not needed for generation)

    x &= 0xFF;
    y &= 0xFF;

    _seed.e = static_cast<uint8_t>(x);
    _seed.f = static_cast<uint8_t>(y);
  }

  // Rotate a byte left by one bit (legacy rotate_byte_left).
  [[nodiscard]] inline uint8_t RotateByteLeft(uint8_t _x)
  {
    return static_cast<uint8_t>(((_x << 1) | (_x >> 7)) & 0xFF);
  }

  // Jump from one galaxy's seed to the next (legacy enter_next_galaxy). There are
  // 8 galaxies; applying this 8 times returns to the start.
  [[nodiscard]] inline GalaxySeed NextGalaxy(GalaxySeed _seed)
  {
    return GalaxySeed{
      RotateByteLeft(_seed.a),
      RotateByteLeft(_seed.b),
      RotateByteLeft(_seed.c),
      RotateByteLeft(_seed.d),
      RotateByteLeft(_seed.e),
      RotateByteLeft(_seed.f),
    };
  }

  // Derive a system's economy/government/tech/etc. from its seed. Pure port of
  // generate_planet_data().
  [[nodiscard]] inline PlanetData GeneratePlanet(const GalaxySeed& _seed)
  {
    PlanetData pl;

    pl.government = (_seed.c / 8) & 7;

    pl.economy = _seed.b & 7;
    if (pl.government < 2)
      pl.economy = pl.economy | 2;

    pl.techLevel = pl.economy ^ 7;
    pl.techLevel += _seed.d & 3;
    pl.techLevel += (pl.government / 2) + (pl.government & 1);

    pl.population = pl.techLevel * 4;
    pl.population += pl.government;
    pl.population += pl.economy;
    pl.population++;

    pl.productivity = (pl.economy ^ 7) + 3;
    pl.productivity *= pl.government + 4;
    pl.productivity *= pl.population;
    pl.productivity *= 8;

    pl.radius = (((_seed.f & 15) + 11) * 256) + _seed.d;

    return pl;
  }

  // The 86-letter digram table that the name generator walks two characters at a
  // time (legacy `digrams`). '?' marks a single-letter (vowel) digram.
  inline constexpr char DIGRAMS[] =
    "ABOUSEITILETSTONLONUTHNOALLEXEGEZACEBISOUSESARMAINDIREA?ERATENBERALAVETIEDORQUANTEISRION";

  // Produce a system's name from its seed (pure port of name_planet). The result
  // is upper-case, as the legacy generator emits it; presentation lower-cases the
  // tail. A seed names itself by twisting an internal copy 3 or 4 times.
  [[nodiscard]] inline std::string NamePlanet(GalaxySeed _seed)
  {
    const int size = ((_seed.a & 0x40) == 0) ? 3 : 4;

    std::string name;
    for (int i = 0; i < size; ++i)
    {
      unsigned int x = _seed.f & 0x1F;
      if (x != 0)
      {
        x += 12;
        x *= 2;
        name.push_back(DIGRAMS[x]);
        if (DIGRAMS[x + 1] != '?')
          name.push_back(DIGRAMS[x + 1]);
      }

      Waggle(_seed);
    }

    return name;
  }

  // Return the seed of system `_index` (0..255) within a galaxy: the galaxy seed
  // twisted four times per system (legacy systems are spaced 4 waggles apart).
  [[nodiscard]] inline GalaxySeed SystemSeed(GalaxySeed _galaxy, int _index)
  {
    for (int i = 0; i < _index; ++i)
    {
      Waggle(_galaxy);
      Waggle(_galaxy);
      Waggle(_galaxy);
      Waggle(_galaxy);
    }
    return _galaxy;
  }
}
