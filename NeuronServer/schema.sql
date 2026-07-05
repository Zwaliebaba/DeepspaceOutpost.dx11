-- DeepspaceOutpost persistence schema v2 (Microsoft SQL Server).
--
-- Shipped with the server; apply once against the `dso` database before starting
-- with DSO_DB pointed at it. Idempotent: every object is guarded with IF NOT
-- EXISTS, so re-running is safe and a schema bump appends a new guarded block
-- rather than editing an existing one. The server refuses to start against a
-- schema_version newer than it knows (world_meta below).
--
-- Design invariants (ARCHITECTURE.md §12): durable state only - never per-tick
-- positions. `empires` exists from day one so the Track C identity layer adds
-- columns/rows, not a rekeying migration.
--
-- v2 (galaxy locations): the universe is no longer regenerated from a seed alone
-- when the server starts. `systems` holds every planet/station LOCATION as a
-- durable row (loaded at boot, seeded once by tools/dbseed), so system ids are
-- stable across server upgrades and generator changes, and `station_markets` can
-- key against them. The seed is retained only as provenance (world_meta
-- 'galaxy_seed'). Planet positions are STATIC, so persisting them honors §12's
-- "never per-tick positions" rule (that rule is about moving entities, not the
-- fixed layout of the world).

IF NOT EXISTS (SELECT * FROM sys.tables WHERE name = 'accounts')
CREATE TABLE dbo.accounts (
  account_id      INT IDENTITY PRIMARY KEY,
  commander_name  NVARCHAR(20) NOT NULL UNIQUE,   -- the sanitized ClientHello name
  auth_token_hash BINARY(32) NULL,                -- reserved: real auth later; NULL = name-claim
  created_utc     DATETIME2 NOT NULL,
  last_seen_utc   DATETIME2 NOT NULL
);

IF NOT EXISTS (SELECT * FROM sys.tables WHERE name = 'empires')
CREATE TABLE dbo.empires (             -- Track C lands into this; v1: one per account
  empire_id  INT IDENTITY PRIMARY KEY,
  account_id INT NOT NULL REFERENCES dbo.accounts(account_id)
);

IF NOT EXISTS (SELECT * FROM sys.tables WHERE name = 'players')
CREATE TABLE dbo.players (             -- one avatar row today; N owned units later
  player_id      INT IDENTITY PRIMARY KEY,
  empire_id      INT NOT NULL REFERENCES dbo.empires(empire_id),
  credits        INT NOT NULL,         -- tenths of a credit (Wallet.credits)
  fuel_tenths    SMALLINT NOT NULL,    -- Fuel.tenths (max stays code-owned)
  wanted_level   SMALLINT NOT NULL,    -- Wanted.level
  score          INT NOT NULL,         -- session score (per-player record, C2)
  hold_capacity  SMALLINT NOT NULL,    -- CargoHold.capacity
  missiles       SMALLINT NOT NULL,    -- Equipment.missiles
  equip_flags    INT NOT NULL,         -- PersistEquipFlags bitmask (PlayerPersistState.h)
  last_system_id INT NOT NULL,         -- system to wake docked at (-1 = home)
  in_witchspace  BIT NOT NULL,
  updated_tick   BIGINT NOT NULL,      -- world tick of the snapshot
  updated_utc    DATETIME2 NOT NULL    -- stamped by the persistence thread
);

IF NOT EXISTS (SELECT * FROM sys.tables WHERE name = 'player_cargo')
CREATE TABLE dbo.player_cargo (        -- non-zero stacks only
  player_id INT NOT NULL REFERENCES dbo.players(player_id),
  commodity TINYINT NOT NULL,          -- 0..16
  units     SMALLINT NOT NULL,
  PRIMARY KEY (player_id, commodity)
);

