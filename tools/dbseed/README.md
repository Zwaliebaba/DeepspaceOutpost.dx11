# dbseed

Seeds the DeepspaceOutpost galaxy into SQL Server — the **initial-loading
mechanism** for the universe. Since the v2 schema, the server loads planet and
station **locations** from `dbo.systems` at boot instead of regenerating them
from a seed alone, so system ids and positions are stable across upgrades and the
commodity market (`dbo.station_markets`) has a durable, referable home.

`dbseed` generates the canonical galaxy from the fixed `GalaxyConfig` seed and
writes:

- one `dbo.systems` row per system (all 256 procedural systems **plus** the
  hand-placed home system, id `-1`) — name, planet/station int64 positions,
  economy/government/tech/population/productivity/radius, and the market seed;
- the **baseline** `dbo.station_markets` rows for every system (stock/price from
  `GenerateMarket`);
- `world_meta` provenance (`galaxy_seed`, and `world_tick = 0` on a fresh DB).

Every write is a `MERGE`, so re-running is idempotent and safe against an existing
database. It re-establishes the systems/baseline; it will not clobber a market row
that already exists (drift the server has persisted stays), only fill in a missing
one.

## Prerequisites

1. A SQL Server database with the v2 schema applied
   (`NeuronServer/schema.sql`).
2. An ODBC driver (e.g. *ODBC Driver 17 for SQL Server*).

## Build

Windows/ODBC only — same gate as the server's persistence backend. Configure with
the backend on and build the `dbseed` target:

```
cmake -S . -B build -DDSO_ENABLE_ODBC=ON
cmake --build build --target dbseed --config Release
```

## Run

```
dbseed "Driver={ODBC Driver 17 for SQL Server};Server=.;Database=dso;Trusted_Connection=yes;"
```

Or set `DSO_DB` (the same connection string the server reads) and run with no
argument:

```
set DSO_DB=Driver={ODBC Driver 17 for SQL Server};Server=.;Database=dso;Trusted_Connection=yes;
dbseed
```

Then start the server with the same `DSO_DB`; it will log
`Galaxy: N systems loaded from the store.` If `dbo.systems` is empty the server
falls back to seed-generated locations and warns that market drift will not be
persisted — run `dbseed` to fix that.
