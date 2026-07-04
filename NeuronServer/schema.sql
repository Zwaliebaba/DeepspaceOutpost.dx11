-- DeepspaceOutpost persistence schema v1 (Microsoft SQL Server).
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
  score          INT NOT NULL,         -- PlayerRecord.score
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
CREATE TABLE dbo.station_markets (     -- lazily materialized; authoritative from F4
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
