#include <gtest/gtest.h>

#include <vector>

#include "GalaxyGen.h"

using namespace Neuron;

TEST(GalaxyGen, ProducesTheRequestedCount)
{
  GameLogic::GalaxyConfig cfg;
  cfg.planetCount = 50;
  std::vector<GameLogic::GalaxySystem> g = GameLogic::GenerateGalaxy(cfg);
  EXPECT_TRUE(g.size() == 50);
  EXPECT_TRUE(g[0].id == 0);
  EXPECT_TRUE(g[49].id == 49);
}

TEST(GalaxyGen, IsDeterministic)
{
  GameLogic::GalaxyConfig cfg;
  cfg.seed = 12345;
  cfg.planetCount = 32;

  std::vector<GameLogic::GalaxySystem> a = GameLogic::GenerateGalaxy(cfg);
  std::vector<GameLogic::GalaxySystem> b = GameLogic::GenerateGalaxy(cfg);

  EXPECT_TRUE(a.size() == b.size());
  for (std::size_t i = 0; i < a.size(); ++i)
  {
    EXPECT_TRUE((a[i].planetPos == b[i].planetPos));
    EXPECT_TRUE((a[i].stationPos == b[i].stationPos));
    EXPECT_TRUE((a[i].seed == b[i].seed));
    EXPECT_TRUE(a[i].name == b[i].name);
    EXPECT_TRUE(a[i].planet.economy == b[i].planet.economy);
  }
}

TEST(GalaxyGen, DifferentSeedsGiveDifferentGalaxies)
{
  GameLogic::GalaxyConfig c1; c1.seed = 1; c1.planetCount = 16;
  GameLogic::GalaxyConfig c2; c2.seed = 2; c2.planetCount = 16;

  std::vector<GameLogic::GalaxySystem> a = GameLogic::GenerateGalaxy(c1);
  std::vector<GameLogic::GalaxySystem> b = GameLogic::GenerateGalaxy(c2);

  // The two galaxies should not be laid out identically.
  bool anyDifferent = false;
  for (std::size_t i = 0; i < a.size(); ++i)
    if (a[i].planetPos != b[i].planetPos)
      anyDifferent = true;
  EXPECT_TRUE(anyDifferent);
}

TEST(GalaxyGen, PlanetsStayWithinTheExtent)
{
  GameLogic::GalaxyConfig cfg;
  cfg.extent = 1'000'000;
  cfg.planetCount = 200;
  std::vector<GameLogic::GalaxySystem> g = GameLogic::GenerateGalaxy(cfg);

  for (const GameLogic::GalaxySystem& s : g)
  {
    EXPECT_TRUE(s.planetPos.x >= -cfg.extent && s.planetPos.x <= cfg.extent);
    EXPECT_TRUE(s.planetPos.y >= -cfg.extent && s.planetPos.y <= cfg.extent);
    EXPECT_TRUE(s.planetPos.z >= -cfg.extent && s.planetPos.z <= cfg.extent);
  }
}

TEST(GalaxyGen, StationSitsBesideItsPlanet)
{
  GameLogic::GalaxyConfig cfg;
  cfg.stationOrbit = 8000;
  cfg.planetCount = 10;
  std::vector<GameLogic::GalaxySystem> g = GameLogic::GenerateGalaxy(cfg);

  for (const GameLogic::GalaxySystem& s : g)
  {
    EXPECT_TRUE((s.stationPos == Math::Vector3i64{ s.planetPos.x + 8000, s.planetPos.y, s.planetPos.z }));
  }
}

TEST(GalaxyGen, PlanetsHaveSaneAttributes)
{
  GameLogic::GalaxyConfig cfg;
  cfg.planetCount = 100;
  std::vector<GameLogic::GalaxySystem> g = GameLogic::GenerateGalaxy(cfg);

  for (const GameLogic::GalaxySystem& s : g)
  {
    EXPECT_TRUE(s.planet.economy >= 0 && s.planet.economy <= 7);
    EXPECT_TRUE(s.planet.government >= 0 && s.planet.government <= 7);
  }
}

TEST(GalaxyGen, PlanetsAreSpreadOutNotStacked)
{
  GameLogic::GalaxyConfig cfg;
  cfg.planetCount = 64;
  std::vector<GameLogic::GalaxySystem> g = GameLogic::GenerateGalaxy(cfg);

  // No two of the first several planets share a position.
  int collisions = 0;
  for (std::size_t i = 0; i < g.size(); ++i)
    for (std::size_t j = i + 1; j < g.size(); ++j)
      if (g[i].planetPos == g[j].planetPos)
        ++collisions;
  EXPECT_TRUE(collisions == 0);
}
