// dbseed - seed the DeepspaceOutpost galaxy into SQL Server (v2 schema).
//
// The universe's LOCATIONS are loaded from dbo.systems at server start rather
// than regenerated from a seed alone (see NeuronServer/schema.sql, ARCHITECTURE.md
// §6.10). This tool is the initial-loading mechanism: it generates the galaxy from
// the fixed GalaxyConfig seed and writes one durable row per system plus each
// station's baseline market, so ids/positions are stable and the commodity market
// has a persistent home. Re-running is idempotent (every write is a MERGE), so it
// is safe to run against an existing DB to (re)establish the baseline; it does NOT
// overwrite drifted market rows unless the row is missing.
//
// Windows/ODBC only (it reuses the server's OdbcStore), built under
// DSO_ENABLE_ODBC. Apply schema.sql first, then run this once against the fresh DB.
//
//   dbseed "Driver={ODBC Driver 17 for SQL Server};Server=.;Database=dso;Trusted_Connection=yes;"
//
// With no argument it reads the connection string from the DSO_DB environment
// variable (the same one the server uses).

#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

#include "GalaxyRows.h"        // BuildSystemRows / BaselineMarketRows (Server-side, header-only)
#include "SceneRows.h"         // BuildPoiRows / BaselinePoiResourceRows (scene.md, header-only)
#include "OdbcStore.h"         // MakeOdbcStore
#include "PersistenceStore.h"  // IPersistenceStore / SystemRow / MarketRow / PoiRow

using namespace Neuron;

int main(int _argc, char** _argv)
{
  std::string conn;
  if (_argc > 1)
    conn = _argv[1];
  else if (const char* env = std::getenv("DSO_DB"); env != nullptr)
    conn = env;

  if (conn.empty())
  {
    std::fprintf(stderr,
        "usage: dbseed <odbc-connection-string>\n"
        "   or: set DSO_DB and run dbseed with no argument\n");
    return 2;
  }

  std::unique_ptr<Persist::IPersistenceStore> store = DSOServer::MakeOdbcStore(conn);
  if (!store)
  {
    std::fprintf(stderr, "dbseed: ODBC connect failed (apply schema.sql first, check the connection string)\n");
    return 1;
  }

  // Generate the canonical galaxy from the fixed seed: every system row plus the
  // hand-placed home system (id -1).
  constexpr GameLogic::GalaxyConfig cfg{};
  const std::vector<Persist::SystemRow> systems = DSOServer::BuildSystemRows(cfg);

  // Baseline markets for every system (updated_tick = 0 = "as seeded").
  std::vector<Persist::MarketRow> markets;
  markets.reserve(systems.size() * GameLogic::COMMODITY_COUNT);
  for (const Persist::SystemRow& s : systems)
  {
    const std::vector<Persist::MarketRow> rows = DSOServer::BaselineMarketRows(s, /*tick*/ 0);
    markets.insert(markets.end(), rows.begin(), rows.end());
  }

  store->UpsertSystems(systems);
  store->UpsertMarketRows(markets);

  // Scene POIs (scene.md): match each system to its template, place the recipe
  // deterministically, and write the anchor rows + baseline belt pools. Same
  // discipline as the market baseline (updated_tick = 0 = "as seeded"). The store's
  // MERGE leaves hand-edited (authored=1) rows untouched.
  const std::vector<Persist::PoiRow> pois = DSOServer::BuildPoiRows(cfg);
  const std::vector<Persist::PoiResourceRow> poiResources = DSOServer::BaselinePoiResourceRows(pois, /*tick*/ 0);
  store->UpsertPois(pois);
  store->UpsertPoiResources(poiResources);

  // Provenance: record the seed the galaxy was generated from, and initialize the
  // world tick if this is a fresh DB (never clobber an advancing counter).
  {
    char seedHex[32] = {};
    std::snprintf(seedHex, sizeof(seedHex), "0x%llX", static_cast<unsigned long long>(cfg.seed));
    store->WriteMeta("galaxy_seed", seedHex);
    if (!store->ReadMeta("world_tick").has_value())
      store->WriteMeta("world_tick", "0");
  }

  std::printf("dbseed: wrote %zu systems, %zu market rows, %zu scene POIs, %zu belt pools (seed 0x%llX).\n",
              systems.size(), markets.size(), pois.size(), poiResources.size(),
              static_cast<unsigned long long>(cfg.seed));
  return 0;
}
