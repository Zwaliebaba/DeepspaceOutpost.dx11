#include "TestFramework.h"

#include <vector>

#include "GalaxyGen.h"

using namespace Neuron;

TEST(GalaxyGen_ProducesTheRequestedCount)
{
  GameLogic::GalaxyConfig cfg;
  cfg.planetCount = 50;
  std::vector<GameLogic::GalaxySystem> g = GameLogic::GenerateGalaxy(cfg);
  CHECK(g.size() == 50);
  CHECK(g[0].id == 0);
  CHECK(g[49].id == 49);
}

TEST(GalaxyGen_IsDeterministic)
{
  GameLogic::GalaxyConfig cfg;
  cfg.seed = 12345;
  cfg.planetCount = 32;

  std::vector<GameLogic::GalaxySystem> a = GameLogic::GenerateGalaxy(cfg);
  std::vector<GameLogic::GalaxySystem> b = GameLogic::GenerateGalaxy(cfg);

  CHECK(a.size() == b.size());
  for (std::size_t i = 0; i < a.size(); ++i)
  {
    CHECK((a[i].planetPos == b[i].planetPos));
    CHECK((a[i].stationPos == b[i].stationPos));
    CHECK((a[i].seed == b[i].seed));
    CHECK(a[i].name == b[i].name);
    CHECK(a[i].planet.economy == b[i].planet.economy);
  }
}

TEST(GalaxyGen_DifferentSeedsGiveDifferentGalaxies)
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
  CHECK(anyDifferent);
}

TEST(GalaxyGen_PlanetsStayWithinTheExtent)
{
  GameLogic::GalaxyConfig cfg;
  cfg.extent = 1'000'000;
  cfg.planetCount = 200;
  std::vector<GameLogic::GalaxySystem> g = GameLogic::GenerateGalaxy(cfg);

  for (const GameLogic::GalaxySystem& s : g)
  {
    CHECK(s.planetPos.x >= -cfg.extent && s.planetPos.x <= cfg.extent);
    CHECK(s.planetPos.y >= -cfg.extent && s.planetPos.y <= cfg.extent);
    CHECK(s.planetPos.z >= -cfg.extent && s.planetPos.z <= cfg.extent);
  }
}

TEST(GalaxyGen_StationSitsBesideItsPlanet)
{
  GameLogic::GalaxyConfig cfg;
  cfg.stationOrbit = 8000;
  cfg.planetCount = 10;
  std::vector<GameLogic::GalaxySystem> g = GameLogic::GenerateGalaxy(cfg);

  for (const GameLogic::GalaxySystem& s : g)
  {
    CHECK((s.stationPos == Math::Vector3i64{ s.planetPos.x + 8000, s.planetPos.y, s.planetPos.z }));
  }
}

TEST(GalaxyGen_PlanetsHaveSaneAttributes)
{
  GameLogic::GalaxyConfig cfg;
  cfg.planetCount = 100;
  std::vector<GameLogic::GalaxySystem> g = GameLogic::GenerateGalaxy(cfg);

  for (const GameLogic::GalaxySystem& s : g)
  {
    CHECK(s.planet.economy >= 0 && s.planet.economy <= 7);
    CHECK(s.planet.government >= 0 && s.planet.government <= 7);
  }
}

TEST(GalaxyGen_PlanetsAreSpreadOutNotStacked)
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
  CHECK(collisions == 0);
}
