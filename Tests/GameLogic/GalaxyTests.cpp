#include <gtest/gtest.h>

#include "Galaxy.h"

using namespace Neuron::GameLogic;

TEST(Galaxy, FirstSystemIsTibedied)
{
  // System 0 of the canonical galaxy-1 seed is the iconic "Tibedied".
  EXPECT_TRUE(NamePlanet(BASE_GALAXY_SEED) == "TIBEDIED");
}

TEST(Galaxy, GeneratePlanetMatchesLegacyMath)
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
  EXPECT_TRUE(pl.government == 1);
  EXPECT_TRUE(pl.economy == 2);
  EXPECT_TRUE(pl.techLevel == 8);
  EXPECT_TRUE(pl.population == 36);
  EXPECT_TRUE(pl.productivity == 11520);
  EXPECT_TRUE(pl.radius == 4610);
}

TEST(Galaxy, WaggleIsDeterministic)
{
  GalaxySeed a = BASE_GALAXY_SEED;
  GalaxySeed b = BASE_GALAXY_SEED;
  for (int i = 0; i < 17; ++i)
  {
    Waggle(a);
    Waggle(b);
  }
  EXPECT_TRUE(a == b);
}

