#include "TestFramework.h"

#include "Galaxy.h"

using namespace Neuron::GameLogic;

TEST(Galaxy_FirstSystemIsTibedied)
{
  // System 0 of the canonical galaxy-1 seed is the iconic "Tibedied".
  CHECK(NamePlanet(BASE_GALAXY_SEED) == "TIBEDIED");
}

TEST(Galaxy_GeneratePlanetMatchesLegacyMath)
{
  // Hand-computed from generate_planet_data() with the base seed
  // {0x4a,0x5a,0x48,0x02,0x53,0xb7}:
  //   government = (0x48/8)&7         = 1
  //   economy    = 0x5a&7 = 2, gov<2 -> |2 = 2
  //   techLevel  = (2^7) + (2&3) + 1  = 5 + 2 + 1 = 8
  //   population = 8*4 + 1 + 2 + 1    = 36
  //   productivity = (5+3)*(1+4)*36*8 = 8*5*36*8 = 11520
  //   radius     = ((0xb7&15)+11)*256 + 2 = 18*256 + 2 = 4610
  PlanetData pl = GeneratePlanet(BASE_GALAXY_SEED);
  CHECK(pl.government == 1);
  CHECK(pl.economy == 2);
  CHECK(pl.techLevel == 8);
  CHECK(pl.population == 36);
  CHECK(pl.productivity == 11520);
  CHECK(pl.radius == 4610);
}

TEST(Galaxy_WaggleIsDeterministic)
{
  GalaxySeed a = BASE_GALAXY_SEED;
  GalaxySeed b = BASE_GALAXY_SEED;
  for (int i = 0; i < 17; ++i)
  {
    Waggle(a);
    Waggle(b);
  }
  CHECK(a == b);
}

TEST(Galaxy_SystemSeedSpacesByFourWaggles)
{
  // SystemSeed(.,1) must equal four manual twists of the galaxy seed.
  GalaxySeed manual = BASE_GALAXY_SEED;
  Waggle(manual);
  Waggle(manual);
  Waggle(manual);
  Waggle(manual);
  CHECK(SystemSeed(BASE_GALAXY_SEED, 1) == manual);
  CHECK(SystemSeed(BASE_GALAXY_SEED, 0) == BASE_GALAXY_SEED);
}

TEST(Galaxy_NextGalaxyRotatesEachByte)
{
  // rotate_byte_left on each byte of {0x4a,0x5a,0x48,0x02,0x53,0xb7}.
  GalaxySeed g2 = NextGalaxy(BASE_GALAXY_SEED);
  CHECK(g2.a == 0x94);
  CHECK(g2.b == 0xb4);
  CHECK(g2.c == 0x90);
  CHECK(g2.d == 0x04);
  CHECK(g2.e == 0xa6);
  CHECK(g2.f == 0x6f);
}

TEST(Galaxy_EightGalaxiesWrapAround)
{
  // Eight single-bit rotations of a byte return it unchanged, so cycling all
  // eight galaxies returns to the starting seed (legacy galaxy_number & 7).
  GalaxySeed g = BASE_GALAXY_SEED;
  for (int i = 0; i < 8; ++i)
    g = NextGalaxy(g);
  CHECK(g == BASE_GALAXY_SEED);
}