IF NOT EXISTS (SELECT * FROM sys.tables WHERE name = 'station_markets')
CREATE TABLE dbo.station_markets (     -- authoritative once seeded; drift persisted on a slow cadence
  system_id    INT NOT NULL,
  commodity    TINYINT NOT NULL,
  stock        SMALLINT NOT NULL,
  price        SMALLINT NOT NULL,      -- legacy x4 fixed-point, as in MarketEntry
  updated_tick BIGINT NOT NULL,
  PRIMARY KEY (system_id, commodity)
);

IF NOT EXISTS (SELECT * FROM sys.tables WHERE name = 'world_meta')
CREATE TABLE dbo.world_meta (          -- 'schema_version', 'galaxy_seed', 'world_tick'
  meta_key   NVARCHAR(32) PRIMARY KEY,
  meta_value NVARCHAR(128) NOT NULL
);

IF NOT EXISTS (SELECT * FROM sys.tables WHERE name = 'command_log')
CREATE TABLE dbo.command_log (         -- audit/replay; order = log_id
  log_id     BIGINT IDENTITY PRIMARY KEY,
  world_tick BIGINT NOT NULL,
  player_id  INT NOT NULL,
  message_id INT NOT NULL,             -- the catalog MessageId
  payload    VARBINARY(512) NOT NULL,  -- the message's generic-codec encoding
  logged_utc DATETIME2 NOT NULL
);

-- Seed the schema version (the server checks this and refuses a newer schema).
IF NOT EXISTS (SELECT * FROM dbo.world_meta WHERE meta_key = 'schema_version')
INSERT INTO dbo.world_meta (meta_key, meta_value) VALUES ('schema_version', '1');

-- ===========================================================================
-- v2: durable galaxy layout.
--
-- `systems` is the initial-loading mechanism for the universe: one row per
-- planet/station, positioned in the absolute int64 world. The server loads these
-- at boot instead of regenerating from a seed, so the layout is stable, editable,
-- and referable. tools/dbseed populates it once (it can regenerate from the seed
-- to fill an empty table, or the rows can be authored/edited directly).
-- system_id is NOT an identity column: it is the stable galaxy id (the procedural
-- index, or -1 for the hand-placed home system), assigned by the seeder.
IF NOT EXISTS (SELECT * FROM sys.tables WHERE name = 'systems')
CREATE TABLE dbo.systems (
  system_id    INT PRIMARY KEY,       -- stable galaxy id (-1 = home); assigned, not IDENTITY
  name         NVARCHAR(32) NOT NULL,
  planet_x     BIGINT NOT NULL,       -- absolute int64 world position of the planet
  planet_y     BIGINT NOT NULL,
  planet_z     BIGINT NOT NULL,
  station_x    BIGINT NOT NULL,       -- absolute int64 world position of the station
  station_y    BIGINT NOT NULL,
  station_z    BIGINT NOT NULL,
  economy      TINYINT NOT NULL,      -- 0..7 (drives the market baseline)
  government   TINYINT NOT NULL,      -- 0..7
  tech_level   TINYINT NOT NULL,
  population   INT NOT NULL,
  productivity INT NOT NULL,
  radius       INT NOT NULL,
  market_seed  INT NOT NULL           -- per-system seed fed to GenerateMarket()
);

-- Tie every market row to a real system now that one exists. Guarded on the
-- constraint name so re-running is safe; both tables are created earlier in this
-- same (GO-less) batch, so the reference resolves.
IF NOT EXISTS (SELECT * FROM sys.foreign_keys WHERE name = 'FK_station_markets_systems')
ALTER TABLE dbo.station_markets
  ADD CONSTRAINT FK_station_markets_systems
  FOREIGN KEY (system_id) REFERENCES dbo.systems(system_id);

-- Bump the version now that v2's objects exist (append-only: never edit the v1
-- insert above). Idempotent - only advances a v1 row.
IF EXISTS (SELECT * FROM dbo.world_meta WHERE meta_key = 'schema_version' AND meta_value = '1')
UPDATE dbo.world_meta SET meta_value = '2' WHERE meta_key = 'schema_version';
