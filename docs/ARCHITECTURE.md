# DeepspaceOutpost — Architecture & Game Design

**Status:** as-built (Phase G complete: G0-G8; chat deferred) + architectural
review, 2026-07-03. This is the **single canonical design document** for the
game: the client/server architecture, the authoritative simulation, the
complete game rules, the network protocol with every message type, the locked
design decisions (§12), the standing **architectural review** (§13), and the
consolidated roadmap (§14).

The former companion documents (`MIGRATION_ROADMAP.md`, `gameplay.md`,
`ARCHITECTURE_REVIEW.md`) have been folded into §12–§14 and retired to prevent
duplication. Sections 1–11 describe code that exists and is tested; §13–§14
are critique and forward plan.

---

## 1. System overview

DeepspaceOutpost is a **server-authoritative multiplayer** remake of the classic
*Elite* gameplay loop: fly, trade, fight, scoop, run from the law, jump between
systems. One dedicated server owns the entire game simulation; every client is a
**thin presentation layer** that sends *intent* and renders *replicated state*.

The single load-bearing rule:

> **The server simulates; the client renders. The only things client and server
> share are data schemas — never behaviour.**

Concretely:

- A client cannot set its position, speed, credits, cargo, shields or fuel. It
  sends what it *wants* (normalized flight axes, fire buttons, station
  requests); the server clamps that intent to the ship's performance envelope
  and its rules, and the results come back as replicated snapshots and events.
- All gameplay code lives in a headless, OS-free static library (`GameLogic`)
  the client does not link. The client keeps only presentation: rendering,
  interpolation, HUD, audio, input capture.
- Every gameplay rule is unit-tested headlessly (no DX11, no sockets, no
  wall-clock) — 200+ GameLogic tests (see CI for the live count) + the
  NeuronCore/NeuronServer suites, CI-built on Windows/MSVC (x64 debug + release).

### Topology

```
                 UDP :40000
 ┌──────────┐   InputCommand (unreliable, 30 Hz)     ┌────────────────────┐
 │  Client   │ ───────────────────────────────────►  │      Server        │
 │ (DX11 UI, │   StationRequest / ClientHello /       │  ~30 Hz fixed tick │
 │  render,  │   acks (reliable lanes)                │  ECS world = truth │
 │  audio)   │ ◄───────────────────────────────────  │  sessions, AOI     │
 └──────────┘   WorldSnapshot (unreliable, AOI)       └────────────────────┘
                EntityDeath/Despawn, PlayerInfo/Status,
                CargoManifest, StationResponse,
                GalaxyManifest  (reliable lanes)
```

Clients connect implicitly: the first `InputCommand` datagram from a new UDP
endpoint spawns a player entity and provisions a session (see §5.2).

---

## 2. Project layout

| Directory | Role | Links against |
|---|---|---|
| `NeuronCore/` | Header-only shared **engine + protocol**: ECS, int64 math, message system, serialization, reliability, snapshot schema, station protocol, galaxy manifest. **Data and mechanism only — no game rules.** | — |
| `GameLogic/` | **Server-only** authoritative simulation: flight, combat, AI, economy, stations, loot, collisions, hyperspace, sessions, spawning, AOI. Headless. | NeuronCore |
| `Server/` | The dedicated host: UDP socket loop, fixed tick, session I/O, world bootstrap (home system + procedural galaxy). | GameLogic, NeuronCore |
| `NeuronClient/` | Client-side engine: DX11 device/render, replication client (socket + interpolation), sound, fonts. | NeuronCore |
| `DeepspaceOutpost/` | The game client: legacy-derived presentation (cockpit, charts, station screens), input → intent, HUD mirrors of replicated state. | NeuronClient, NeuronCore |
| `Tests/GameLogic/`, `Tests/NeuronCore/`, `Tests/NeuronClient/`, `Tests/NeuronServer/` | GoogleTest suites (headless). | respective libs |
| `GameData/Models/` | Ship meshes (JSON), converted from the legacy tables. | — |

Dependency direction is strictly downward: the client never includes
`GameLogic`, the server never includes render/UI code, and `NeuronCore` includes
nothing above it.

### Coding conventions that matter architecturally

- **DirectXMath** (`XMVECTOR`/`XMMATRIX`, `NeuronCore/GameMath.h`) is the
  standard math for client/render code; the legacy `LegacyVector*`/`Matrix33`
  wrappers are frozen (do not extend).
- The **authoritative simulation** uses `Neuron::Math::Vector3i64` (absolute
  world position) and `Vector3d` (orientation/velocity) — double precision by
  design, for cross-run determinism the golden tests rely on.
- **No wall-clock randomness anywhere in GameLogic.** Every random draw comes
  from a caller-owned, seeded LCG stream (`x = x*1664525 + 1013904223`); the
  server keeps separate streams for loot, AI and hyperspace so the sequences
  cannot perturb each other. Deterministic in, deterministic out.

---

## 3. Core engine concepts

### 3.1 ECS

`Neuron::ECS::Registry` (NeuronCore/ECS.h) — an in-house sparse-set ECS:

- `EntityId { index : u32, generation : u32 }`. Slots are recycled;
  generations catch stale handles (`IsValid`). `INVALID_INDEX = 0xFFFFFFFF`.
- Component pools are sparse-set (O(1) add/remove/get, dense iteration).
- API: `Create()`, `Destroy(e)`, `Add<T>(e, v)`, `TryGet<T>(e) → T*`,
  `Get<T>(e) → T&`, `Has<T>(e)`, `Remove<T>(e)`, `Each<T>(fn)`,
  `Each<A,B>(fn)` (iterates the A pool, probes B — put the rarer component
  first), `LiveEntity(index)` (resolves a bare wire index to a live handle),
  `AliveCount()`.
- **Wire entity ids are bare indices** (the snapshot stream and events carry
  `u32` indexes). Server code resolving a client-supplied index goes through
  `LiveEntity()` so a dead/recycled slot can't be forged into a target.

### 3.2 World space & floating origin

- The world is an **absolute `int64³` space**. Positions never wrap; systems
  are scattered across ±100M units. Distance gates use **Chebyshev** ranges
  (no multiplies on absolute coordinates → no overflow), computing true/squared
  distances only over small in-range deltas.
- The client renders **camera-relative** (floating origin): every frame it
  rebases replicated `int64` positions around the viewer before touching float
  math, so precision never degrades far from origin.

### 3.3 Fixed tick

The server advances the world on a fixed ~30 Hz tick (`Sleep(33)`); all rates
below are expressed in ticks. The client renders at display rate and
interpolates between snapshots.

---

## 4. Networking & protocol

### 4.1 Transport stack

Everything rides **UDP on port 40000**. Each datagram is routed by its leading
magic:

| Magic | Stream | Contents |
|---|---|---|
| `'NSNP'` | **Snapshot** (unreliable, server→client) | AOI world-state snapshots; superseded by the next one, never retransmitted. |
| `'NRLB'` | **Reliable lanes** (both directions) | `[magic][lane u8][token u64]` + one `ReliableChannel` packet (`'NEVT'` seq/ack framing inside). |
| `'NMSG'` | **Message packet** (unreliable lane) | `[magic][version][lane][token u64]` + the framed record stream — today this carries `InputCommand` client→server. |

**Reliable lanes** (`Msg::MessageEndpoint`): one `ReliableChannel` per lane —
`Control(0)`, `Gameplay(1)`, `Bulk(2)` — each with its own sequence/ack space,
so a large cold Bulk payload (the galaxy manifest) can never head-of-line-block
a gameplay death or the session handshake. Receive drains Control → Gameplay →
Bulk. Idle lanes are silent. `ReliableChannel` is TCP-like at message level
(ordered, deduplicated, resent until acked) but stays on UDP.

**Framing** (`Messages/Framing.h`): an `'NMSG'` packet is
`magic u32 | PROTOCOL_VERSION u16 (=2) | lane u8 | token u64` followed by zero or
more records, each `MessageId u16 | length u16 | payload`. The mandatory
per-record length bounds every decoder to exactly its own bytes — a malformed
message cannot run the reader into the next record.

**Session token** (B2): both client→server framings (`'NMSG'` and `'NRLB'`)
carry a `token u64` right after the lane byte. It is the session's identity: the
server keys sessions by token, not by source address, and drops any client
datagram whose token doesn't match a live session *before decoding it* (`OnInput`
/ `ServerSessions::OnReliable`, via `PeekReliableToken`). So a spoofed source
address is useless without the token, and a NAT rebind that changes the address
heals silently (the token re-binds the session to the new address). The token is
`0` (unauthenticated) on the opening `ClientHello` and on server→client packets
(the client trusts the server by address). Token-less datagrams are rate-limited
per endpoint so a spoofed-source flood can't provision unbounded sessions. Tokens
come from the OS CSPRNG (`Server/SecureRandom.h` → `BCryptGenRandom`), never a
gameplay RNG.

**MTU discipline:** all state datagrams are kept at or below
`SAFE_UDP_PAYLOAD = 1200` bytes. The snapshot packetizer splits a world
snapshot into datagrams holding only whole entities; the galaxy manifest ships
in chunks sized the same way.

### 4.2 The message system

Every protocol message is a **catalog message**: a plain struct declaring five
compile-time traits and describing its fields once:

```cpp
struct PlayerInfo {
  static constexpr MessageId    Id    = 0x0301;
  static constexpr MessageScope Scope = MessageScope::Wire;
  static constexpr MessageKind  Kind  = MessageKind::Event;
  static constexpr MessageLane  Lane  = MessageLane::Gameplay;
  static constexpr Direction    Dir   = Direction::ServerToClient;
  uint32_t entityId; std::string name; int32_t wantedLevel;
  auto Fields() { return std::tie(entityId, name, wantedLevel); }
};
REGISTER_MESSAGE(PlayerInfo);
```

- **Traits** (`MessageTraits.h`): `Scope` (LocalOnly / Wire / Control /
  DebugOnly / Tooling), `Kind` (Command = must be validated, Event = a fact),
  `Lane` (Control / Gameplay / Bulk / Unreliable), `Dir` (ClientToServer /
  ServerToClient / Both / None). Wrong combinations are compile errors (e.g. a
  LocalOnly message cannot be encoded for the wire).
- **Generic codec** (`Serialize.h`): `Encode`/`Decode` fold over `Fields()`,
  little-endian, bounds-checked. Leaf types: `u8/u16/u32/u64`, `i32/i64`,
  `float`, `double`, `bool` (1 byte), enums (underlying type), `std::string`
  (u16 length + UTF-8, capped at **4096**), `NetEntityId` (index+generation),
  `std::optional<T>` (present byte + value), `std::vector<T>` (u16 count +
  elements, capped at **4096**). Hostile lengths can never drive unbounded
  allocation; truncated buffers fail decode safely.
- **Registry / governance**: `REGISTER_MESSAGE` adds each type to a global
  catalog; CI tests enforce *no duplicate ids*, *wire scope ⇔ wire id half*,
  and *every wire message declares a real direction*.

### 4.3 Message id bands

Ids are permanent ABI: never reused, retired ids stay reserved. Bit 15 set ⇒
never on the wire.

| Band | Purpose |
|---|---|
| `0x0001–0x00FF` | core / session / control |
| `0x0100–0x01FF` | input |
| `0x0200–0x02FF` | replication control / lifecycle |
| `0x0300–0x03FF` | chat / social / player identity |
| `0x0400–0x04FF` | station / economy |
| `0x0F00–0x0FFF` | debug / tooling (wire-visible) |
| `0x1000–0x7FFF` | game-specific extensions |
| `0x8000–0xFFFE` | **LocalOnly / Tooling — never on the wire** |

Grandfathered: `EcmPulse` (`0x0202`) and `EscapePodUsed` (`0x0203`) are
gameplay events that predate this note and sit in the replication band; their
ids stay (permanent ABI). Future gameplay/combat/VFX events allocate from the
game-specific band (`0x1000+`).

### 4.4 Wire message catalog

#### Session & identity

**`AssignPlayer`** — `0x0001` · **RETIRED** (id reserved, permanent ABI).
Was the connect handshake reply ("you control entity N") back when a session was
spawned on first input. Since B1 the handshake reply is `HelloAck`, which folds
in the protocol-version echo and the (future) session token. The id stays retired
and is never re-issued.

**`ClientHello`** — `0x0002` · Control scope · Command · Control lane · C→S.
The opening handshake and, since B1, the **front door**: the server spawns nothing
until a valid, version-checked hello arrives (no more spawn-on-first-input). Carries
the protocol version + the commander name the player chose. The server sanitizes
(printable ASCII, ≤ 20 chars, trailing spaces trimmed) and de-duplicates (`-2`,
`-3`, …) before adopting it.

| Field | Type | Meaning |
|---|---|---|
| `protocolVersion` | u32 | client's `PROTOCOL_VERSION` |
| `commanderName` | string | requested display name (raw; server sanitizes) |

**`HelloAck`** — `0x0003` · Control scope · Event · Control lane · S→C.
The handshake was accepted: "you are player P, controlling entity N." Subsumes
and retires `AssignPlayer`, adding the protocol-version echo, the session token,
and (C) the player identity. Sent once, on the first valid `ClientHello`. The
client stamps the token on every subsequent datagram (B2); the server
authenticates by it. *(Layout extended in place for C under the pre-launch
no-back-compat rule, with a `PROTOCOL_VERSION` bump to 3; post-launch layout
changes take a successor id per §4.3.)*

| Field | Type | Meaning |
|---|---|---|
| `sessionToken` | u64 | the session's identity — a CSPRNG token stamped on every later client datagram |
| `playerId` | u32 | the player identity (C: §12 "Account → Empire → owns N entities"); stable across reconnects |
| `entityId` | u32 | the PRIMARY entity this player controls |
| `protocolVersion` | u16 | the server's `PROTOCOL_VERSION` (echo) |

**`HelloReject`** — `0x0004` · Control scope · Event · Control lane · S→C.
The handshake was refused and no session was provisioned. Today the only reason
is a protocol-version mismatch; the client surfaces a connect error instead of a
world.

| Field | Type | Meaning |
|---|---|---|
| `reason` | u8 | a `HelloRejectReason` (`ProtocolMismatch = 1`) |

**`PlayerInfo`** — `0x0301` · Wire · Event · Gameplay · S→C.
One roster entry, broadcast to everyone on join, name change, or wanted-level
change (crime, decay, death, hyperspace cooling). Since C the roster is keyed by
**player**, not hull — required the moment one player owns two ships. *(Layout
extended in place; see the `HelloAck` note.)*

| Field | Type | Meaning |
|---|---|---|
| `playerId` | u32 | the roster's identity key (C) |
| `entityId` | u32 | the player's PRIMARY ship (nameplate anchor) |
| `name` | string | display name (sanitized, unique) |
| `wantedLevel` | i32 | legal status (0 = clean) |

**`PlayerStatus`** — `0x0302` · Wire · Event · Gameplay · S→C (owner only).
The owning player's private vitals for the HUD. Sent **on change only** (the
server caches the last sent copy per session).

| Field | Type | Meaning |
|---|---|---|
| `energy` | i32 | energy bank (0–255) |
| `frontShield` | i32 | front shield (0–255) |
| `aftShield` | i32 | aft shield (0–255) |
| `fuel` | i32 | hyperspace fuel, tenths of a LY (0–70) |
| `credits` | i32 | wallet, tenths of a credit |
| `missiles` | i32 | missiles in the rack |
| `cargoUsed` | i32 | hold tonnage used |
| `wantedLevel` | i32 | own legal status |
| `score` | i32 | kill count |
| `laserTemp` | i32 | laser temperature (G8); >= 242 locks the trigger |

**`CargoManifest`** — `0x0303` · Wire · Event · Gameplay · S→C (owner only).
The full per-commodity hold, resent whenever it changes outside a trade (a
scoop, or a respawn emptying it) — `PlayerStatus.cargoUsed` can't convey the
breakdown.

| Field | Type | Meaning |
|---|---|---|
| `units` | vector\<i32\> | held units per commodity, index 0..16 |

**`Chat`** — `0x0300` · Wire · Event · Gameplay · Both. *(Registered; UI not
yet wired — see §14.)*

| Field | Type | Meaning |
|---|---|---|
| `sender` | u32 | sending entity index |
| `text` | string | UTF-8 line |

#### Input

**`InputCommand`** — `0x0100` · Wire · Command ·
**Unreliable** lane · C→S. The per-frame flight/fire intent. Self-superseding:
the server keeps the highest `sequence` and drops stale datagrams. A static
trait forbids queuing it on a reliable lane.

| Field | Type | Meaning |
|---|---|---|
| `sequence` | u32 | monotonic; latest wins |
| `rollAxis` | f32 | [-1, 1] desired roll (right +) |
| `pitchAxis` | f32 | [-1, 1] desired pitch (climb +) |
| `throttle` | f32 | [0, 1] desired forward throttle |
| `fire` | bool | fire the front laser this frame |
| `fireMissile` | bool | launch a missile this frame |
| `missileTarget` | u32 | locked target index, or `0xFFFFFFFF` (none) |
| `ecm` | bool | fire the ECM burst (G8) |
| `energyBomb` | bool | detonate the energy bomb (G8) |
| `escapePod` | bool | eject in the escape pod (G8) |

#### Lifecycle

**`EntityDespawn`** — `0x0200` · Wire · Event · Gameplay · S→C. An entity left
the world (reaped player, expired canister, docked trader, fled NPC…) — the
thing absence can't convey on the snapshot stream. Client drops it from view
and clears any missile lock on it.

| Field | Type | Meaning |
|---|---|---|
| `entityId` | u32 | who left |

**`EntityDeath`** — `0x0201` · Wire · Event · Gameplay · S→C. A kill. For the
victim's own session this triggers the death/respawn sequence; for everyone
else it plays the explosion and removes the wreck. (The dying *player's* death
is sent only to that session — the ship respawns immediately, so others never
see it flicker out.)

| Field | Type | Meaning |
|---|---|---|
| `victim` | u32 | destroyed entity |
| `killer` | u32 | credited entity index |

**`EcmPulse`** — `0x0202` · Wire · Event · Gameplay · S→C (broadcast). A ship's
ECM burst fired — a player's activation or an NPC's automatic defence. Plays the
classic buzz; the downed missiles arrive as `EntityDeath` events alongside.

| Field | Type | Meaning |
|---|---|---|
| `source` | u32 | the ship whose ECM fired |

**`EscapePodUsed`** — `0x0203` · Wire · Event · Gameplay · S→C (owner only).
Your pod ejected: the ship is lost (cargo gone, record cleared, tank refilled)
and you are already respawned docked. The client flips to the docked flow.

| Field | Type | Meaning |
|---|---|---|
| `entityId` | u32 | the ejecting ship (your own) |

#### Station & economy

**`StationRequest`** — `0x0400` · Wire · Command · Gameplay · C→S.

| Field | Type | Meaning |
|---|---|---|
| `kind` | u8 enum | see request kinds below |
| `commodity` | u16 | commodity index (Buy/Sell) or `EquipItem` (Equip) |
| `quantity` | u16 | units (Buy/Sell) |
| `stationId` | u32 | destination **system id** (Teleport) |

**`StationResponse`** — `0x0401` · Wire · Event · Gameplay · S→C.

| Field | Type | Meaning |
|---|---|---|
| `kind` | u8 enum | echoes the request kind |
| `status` | u8 enum | see statuses below |
| `credits` | i32 | resulting wallet |
| `commodity` | u16 | echoed commodity |
| `cargo` | u16 | resulting held quantity of `commodity` |

`StationRequestKind`: `Dock=1`, `Undock=2`, `Buy=3`, `Sell=4`, `Equip=5`,
`Refuel=7`. All hit `ProcessStationRequest`. *(`Teleport=6` and `JumpDrive=8`
are **retired** — travel moved to `TravelRequest`; the values stay reserved and
a request carrying them is rejected.)*

`StationStatus`: `Ok=0`, `NotDocked=1`, `NoStock=2`, `NotEnoughCredits=3`,
`HoldFull=4`, `NoCargo=5`, `BadCommodity=6`, `CantDock=7`, `AlreadyOwned=8`,
`DockingRefused=9` (fugitive turned away). *(Values `10–14` are **retired** —
the travel outcomes moved to `TravelStatus`; never reuse them.)*

`EquipItem`: `Missile=1` (30.0 Cr, max 4), `LargeCargoBay=2` (400 Cr, +15 t),
`Ecm=3` (600 Cr), `FuelScoop=4` (525 Cr), `EnergyBomb=5` (900 Cr),
`EscapePod=6` (1000 Cr). *(Ownership is authoritative; ECM/bomb/pod behaviour
lands in G8.)*

#### Travel

**`TravelRequest`** — `0x1000` · Wire · Command · Gameplay · C→S. Take me
somewhere; the server validates fuel/range/mass-lock through
`HyperspaceSystem` (§6.8).

| Field | Type | Meaning |
|---|---|---|
| `kind` | u8 enum | `Hyperspace=1`, `InSystemJump=2` |
| `systemId` | u32 | destination system (Hyperspace; ignored by InSystemJump) |

**`TravelResponse`** — `0x1001` · Wire · Event · Gameplay · S→C. The outcome;
position/fuel changes ride the snapshot stream and `PlayerStatus`.

| Field | Type | Meaning |
|---|---|---|
| `kind` | u8 enum | echoes the request kind |
| `status` | u8 enum | `Arrived=0`, `Witchspace=1`, `Jumped=2`, `NotEnoughFuel=3`, `OutOfRange=4`, `UnknownSystem=5`, `MassLocked=6`, `Rejected=7` |

#### Bulk

**`GalaxyChunkRequest`** — `0x1002` · Wire · Command · Bulk · C→S. The client
**pulls** the galaxy chart in bounded ranges (`baseIndex u32`, `count u16`,
server-clamped to 64) instead of receiving a connect-time fire-hose — the
prerequisite for fog of war (§13.2.3-5).

**`GalaxyChunk`** — `0x1003` · Wire · Event · Bulk · S→C. One slice:
`total u32 | baseIndex u32 | systems vector<entry>` through the generic codec
(entries are nested records: `id u32 | x,y,z i64 | name string | government u8
| economy u8 | techLevel u8 | population u16 | productivity u16`), at most 16
entries per message so each fits a safe datagram. An out-of-range request is
answered with an empty chunk still carrying `total`. The client requests the
next range as each completes, until it holds all `total` systems; the chart
renders progressively meanwhile. *(Replaces the retired hand-encoded `0x0210`
manifest — one serialization path.)*

#### Snapshot stream (not a catalog message)

`'NSNP'` datagrams, unreliable, per-viewer. Since **E2** the format is **version
2**, a compact quantized encoding (32 bytes/entity, down from v1's 58) that
replaces v1 outright (pre-launch, no dual-format negotiation). Header:
`magic u32 | version u16 (=2) | tick u32 | viewerId u32 | refX,Y,Z i64 ×3 |
count u16` (40 bytes), then `count ×` `EntitySnapshot` (32 bytes):

| Field | Type | Meaning |
|---|---|---|
| `id` | u32 | entity index |
| `x, y, z` | **i32 ×3** | position as an **offset from the header's `ref` origin** — see below |
| `noseX..Z` | **i16 ×3** | forward direction, quantized (×32767; 0/±1 exact) |
| `roofX..Z` | **i16 ×3** | up direction (side = nose × roof), quantized |
| `speed` | **u16** | units/tick along nose, 1/256-unit fixed-point (dead-reckoning) |
| `type` | i16 | renderable ship type (legacy `SHIP_*`; see §6.10) |

**Reference origin (keeps the world unbounded int64).** The header carries a
full `int64` reference (`ref`, set to the viewer's position); each entity's
position is a compact `int32` *offset* from it. The absolute world stays
**unbounded int64** — only the offset is int32, and every entity in a snapshot
is within the viewer's area of interest (a few million units), so the offset
always fits int32 however large the galaxy grows. Position is **exact** (integer
subtraction, no float loss); the offset saturates rather than wraps if an
out-of-AOI entity is ever handed in. Orientation/speed quantization is
deterministic integer math (identical on every client — the "server-side
rounding" the E2b delta stage relies on); the decoded in-memory `EntitySnapshot`
is unchanged (int64 pos + float basis), so the interpolator and render path are
untouched. Codecs live in `NeuronCore/Quantization.h`.

Snapshots are **area-of-interest filtered** per viewer: entities within ±1 cell
of a 100 000-unit grid around the viewer, **plus** the local system's
landmarks (planet + station) out to 2 000 000 units so celestial bodies never
pop out mid-approach. Packetized to whole entities ≤ 1200 bytes (the reference
origin is repeated in each split datagram so each decodes independently). Later
snapshots supersede earlier ones; loss is never repaired, only outrun.

**Delta stream (E2b).** Rather than re-sending every visible entity each tick,
the server sends a small **delta** against the snapshot the client last
**acknowledged** — only changed entities (new/moved) and removed ids — plus an
occasional full **keyframe** (forced ~1 s, or when no acked baseline is held, or
for a crowded multi-datagram AOI). A delta shares the `'NSNP'` magic with a
distinct version byte (3) and names its `baselineTick`; a full carries a
`complete` flag so the client only treats a whole-tick snapshot as a baseline.
The client **acks** the latest baseline tick by piggybacking it on
`InputCommand` (`ackSnapshotTick`). Both sides keep a bounded ring of recent
snapshots, so a lost delta self-heals (the server keeps deltaing against the
still-acked older baseline) and reordering is safe (a delta resolves its baseline
by tick). Change detection compares **absolute** positions (stable under the
moving reference origin) at their **quantized** resolution. Codecs:
`SnapshotDelta.h` (diff/apply) and `SnapshotStream.h` (encoder/decoder).

**Send budget (E2c).** Each viewer's per-tick state is capped
(`SNAPSHOT_SEND_BUDGET_BYTES`): an overloaded AOI keeps the entities **closest**
to the viewer and sheds the farthest, sorted by distance then id so the trim is
identical on every client. Applied before delta-encoding; the shed count feeds
the `[metrics]` line (`dropped=`). See `SnapshotBudget.h`.

### 4.5 Server-internal messages (never on the wire)

The server's own combat pipeline is decoupled through an in-process
`MessageBus` with LocalOnly messages (ids in the non-wire half):

| Message | Id | Meaning |
|---|---|---|
| `FireWeapon{shooter, weapon, target}` | `0x8101` | a fire request (synthesized from `InputCommand`); resolved against the world |
| `Crime{offender, victimTeam, firstOffence}` | `0x8102` | a protected victim was fired on; police dispatch on first offence |
| `EntityKilled{victim, killer}` | `0x8103` | something died; ONE subscriber decides what a death does |
| `EcmFired{ship}` | `0x8104` | an ECM burst fired (G8) → the server broadcasts `EcmPulse` |
| `PodEjected{ship}` | `0x8105` | a pod ejected (G8) → owner notify + roster/cargo refresh |
| `ActionTriggered{action, param}` | `0x8200` | **client**-local: raw input → command-builder bridge |

### 4.6 Canonical sequences

**Connect:** client sends `ClientHello{version, name}` (Control, token 0) →
server version-checks it (mismatch ⇒ `HelloReject`, no session), else spawns the
player entity, sanitizes/dedupes the name, mints a CSPRNG session token, and
replies `HelloAck{token, entityId, version}` (Control) → the client stamps that
token on every subsequent datagram (B2) → server broadcasts the full `PlayerInfo`
roster → snapshots + `PlayerStatus`/`CargoManifest` begin flowing. Input from an
endpoint that has not completed this handshake — or that carries the wrong/no
token — is ignored (no spawn-on-first-input). Once connected the client **pulls**
the galaxy chart in bounded ranges (`GalaxyChunkRequest` → `GalaxyChunk`, Bulk)
until it holds all systems.

**Fire → kill → respawn:** `InputCommand.fire` → server publishes `FireWeapon`
→ `ResolveFireWeapon` applies damage, may publish `Crime` (wanted +1, police
launch from the nearest station with a warrant on the offender) and/or
`EntityKilled` → the death subscriber pays the killer (`CreditKill`), scatters
loot (`DropLoot` / `DropPlayerCargo`), and for a player victim: sends that
session `EntityDeath`, restores hull/shields, clears wanted, sweeps every NPC
`focus` off them, respawns them docked at the nearest station, broadcasts the
refreshed `PlayerInfo`, resends `CargoManifest` (now empty). NPC victims are
destroyed and broadcast to everyone.

**Hyperspace:** chart crosshair → `TravelRequest{Hyperspace, systemId}` →
server runs `Hyperspace()` → `TravelResponse{status = Arrived | Witchspace |
NotEnoughFuel | OutOfRange | UnknownSystem}` (+ a `PlayerInfo` broadcast if the
wanted level cooled) → the new position rides the next snapshot; fuel rides
`PlayerStatus`.

---

## 5. The authoritative server

### 5.1 Tick pipeline

Every ~33 ms, in this order:

1. **Drain the socket.** Route datagrams by magic: `InputCommand` →
   `ServerSessions::OnInput` (applied only to a live, handshaken session — an
   unknown endpoint is ignored; stale-sequence drop; intent applied to
   `FlightIntent`; `fire`/`fireMissile` become `FireWeapon` bus messages) ·
   reliable datagrams → `ServerSessions::OnReliable` (an unknown endpoint gets a
   pending, entity-less shell so its `ClientHello` can be received) → per-session
   `MessageEndpoint`.
2. **Dispatch the bus** — fire commands resolve to `Crime`/`EntityKilled` facts.
3. **Roster upkeep** — on membership change, rebroadcast all `PlayerInfo`.
4. **Reliable requests** — per session: `ClientHello` (the front door —
   `OnHello` spawns + `HelloAck` on first valid hello, or a rename on a live one,
   or `HelloReject` on a version mismatch), `StationRequest` (Teleport/JumpDrive
   routed through `HyperspaceSystem`, everything else through
   `ProcessStationRequest`), `TravelRequest` (hyperspace/in-system jump). Gameplay
   requests are gated on the session being live.
5. **`StepAi`** — NPC tactics write `FlightIntent`s (see §6.7).
6. **`GameLogic::Tick`** — `StepFlightInput` (intent → controls through caps)
   → `StepFlight` (orientation/position integration) → `StepMotion` (simple
   velocity movers, e.g. drifting canisters).
7. **Spawning** — `SpawnDirector::Step` (pirates near players, every 600 ticks,
   NPC cap 12) and `StepTraders` (lane traffic, every 900 ticks, cap 2).
8. **Shield regen & equipment upkeep** — every 8 ticks, players'
   shields/energy recharge; every tick, `StepEquipment` cools lasers and
   recharges ECM (§6.12).
9. **Combat resolution** — `StepMissiles` (homing + detonation) + `StepCombat`
   (NPC auto-fire) + `StepCollisions` (G6) → all kills published as
   `EntityKilled`; bus dispatched (deaths resolve; double-reports are guarded).
10. **Loot** — `StepLoot` (age canisters) + `ScoopSystem` (players vacuum or
    smash canisters; changed holds get a `CargoManifest`).
11. **Wanted decay** — every 600 ticks each record cools 1 level; changed
    players get a roster refresh.
12. **Reap** idle sessions (300 ticks ≈ 10 s), diff live entities →
    `EntityDespawn` broadcasts.
13. **Send** — per session: AOI snapshot (+ landmarks), on-change
    `PlayerStatus`, then flush all reliable lanes.

### 5.2 Sessions (`ServerSessions`)

- Keyed by the client's **current endpoint** (`addr<<16 | port`), but the
  IDENTITY is the **session token** (B2): a second index maps token → endpoint.
  A valid, version-checked `ClientHello` (`OnHello`) spawns the player entity (see
  component list below), mints a CSPRNG token, and queues `HelloAck`; a token-less
  reliable datagram from an unknown endpoint first gets a pending, entity-less
  **shell** (`OnReliable`) so that hello can be received. Every later client
  datagram is authenticated by token *before* it touches a session: a wrong/no
  token is dropped (`Authenticate`); a correct token from a new address re-binds
  the session there (NAT rebind heals). Input (`OnInput`) applies only to a live,
  correctly-tokened session — so there is no spawn-on-first-input and no
  endpoint-spoofing. `Session::Live()` (entity valid) distinguishes a connected
  player from a pending shell; pending shells are excluded from the roster and
  reaped on the idle timeout like any session, and their token index is pruned.
- Token-less (pre-handshake) datagrams are **rate-limited** per endpoint
  (`RATE_MAX_UNAUTH` per `RATE_WINDOW_TICKS`, muted `RATE_MUTE_TICKS` on breach)
  so a spoofed-source flood can't provision unbounded shells.
- **Reconnect grace (B3):** a pending shell reaps after `SESSION_TIMEOUT_TICKS`
  (300 ≈ 10 s), but an authenticated session survives `SESSION_GRACE_TICKS`
  (1800 ≈ 60 s) of silence so it can reconnect. `SafeParkSilent` zeros the flight
  intent of a live session silent past `SESSION_PARK_TICKS` (~1.5 s) so a
  disconnected ship stops rather than flies away. A hello on a live session is a
  **resume** (`HelloResult::Resumed`): keep the entity + token, re-queue `HelloAck`.
- Latest-sequence-wins input application.
- Owns the commander-name pipeline: sanitize → cap (20) → de-dupe → stored as
  the authoritative per-player record on the session (C2) → roster broadcast.
- Owns the per-player **identity + records** (C): each session gets a
  `playerId`, the `OwnershipIndex` maps it to every entity it owns, and the
  name/score records live here — off the hull.
- `Broadcast(msg)` queues a catalog message to every session's proper lane.

### 5.3 A player entity (as spawned)

`WorldTransform`, `Flight`, `FlightIntent`, `FlightCaps` (0.121 rad/tick roll &
pitch, 100 u/t max speed), `Wallet` (1000 = 100.0 Cr), `CargoHold` (20 t),
`DockState`, `Equipment` (3 missiles), `Fuel` (70/70 tenths), `PlayerTag`,
`Combatant` (Team Player, 255 energy, laser 10, range 6000, autoEngage
**false**, 150 ticks spawn grace), `Shields` (255/255), `Wanted` (0),
`Owner` (the session's playerId, C1), `NetType` (Viper hull for now). The
commander name and score are **not** components: they are per-player session
records (C2).

---

## 6. Game design (the rules, as implemented)

All quantities in server ticks (~30/s), world units, tenths of a credit and
tenths of a light year, matching the legacy fixed-point scales.

### 6.1 Flight

The intent model: client sends normalized axes; `ResolveIntent` clamps to the
hull's `FlightCaps` — **a client can never out-turn or out-run its ship** —
then the integrator applies roll/pitch to the ship's orthonormal basis
(side/roof/nose) and advances the position along the nose with a sub-unit
`carry` remainder, a faithful port of the legacy `rotate_vec` model inverted to
ship-carries-its-frame. NPCs fly through the *same* pipeline: their AI writes
`FlightIntent` like any client (a deliberate architectural rhyme — *everything
flies by intent*).

### 6.2 Combat

- **Teams:** Player=0, Pirate=1, Police=2, Station=3, Trader=4. Same team never
  auto-fights; players (autoEngage=false) fire only on command but may target
  **other players** — that is how PvP exists at all.
- **Player laser:** on `fire`, `ResolvePlayerFire` picks the nearest enemy
  within 6000 units inside a cos ≥ 0.9 (~25°) aiming cone; damage is the
  ship's laser strength.
- **NPC lasers:** `StepCombat` — each auto-engaging combatant fires at most
  once per `fireInterval` (10 ticks), damage accumulates and resolves
  simultaneously (fire order can't matter).
- **Target memory:** `Combatant.focus` — a locked, live, in-range enemy
  outranks the nearest-enemy scan for **both** fire and AI flight, so an NPC
  shoots what its pilot is flying against. Police warrants (below) live here.
- **Shields (players):** a hit lands on the shield **facing the attacker**
  (sign of the attacker-direction · nose); overflow drains the energy bank
  (`ApplyDamageToShields`, legacy `damage_ship`). NPCs have no shields — damage
  hits energy directly. Regen every 8 ticks: while the bank is over half, one
  point bleeds into each non-full shield, then the bank recovers one (capped
  255) — legacy `regenerate_shields`.
- **Missiles:** real homing entities — speed 180 u/t, life 240 ticks, detonate
  within 400 units for 250 damage (through the same directional-shield path).
  Launched by players (`fireMissile` + lock) and by hurting NPCs (panic
  launch). Missiles carry no `Combatant`, so they can't be targeted, pay no
  bounty, never mass-lock, and never collide.
- **Spawn grace:** 150 ticks of damage immunity on spawn and respawn; shots
  pass through, collisions don't bite, and the countdown is owned by
  `StepCombat` alone.

### 6.3 Crime & the law

- Firing on a **protected** victim — Station, Police, Trader, or a **clean**
  player (wanted 0) — raises the shooter's `Wanted` by 1 and publishes `Crime`.
  A wanted player is *fair game*: attacking them is legal.
- **First offence** dispatches 2 police Vipers, launched **from the system's
  station** when one is within 2M units (legacy stations launched their own),
  else warped in near the offender. Each carries a **warrant**: `focus` fixed
  on the actual offender.
- **Police discipline** (`PoliceMayEngage`): the law engages only pirates and
  wanted players — never traders or the innocent — in both flight targeting
  and fire selection. Warrants self-heal: the lock drops the moment the
  offender dies clean or escapes; player respawn sweeps *every* NPC focus off
  the reborn entity.
- **Wanted decay:** −1 level per 600 ticks (~20 s). A hyperspace jump *halves*
  it (legacy `legal_status /= 2`). Death clears it.
- **Fugitive threshold: 8.** At or above it, stations refuse docking
  (`DockingRefused`) — cool off, jump it down, or die.
- Killing a fugitive player pays a bounty of `20 × wantedLevel`; killing a
  clean player pays nothing (but still scores). Ramming is not a crime (no
  shot) — piracy by collision is dangerous but legal.

### 6.4 Death & respawn

On a player kill: pay the killer *before* the record wipes → spill the whole
cargo hold as canisters at the wreck → restore energy/shields to max, grant
respawn grace, zero the wanted record → **respawn docked at the nearest
station anywhere in the world** (hold emptied, capacity/equipment/credits/score
kept) → sweep NPC grudges → refresh the roster → resend the (empty)
`CargoManifest`. Only the dying session receives `EntityDeath` for the player,
so nobody else sees the respawned ship blink. NPC deaths broadcast
`EntityDeath` to everyone, pay bounties, drop loot, and destroy the entity.

### 6.5 Loot & scooping

- **Kill drops** (`DropLoot`, legacy `launch_loot`): a destroyed real combatant
  sheds an *alloys* batch and a *cargo* batch. Each batch: rand255 ≥ 128 → drop
  nothing; else `count & 3` single-unit canisters. Cargo pods roll a random
  tradeable good (index `rand & 7`); alloy splinters are commodity 9.
- **Player-death drops** (`DropPlayerCargo`): one canister per non-empty
  commodity stack, carrying the whole stack.
- A **canister** is a real entity: `WorldTransform` + small random drift
  `Velocity` + `LootItem{commodity, units, life}` + `NetType` mesh (Alloys→4,
  Minerals→8/Rock, else 5/Cargo). It despawns after 3600 ticks (~2 min).
- **Scooping** (`ScoopSystem`, legacy `scoop_item`): a non-docked player within
  600 units of a canister consumes it — into the hold if they own a **fuel
  scoop** and (for tonnage goods, commodities 0–12) the hold has room;
  otherwise contact smashes it. First player claims each canister; a scoop
  triggers a `CargoManifest` resend (+ pickup sound client-side).

### 6.6 Economy

- **17 commodities** (legacy table: Food … Alien Items). Tonnage goods are
  indices 0–12; Gold/Platinum/Gems/Alien Items don't count against hold space.
- Each station carries its **own authoritative market**, generated from its
  system's economy + market seed (`GenerateMarket`, a faithful
  `generate_stock_market` port: 8-bit wrap, >127 → 0 clamp, quantities 0–63,
  Alien Items never stocked, price stored ×4).
- Buy/Sell validate docked state, stock, credits, hold space; move
  credits/cargo/stock atomically. The client's market/hold displays mirror
  `StationResponse` / `CargoManifest` — it can neither conjure credits nor
  cargo.
- **Refuel:** 2 per tenth (0.2 Cr / 0.1 LY); fills as far as the wallet
  allows; full tank is an Ok no-op; broke is `NotEnoughCredits`.
- **Equipment** is server-owned (§4.4 prices); the large cargo bay immediately
  raises hold capacity +15 t. Wallet starts at 100.0 Cr.

### 6.7 NPCs & AI

All NPCs think in `StepAi`, staggered by the legacy scheduler — a ship thinks
every 8th tick (`(index ^ tick) & 7 == 0`) — and act by writing `FlightIntent`.
The steering is a faithful `tactics()`/`track_object()` port; the legacy
`rotx/rotz` countdown-timer model (fixed 1/19 rad step, magnitude = frames
remaining) maps exactly onto held intents with an NPC turn cap of
7/152 rad/tick and axis = legacyRate/7.

| | Pirate | Police | Trader | Thargoid |
|---|---|---|---|---|
| Hull (`NetType`) | Viper (16) | Viper (16) | Shuttle (9)/Transporter (10) | Thargoid (29) |
| Energy | 80 | 120 | 60 | 120 |
| Laser / range | 3 / 5000 | 4 / 6000 | — (0) | 4 / 5000 |
| Bravery | 64–127 | 113 | 0 | 120 |
| Missiles | 2 | 1 | 0 | 1 |
| Max speed | 114 | 114 | 30 | 114 |
| Bounty | 50 | — (crime) | — (crime) | 100 |
| Spawned by | every 600 ticks near a player (cap 12 NPCs) | Crime (2, station launch, warrant) | every 900 ticks on a lane (cap 2) | witchspace misjump (1–4) |

Behaviour building blocks (constants in `AiSystem.h`):

- **Pursue:** steer with the 0.111 deadzone, roll/pitch sign coupling, hard
  pitch when the target is behind (< −0.861). Throttle: ease off ≤ −0.167,
  push ≥ 0.223, cruise floor 6/32, never below 1/32 (NPCs never stop) — the
  legacy ±3/−1 acceleration nudges as persistent throttle steps.
- **Attack runs:** commit only from outside the close box (768 along the
  target's nose, 512 across) on a bravery roll (`bravery > rand & 127`); a
  failed roll evades. Aligned within 0.833 inside 8192 units: track and bleed
  speed; inside the close box: **break off** (random pitch, full throttle) —
  the anti-ramming rule, deliberately wider than the 600-unit collision range.
- **Jink:** 1-in-25 per think, a full-deflection roll latched ~13–15 thinks.
- **Self-preservation:** +1 energy regen per think. Below half energy: panic
  missile (`missiles ≥ rand & 31`). Below ⅛ energy (~10 %/think): **flee** —
  stop firing (`autoEngage` off), run flat out, despawn once clear of every
  enemy within 16 384 units (the legacy escape-pod eject, translated).
- **Targeting:** pirates prefer **civilians** (players/traders) over the
  police shooting at them; police obey `PoliceMayEngage`; nobody attack-runs a
  station; `focus` keeps flight and fire on the same prey.
- **Traders:** fly a station ↔ planet-gate lane with the legacy autopilot
  variant (deadzone 0.1666, dropped when the waypoint is > 131° behind; full
  throttle only within ~36° / 0.8055), dock-despawn within 1500 of the
  endpoint, and flee for good when hurt below half — cowards by design.

### 6.8 Travel (G7)

- **Fuel:** 0–70 tenths (7.0 LY), full at spawn. Scale:
  `UNITS_PER_TENTH_LY = 500 000` ⇒ a full tank spans ~35M units, calibrated
  against the real galaxy (mean nearest-neighbour ≈ 14.5M) so refuelling gates
  onward travel without stranding anyone.
- **Hyperspace** (`Teleport` request, works docked or in flight): cost =
  Euclidean distance / 500k, floored at 1 tenth. Rejections: `OutOfRange`
  (beyond a full tank), `NotEnoughFuel` (beyond the current tank),
  `CantDock` (unknown system). On success: fuel deducted, wanted halved,
  undocked, relocated to the destination station + 2000 units — **arriving in
  flight**, fly in and dock.
- **Witchspace:** rand255 > 253 (~0.8 %) per jump — fuel still spent, the ship
  is flung 20M units off the destination into deep space, marked `Witchspace`,
  and ambushed by 1–4 Thargoids. **Kills made in witchspace pay no bounty**
  (score still counts). A later clean jump clears the marker.
- **In-system jump** (`JumpDrive` request, the legacy `jump_warp`): shoves the
  ship up to 200k units toward the nearest planet, stopping 6000 clear of the
  kill radius — **unless mass-locked** by any other hull (ship or station) or a
  planet within 75k units. Canisters/missiles never mass-lock (no `Combatant`),
  matching the legacy cargo/rock exemption.

### 6.9 Collisions & environment (G6)

Per tick (`StepCollisions`), Chebyshev ranges, damage repeats while contact
holds (grinding, the legacy near-fatal feel):

| Contact | Range | Effect |
|---|---|---|
| ship ↔ ship | 600 (scoop-calibrated) | 100/tick to **both**, routed through the facing-shield path; a ram kill credits the surviving hull (bounty/loot flow normally) |
| ship ↔ station | 1000 | 200/tick to the **ship only** — the fortress doesn't notice; the safe way in is the dock request (range 5000) |
| ship ↔ planet | 4000 | instant death (legacy zero-altitude), killer = the planet |

Exempt: docked ships (inside the station, not in space), spawn/respawn grace
(without consuming it), and anything without a `Combatant`. All launch offsets
(undock, police, traders: 2000) are born clear of every contact range, and
trader lanes end at a **gate** 6000 above the planet, outside the kill zone.
Sun proximity / cabin heat is deferred with the fuel-scoop payoff to G8+ (no
sun entities exist yet — see §14).

### 6.10 World generation

- **Home system** (id −1): planet at (0, 0, 65536), station at (0, 0, −3000)
  with its own market, one hand-placed pirate on the way to the planet.
  Players spawn near the station, spread 2000 units apart, docked=false.
- **Procedural galaxy:** 256 systems scattered over ±100M units from a single
  seed (`0xC0FFEE`); each system gets a planet entity, a station entity
  (orbit +8000 x) with a generated market, and legacy-style name/attributes
  synthesized from a per-system seed. Shipped to clients once as the manifest.
- **Replicated ship types** (`NetType.type` = legacy `SHIP_*`): Sun −2,
  Planet −1, Missile 1, Coriolis 2, Alloy 4, Cargo 5, Rock 8, Shuttle 9,
  Transporter 10, Viper 16, Thargoid 29. The client maps them straight onto
  the legacy meshes; 0 draws a default ship.

### 6.11 Scoring & bounties

Bounty sources: explicit `Bounty` component (pirate 50 = 5.0 Cr, Thargoid 100)
or wanted-derived for fugitive players (20/level). `CreditKill` pays the
killer's wallet in place and returns the earned `KillCredit{bounty, score}`;
the server routes the score to the killer's session record (C2) — only a
player killer with a wallet earns; missiles' own detonations credit their
owner; witchspace withholds the money but not the score.

### 6.12 Equipment (G8)

The purchased items work, all server-validated (`EquipmentSystem`):

- **ECM** (activation, 32 energy, 32-tick recharge): downs EVERY in-flight
  missile within 12 000 units — anyone's, including your own (the legacy burst
  was indiscriminate). NPCs get the legacy *automatic* defence instead: each
  tick a missile homes on an ECM-fitted target it has a 16/256 chance of being
  jammed. Fittings: police and traders always (`EcmFitted`), pirates ~50 %,
  Thargoids never; players buy theirs.
- **Energy bomb** (one shot, consumed, in flight only): kills every NPC hull
  and missile within 16 384 units. Stations are immune (the legacy
  Coriolis/Dodec exemption) and so are players — no area one-shots on people.
  Each police/trader victim is a separate crime.
- **Escape pod** (consumed, in flight, never in witchspace): the legacy
  `abandon_ship` — cargo lost with the hull (nothing spilled), record CLEARED,
  tank refilled, hull/shields restored with respawn grace, and you wake docked
  at the nearest station. An expensive, legitimate get-out-of-trouble card.
- **Laser temperature** (players only): +8 per trigger pull and −1 energy; at
  242+ the trigger locks until it cools (−1/tick). Sustained fire locks after
  31 pulls (~1 s at 30 Hz), forcing the legacy fire discipline.

---

## 7. Client presentation layer

The client is deliberately dumb. It keeps:

- **Rendering:** DX11, legacy wireframe meshes, camera-relative floating
  origin. Replicated entities are drawn from interpolated snapshots
  (`SnapshotInterpolator` + dead-reckoning on `speed`).
- **HUD mirrors:** shields/energy/fuel/credits/missiles/cargo/wanted/score from
  `PlayerStatus` + `CargoManifest`; the roster (`PlayerInfo`) for ship labels;
  the market/chart from `StationResponse`/the pulled galaxy chunks. There is
  **no offline simulation**: a disconnected client shows a connection-lost
  screen and retries (the single-player fallback was deleted — S4 extended).
- **Input:** raw keys → `ActionTriggered` (LocalOnly bus) → command builder →
  one `InputCommand` per frame. Station screens send `StationRequest`s.
  The hyperspace key on a chart sends `TravelRequest{Hyperspace}` (docked or
  in flight); the jump key sends `TravelRequest{InSystemJump}`; the server
  answers both with a `TravelResponse`. In-flight docking is request-based:
  the proximity check and the docking computer only *send* a Dock request,
  and the docked flow starts on `StationResponse{Dock, Ok}`.
- **Presentation effects:** death/explosion VFX (a world-anchored replicated
  explosion re-using the legacy debris animation), sounds (launch, hits, ECM,
  hyperspace, scoop beep), the break-pattern screen transitions.

A `TravelResponse{Hyperspace, Arrived|Witchspace}` flips the client from the
station screen into flight; position updates always come from snapshots.

---

## 8. Determinism & testing

- **Everything gameplay is headless-testable**: no sockets, no GPU, no clock,
  no global RNG. 200+ GameLogic tests + NeuronCore protocol suites run in CI
  (Windows, MSVC, x64 debug + release) and locally.
- **Parity tests** pin the wire ABI: golden byte layouts for the folded legacy
  codecs, round-trips for every catalog message, and registry governance
  (unique ids, scope/id-band consistency, wire direction present).
- **Behavioural tests** run real mini-simulations: AI steering convergence
  through the actual flight integrator from five orientations, flee-escape-
  despawn cycles, seeded witchspace misjumps, collision exemptions, trader
  lane runs — same seed, same world ⇒ bit-identical positions.
- The engine **forbids wall-clock randomness**; any new system takes an
  explicit seeded stream. This is what makes golden-run tests possible and is
  a hard prerequisite for the future replay/reconciliation work.

---

## 9. Security & anti-cheat posture

- The client is untrusted by construction: it holds no authoritative state and
  every `Command` is validated (docked checks, stock/credit checks, hold
  space, fuel gates, range gates, aim cones, per-hull intent clamps).
- Wire hygiene: per-record length bounds; string/vector caps (4096); truncated
  or foreign buffers fail decode safely; wrong-direction messages are
  rejectable by trait; LocalOnly ids cannot be serialized at compile time.
- Entity references from the wire are bare indices resolved through
  `LiveEntity` (generation check) before use.
- Names are sanitized (printable ASCII, length-capped, de-duplicated)
  server-side.
- Datagrams are authenticated by the session token (B2): the server keys
  sessions by a CSPRNG token, not by source address, and drops a wrong/no-token
  client datagram before decoding it. Token-less (pre-handshake) datagrams are
  rate-limited per endpoint.
- Not yet addressed (future): encryption (tokens travel in cleartext — a
  same-path attacker can still read them; TLS/DTLS or a challenge exchange is
  post-F), server-side sanity on input *cadence* (a client can send at > 30 Hz;
  only the latest wins, so the damage is bounded).

---

## 10. Key constants (quick reference)

| Constant | Value | Where |
|---|---|---|
| Tick rate | ~30 Hz (33 ms) | Server/Main |
| Server port | 40000 UDP | Server/Main |
| Safe datagram payload | 1200 B | Replication.h |
| AOI cell / radius | 100 000 / ±1 cell | Server/Main |
| Landmark visibility | 2 000 000 | Server/Main |
| Session timeout | 300 ticks (~10 s) | Server/Main |
| Dock range | 5000 | Server/Main |
| Player laser range / cone | 6000 / cos 0.9 | Server/Main |
| Launch/undock offset | 2000 | StationServices.h |
| Max shields / energy | 255 / 255 | CombatSystem.h |
| Shield regen cadence | 8 ticks | Server/Main |
| Respawn grace | 150 ticks | CombatSystem.h |
| Wanted decay | 1 level / 600 ticks | Server/Main |
| Fugitive threshold | 8 | CombatSystem.h |
| Bounties: pirate / thargoid / per-wanted-level | 50 / 100 / 20 | CombatSystem.h, HyperspaceSystem.h |
| Missile speed / life / damage / detonate | 180 / 240 / 250 / 400 | MissileSystem.h |
| Loot life / scoop range | 3600 ticks / 600 | LootSystem.h |
| Ship / station contact, planet kill | 600 / 1000 / 4000 | CollisionSystem.h |
| Ram / station-scrape damage | 100 / 200 per tick | CollisionSystem.h |
| NPC turn cap / engage range | 7/152 rad/tick / 16 384 | AiSystem.h |
| NPC / trader max speed | 114 / 30 | AiSystem.h |
| Pirate spawn cadence / NPC cap | 600 ticks / 12 | Server/Main, SpawnDirector.h |
| Trader cadence / cap / dock range | 900 ticks / 2 / 1500 | SpawnDirector.h, AiSystem.h |
| Fuel max / price / units-per-tenth-LY | 70 / 2 / 500 000 | StationServices.h, HyperspaceSystem.h |
| ECM cost / recharge / range | 32 energy / 32 ticks / 12 000 | EquipmentSystem.h |
| Missile auto-jam chance | 16/256 per tick | MissileSystem.h |
| Energy bomb radius | 16 384 | EquipmentSystem.h |
| Laser heat per shot / lock / cool | +8 / 242 / −1 per tick | EquipmentSystem.h |
| Witchspace odds / displacement | >253 of 256 (~0.8 %) / 20M | HyperspaceSystem.h |
| Mass-lock / in-system hop | 75 000 / ≤200 000 | HyperspaceSystem.h |
| Galaxy systems / extent / station orbit | 256 / ±100M / 8000 | GalaxyGen.h |
| Commander name cap | 20 chars | ServerSessions.h |
| String / vector wire caps | 4096 / 4096 | Serialize.h |

---

## 11. Message id inventory (complete)

| Id | Message | Scope | Lane | Dir |
|---|---|---|---|---|
| `0x0001` | AssignPlayer *(RETIRED)* | Control | Control | S→C |
| `0x0002` | ClientHello | Control | Control | C→S |
| `0x0003` | HelloAck | Control | Control | S→C |
| `0x0004` | HelloReject | Control | Control | S→C |
| `0x0005` | *reserved* (the never-shipped `AssignControl`; identity folded into `HelloAck` at C1) | — | — | — |
| `0x0006` | Ping | Control | Control | C→S |
| `0x0007` | Pong | Control | Control | S→C |
| `0x0100` | InputCommand | Wire | Unreliable | C→S |
| `0x0200` | EntityDespawn | Wire | Gameplay | S→C |
| `0x0201` | EntityDeath | Wire | Gameplay | S→C |
| `0x0202` | EcmPulse | Wire | Gameplay | S→C |
| `0x0203` | EscapePodUsed | Wire | Gameplay | S→C (owner) |
| `0x0210` | *retired* (was the hand-encoded GalaxyManifest chunk → `GalaxyChunk 0x1003`) | — | — | — |
| `0x0300` | Chat *(UI pending)* | Wire | Gameplay | Both |
| `0x0301` | PlayerInfo | Wire | Gameplay | S→C |
| `0x0302` | PlayerStatus | Wire | Gameplay | S→C (owner) |
| `0x0303` | CargoManifest | Wire | Gameplay | S→C (owner) |
| `0x0400` | StationRequest | Wire | Gameplay | C→S |
| `0x0401` | StationResponse | Wire | Gameplay | S→C |
| `0x1000` | TravelRequest | Wire | Gameplay | C→S |
| `0x1001` | TravelResponse | Wire | Gameplay | S→C |
| `0x1002` | GalaxyChunkRequest | Wire | Bulk | C→S |
| `0x1003` | GalaxyChunk | Wire | Bulk | S→C |
| `0x8101` | FireWeapon | LocalOnly (server) | — | — |
| `0x8102` | Crime | LocalOnly (server) | — | — |
| `0x8103` | EntityKilled | LocalOnly (server) | — | — |
| `0x8104` | EcmFired | LocalOnly (server) | — | — |
| `0x8105` | PodEjected | LocalOnly (server) | — | — |
| `0x8200` | ActionTriggered | LocalOnly (client) | — | — |

Plus the two non-catalog streams: `'NSNP'` snapshots (§4.4) and the raw
`'NRLB'`/`'NEVT'` reliability framing (§4.1).

---

## 12. Locked design decisions & trajectory

Preserved from the retired migration roadmap (its §0 and §2.4) — these are
**owner-locked** and every recommendation in §13 honors them.

| Topic | Decision |
|---|---|
| Platform | Windows client **and** Windows server (MSVC, DX11) |
| Authority | Server-authoritative; clients send intent, render replicated state |
| Trajectory | Gameplay evolves from space-flight toward a **4X / RTS-style MMO** (many units per player, empire/economy/territory, less twitch) — as an *extension*, never a rewrite |
| World | One **seamless** absolute `int64³` space, no visible segments; an invisible cell partition underneath for interest management and future multi-process sharding |
| Identity | **Account → Empire/Faction → owns N entities.** A player is *not* bound to one avatar; camera & interest are view-driven |
| Input | Command/intent protocol (validated orders with costs/preconditions) — today flight axes, tomorrow unit orders; the anti-cheat boundary |
| Streaming | Multi-resolution AOI: a high-detail **tactical** tier + a low-detail **strategic** tier (territory/fleet summaries) |
| Transport | Raw winsock UDP + the custom reliability layer; hand-rolled binary hot path |
| Persistence | Microsoft SQL Server; async batched writes off the sim thread; the world simulates while players are offline; **never** per-tick positions to SQL |
| Entity model | In-house sparse-set ECS in NeuronCore; indexed spatially **and** relationally (owner/faction/group/tag) |
| Sim cadence | **Decoupled clocks**: sim tick, command intake, and replication are separate rates; tactical replicates faster than strategic |
| Logic boundary | `GameLogic` is server-only; the client shares **data schemas only**, never behavior |
| Scale model | **Replication, not lockstep** — determinism kept for replays/tests; 100-player scale via interest-managed state replication |
| Test harness | Headless `BotClient` over the real net stack for the 100-player load milestone |
| Aesthetic | The faithful **low-poly wireframe / retro-vector** look is the art direction, not a placeholder — rendering work amplifies it, never replaces it |

Phase status at retirement of the roadmap: 0/A/C ✅ · B/D/E/G 🟡 (BotClient,
strategic tier, delta/quantization, prediction, chat outstanding) · F/H/I 🔴.
Missions are deferred until after F. The **persistence-readiness rule** from
the Phase G plan is promoted to a standing invariant here: *every
durable-in-spirit piece of state lives in a plain serializable component*
(`Wallet`, `CargoHold`, `Fuel`, `Wanted`, `Equipment`, …) or a plain
per-player session record (name/score, C2) so Phase F serializes state
without refactoring gameplay.

---

## 13. Architectural review — 4X Space MMO readiness

**Reviewed 2026-07-03** against the locked trajectory in §12: a
server-authoritative, retro-future **tactical-digital 4X space MMO** — low-poly
wireframe presentation over a large-scale emergent simulation. Verdict in one
paragraph:

> The bones are unusually good. The single load-bearing rule (server
> simulates, client renders, only schemas are shared) is actually enforced —
> intent clamping, compile-time message traits, bounded decoding, seeded
> deterministic RNG, headless tests. The weaknesses are at the edges: a
> handful of protocol conveniences that accumulated special cases (§13.1); MMO
> and 4X table-stakes that are acknowledged but unbuilt — persistence,
> session security, the strategic tier, and above all the **identity layer**,
> where the as-built protocol has quietly re-concretized the single-avatar
> assumption §12 explicitly forbids (§13.2); and hot paths that are
> fine at 12 NPCs but shaped wrong for fleets — O(n²) pair sweeps, per-tick
> allocation churn, an unquantized 58-byte snapshot (§13.3). Nothing below
> changes the concept, the lore, or the wireframe direction; everything
> builds on the existing seams.

### 13.1 Concept integrity & simplification

Ordered by value ÷ effort. Message ids are permanent ABI (§4.3), so protocol
simplifications mean *introducing a successor id and retiring the old one*,
never mutating in place.

**S1 — Invert the handshake: `ClientHello` becomes the front door.**
✅ *Done 2026-07-03 (B1):* previously any first `InputCommand` from an unknown
endpoint spawned an entity, provisioned a session, and (with the old manifest)
streamed kilobytes *before* the server had seen a protocol version — a version
check after the entity exists, an amplification/DoS primitive, and permanent
"hello-before-or-after-input" state-machine complexity. Now unknown endpoints
are ignored until a valid, version-checked `ClientHello` arrives on the Control
lane; a reliable datagram from a new endpoint gets only a pending, entity-less
shell so that hello can be received, and *that* spawns the session (`OnHello`).
The reply is `HelloAck` (or `HelloReject` on a version mismatch). This deleted
the `Commander-<n>` placeholder-on-input path (the hello always carries the
name) and gives session-security work (§13.2.2 / B2) a single choke point.

**S2 — One serialization path: fold the galaxy manifest into the catalog codec.**
✅ *Done 2026-07-03:* the hand-encoded `0x0210` chunk is retired; the client
pulls the chart with `GalaxyChunkRequest`/`GalaxyChunk` (`0x1002`/`0x1003`,
§4.4) through the generic codec, request-driven (`baseIndex/count`) rather
than a connect-time fire-hose — bounding the connect burst and readying
fog-of-war (§13.2.3).

**S3 — Split travel out of the station protocol.**
✅ *Done 2026-07-03:* `TravelRequest{kind, systemId}` / `TravelResponse{status}`
(`0x1000`/`0x1001`, §4.4) carry travel; the station protocol is docking +
commerce again. `StationRequestKind::Teleport/JumpDrive` and the travel
`StationStatus` values are retired in place (reserved, rejected if received).

**S4 — Delete the client's single-player shield-regen fallback.**
✅ *Done 2026-07-03, extended:* the whole single-player fallback engine was
deleted (not just shield regen — local combat/AI/spawning, local travel, the
client-side altitude/cabin-temp deaths, local market/equipment mutation). A
disconnected client shows a connection-lost state and retries. The central
claim is literally true; see docs/IMPLEMENTATION.md A1 for the residue notes.

**S5 — Replace `Sleep(33)` with an accumulator-based fixed timestep.**
`Sleep` guarantees *at least* the delay; tick duration drifts under load, so
every "600 ticks ≈ 20 s" rule silently stretches. Because all rates are
already tick-denominated (good), the fix is confined to the host loop: run N
catch-up ticks when behind, sleep the remainder when ahead. Also yields the
tick-overrun metric Phase H needs, for free.

**S6 — Two math stacks, not three.**
The document sanctions DirectXMath for presentation and
`Vector3i64`/`Vector3d` for simulation — correct. The frozen
`LegacyVector*`/`Matrix33` wrappers are a third stack that every new
contributor must learn to *not* use. Schedule their retirement file-by-file as
legacy presentation code is touched; freezing is a state, not a plan.

**S7 — Id-band hygiene before the bands ossify.**
`EcmPulse` (`0x0202`) and `EscapePodUsed` (`0x0203`) are gameplay events
sitting in the replication-lifecycle band. Ids are permanent, so: grandfather
these two with a note in §4.3, and declare that future combat/VFX events
allocate from the game-specific band (`0x1000+`). *(The
`InputCommand`/`Net::ClientInput` alias half of this item is done — the alias
was struck 2026-07-03; `Msg::InputCommand` is the one catalog name.)*

**What is *not* over-engineered — do not "simplify" these.**
The five-trait message catalog, the three reliable lanes, the sparse-set ECS,
the separate seeded LCG streams, Chebyshev gating on `int64` coordinates, and
intent-based flight for players *and* NPCs are all proportionate mechanism:
each buys a compile-time guarantee, a head-of-line-blocking fix, or the
determinism the test strategy depends on. The snapshot stream staying outside
the catalog codec is also correct — it is a packed hot path with its own
packetizer, and forcing it through `Fields()` would cost real bytes and CPU
for uniformity's sake.

### 13.2 Missing features & functional gaps

#### 13.2.1 Wireframe tactical rendering & spatial partitioning

The wireframe aesthetic is not just art direction — it is a **performance
budget**. A hull is tens of line segments, not tens of thousands of shaded
triangles; a thousand-ship battle is only a few hundred thousand lines. The
current renderer does not cash that cheque:

- **Batched, instanced vector rendering.** The legacy path draws each object
  immediately, mesh by mesh. Replace with: one persistent line-list vertex
  buffer per hull type (`NetType` → mesh is already 1:1), one per-frame
  instance buffer (world transform ± palette tint per replicated entity), one
  `DrawIndexedInstanced` per hull type. Draw calls become O(hull types), not
  O(entities) — the single change that makes fleet-scale battles renderable.
- **Aesthetic as post-process, not per-object cost.** The retro-vector look
  (line glow/bloom, additive trails, depth-faded tactical grid) belongs in a
  small post chain: render lines to an emissive target, blur, composite.
  Expand raw lines to anti-aliased screen-space quads in the vertex shader
  (4 vertices per segment via `SV_VertexID`, no geometry shader) so line
  weight is a style parameter, not a raster accident.
- **Client-side spatial partitioning + iconic LOD.** `Spatial::Grid` lives in
  NeuronCore but the client uses no partition: no frustum/range culling, and
  every AOI entity renders as its full mesh at any distance. Add a
  grid-backed cull, and beyond a range threshold draw the *glyph*, not the
  mesh — a 2–6 line vector icon per hull class. Iconic LOD **is** the
  tactical-digital look (distant contacts as symbology) and simultaneously
  the LOD strategy; it also becomes the render path for the strategic tier
  below. No smooth-LOD/mesh-decimation machinery is needed or wanted for
  low-poly wireframe.
- **Snapshot-type indirection.** `NetType` values are raw legacy `SHIP_*`
  ints shared by sim and render. Before hull variety grows (§13.2.3 drones,
  outposts), route them through a client-side table (`NetType` → mesh, glyph,
  palette row) so adding a hull is data, not a switch statement.

#### 13.2.2 4X MMO scalability — state, ticking, concurrency, persistence

In dependency order; the first three block everything else being "real".

- **Persistence (Phase F) — the top structural gap.** ✅ *Done 2026-07-04 (B4):*
  the persistence service (NeuronServer) runs a single writer thread off the sim
  thread — the sim only ever copies structs onto coalesced queues (one snapshot per
  player, latest wins) and drains completed loads; it never touches the store or
  blocks on the DB. `IPersistenceStore` swaps an `InMemoryStore` (tests/CI) for the
  raw-ODBC `OdbcStore` (SQL Server, behind the `DSO_ENABLE_ODBC` soak flag).
  Load-on-hello DEFERS the spawn until the commander's durable state loads, so a
  returning commander is never spawned-fresh (which a save would alias). Cadence
  saves (150 ticks, change-gated) + a shutdown flush persist wallet/cargo/fuel/
  standing/equipment and the wake-docked system. The schema (`NeuronServer/
  schema.sql`) carries accounts, **empires**, players, cargo, markets, world-meta,
  and an append-only command log; `empires` exists from day one so Track C is an
  additive migration. `DSO_DB` unset ⇒ the whole feature is off (unchanged server).
- **Session security: the UDP endpoint must stop being the identity.**
  ✅ *Done 2026-07-04 (B2):* `HelloAck` hands the client a CSPRNG 64-bit token;
  every subsequent `'NMSG'`/`'NRLB'` datagram carries it after the lane byte, and
  the server authenticates by token before any decode (`Authenticate`) — a spoofed
  source address with the wrong/no token is dropped, and a correct token from a new
  address re-binds the session (NAT rebind heals). Token-less datagrams are
  rate-limited per endpoint. Endpoint-as-identity is gone; accounts still arrive
  with F.
- **Reconnect & resume.** ✅ *Done 2026-07-04 (B3):* an authenticated session now
  gets a 60 s (1800-tick) grace window instead of the 10 s shell reap, and a
  silent ship is **safe-parked** (its flight intent zeroed after ~1.5 s) so it
  stops coasting on stale input during the gap. A token-bearing reconnect re-binds
  the session to the new address (B2) and a hello on the live session **resumes**
  it — the server re-queues `HelloAck` and re-sends that client its roster, cargo,
  and status. (Reliable-lane sequence continuity assumes the client keeps its
  transport across the blip, which the current client does; a full channel-reset
  resume is post-C.)
- **Time synchronization, then lag compensation.** ✅ **Done (E1, 2026-07-04).**
  Control-lane `Ping`/`Pong` (0x0006/0x0007) at ~1 Hz give a smoothed RTT;
  `ResolvePlayerFire` rewinds targets through a 15-tick transform ring at
  `now − RTT/2 − interpDelay` (favour-the-shooter, clamped to the ring).
  Determinism unaffected (derived state). Original note retained below for
  context. Snapshots carried a `tick` but no shared-clock contract:
  interpolation delay was a guess and there was no RTT estimate to compensate
  against. Dogfighting at 100+ ms RTT punished exactly the players an MMO must
  keep.
- **Replication depth (Phase D debt): quantization, delta, budgets.**
  🟡 **Quantization done (E2a, 2026-07-04); delta + budgets pending (E2b/E2c).**
  The v1 58-byte `EntitySnapshot` re-sent full `int64` positions and two full
  float basis vectors every tick to every viewer; v2 is 32 bytes (int32
  position offset from a per-packet int64 reference origin, int16 basis, u16
  speed). Bandwidth — not CPU — is
  the 4X scaling wall (units ≫ players). See §13.3-E4 for the concrete
  re-cut; the *architectural* requirements are: per-session delta against a
  last-acked baseline, per-lane byte budgets, and a documented snapshot send
  rate (today implicitly "every tick", which is the knob this work turns).
- **The strategic tier — the second AOI resolution §12 promises.** There is
  exactly one interest radius. A commander with holdings in three systems is
  blind to all of them; widening the tactical AOI is the wrong (bandwidth-
  catastrophic) answer. Add a low-rate (0.5–1 Hz), reliable-lane summary
  stream keyed by system id — aggregate counts, ownership, alerts ("your
  station in Lave is under attack") — filtered by known systems (§13.2.3),
  rendered by the chart screen and the iconic-LOD path (§13.2.1). This also
  realizes the decoupled-clocks decision: tactical at tick rate, strategic
  at its own cadence.
- **Ticking & concurrency shape.** One thread runs everything (§5.1). That
  is *correct today* — do not parallelize ahead of profiling — but the 4X
  entity counts will outgrow it, so keep the phases parallelizable:
  AI-think is read-world/write-own-intent (data-parallel by construction),
  snapshot building is per-session independent, collision broadphase
  partitions by cell. Parallelize *by phase with deterministic partitioning
  and ordered merge*, never by handing entities to free-running threads —
  that would forfeit the golden-run determinism the test strategy is built
  on. The invisible cell partition is also the future shard boundary; the
  discipline that keeps sharding cheap later is keeping systems cell-local
  now (no system should casually scan the whole world when the grid can
  answer).
- **MMO table stakes with existing seams:** broadcast a cosmetic
  `ExplosionAt{pos}` on player kills (today the killer sees their victim
  silently vanish — wrong trade for PvP); validate `missileTarget`
  server-side with the same range+cone gate the laser has; finish chat with
  relay-side rate limits and a client mute list designed in from day one.

#### 13.2.3 Emergent systems & algorithmic automation

The Darwinia comparison is architecturally load-bearing in one specific way:
**indirect control** — the commander issues orders; units pilot themselves.
This engine is unusually pre-adapted because *NPCs already fly by writing
`FlightIntent` through the same pipeline as players* (§6.1). Every feature
below exploits that seam; none touches the lore or the flight feel.

1. **The identity layer (player ≠ avatar) — do this first.** ✅ **Done (C1 +
   C2).** Each session owns a `PlayerId`; name/score are per-player session
   records off the hull; `Owner{playerId}` is a component with the
   `OwnershipIndex` relational index ("all my units" as a cheap query, per
   §12); `HelloAck` carries `{token, playerId, primaryEntityId, version}`
   (extended in place pre-launch instead of a successor `AssignControl` id).
   `Wallet` deliberately stays ship-borne until multiple hulls trade
   concurrently (F). Short-term behavior identical; it unblocks every item
   below.
2. **Ordered units — the Darwinia move.** A player-owned escort/drone is an
   NPC hull with `Owner{you}` whose `AiSystem` target/waypoint comes from a
   validated `UnitOrder{unitId, order: Escort|Attack|Patrol|Dock|Route,
   target}` message (`0x1000+` band) instead of the spawn director. The AI,
   flight model, combat, and replication all already exist — indirect
   control is literally the current architecture with a different order
   source. Vertical slice: one buyable escort fighter.
3. **Automated resource routing.** The trader autopilot (station ↔ gate
   lanes, dock-despawn) is already a working logistics primitive. Owned
   haulers reuse it with a `Route{stationIds[]}` order. Above that, a
   per-empire **flow pass** at strategic cadence (once per few seconds, not
   per tick): diff supply/demand across the player's known/owned stations
   per commodity, emit hauler orders greedily. Player sets policy; ships
   route themselves — 4X logistics without a new simulation layer.
4. **A living economy instead of a seeded one.** Keep `GenerateMarket` as
   the *baseline*; make stock/prices state that drifts back toward it, with
   player and NPC trade pushing against it. Then make the ambient traders
   *be* the supply chain: a docking trader delivers goods (stock up, price
   down). Piracy now causes scarcity, trade routes decay as they are
   exploited, and blockades become emergent gameplay — the mechanism (lane
   traffic) already exists; only the dock side-effect is missing. Later:
   per-economy production/consumption, giving owned stations something to
   tax and haulers something to haul. Requires F.
5. **Fog of war over the galaxy.** All 256 systems ship to every client at
   connect, so "explore" is a chart screen. Per-player
   `KnownSystems{bitset}` (persistence-ready component); known by visiting,
   buying charts at high-tech stations (an economy sink), or scout units
   (item 2). The manifest becomes incremental (S2's re-cut), and the
   strategic tier filters by it. AOI already hides *entities*; this extends
   the same idea to *map knowledge*.
6. **Ownership & territory.** Stations gain `Owner`; claiming is a validated
   station transaction (charter purchase / deployed beacon), conferring
   small concrete privileges first (fee share, docking lists). A deployable
   outpost kit is cargo with tonnage semantics, deployed via a command
   message, spawning a structure entity — replicated, persisted, rendered by
   the existing paths (one new `NetType`). Territory then *emerges* as
   influence radii around owned structures; no map-painting system.
7. **Factions — outgrow the five-value team enum.** Keep `Team` for NPC
   *archetype* rules (police discipline, pirate preferences); add a
   server-issued `FactionId` + small standings table for *allegiance*.
   "Protected victim" (§6.3) generalizes to "clean player not at war with
   you"; declared wars suspend wanted consequences between belligerents —
   consensual mass PvP without touching the police system for everyone else.

Deliberately **not** recommended: planetary landings, crafting trees,
player-built capitals, sharded mega-galaxy, voice. None is required by the 4X
loop, and each strains the thin-client / 1200-byte / 30 Hz envelope that keeps
this codebase testable and honest.

### 13.3 Engineering & performance recommendations

**E1 — Wire `Spatial::Grid` into every pairwise loop *before* entity counts
grow.** Confirmed in code: `CollisionSystem.h` gathers all combatants then
runs an `i < j` pair sweep; combat target scans, scooping, ECM radius,
energy-bomb radius, and AOI population are the same shape. Only
`AreaOfInterest` uses the grid today. With caps of 12 NPCs this is invisible;
with §13.2.3's drone fleets every one of these goes quadratic *simultaneously*.
Broadphase: cell size ≥ the largest interaction range (1000 for station
scrape; 16 384 interactions like ECM/bomb query multiple cells), then exact
Chebyshev inside candidate cells. Convert and golden-test one system at a
time now, not under Phase H load-test fire.

**E2 — Kill per-tick allocation churn.** Systems rebuild scratch
`std::vector`s every tick (the collision gather is one of several). Introduce
a per-tick **frame arena** (bump allocator reset each tick) or persistent
per-system scratch buffers with `clear()`-not-free semantics. Same treatment
for snapshot build and message encode buffers — the reliable channels' resend
queues should recycle, not reallocate. This is the difference between a flat
33 ms budget and GC-like latency spikes at fleet scale.

**E3 — ECS: right storage, keep the discipline, add the relational index.**
Sparse-set with swap-and-pop dense arrays (verified in `ECS.h`) is the correct
choice at this scale — do *not* migrate to archetypes; the churn isn't worth
it for ~dozens of component types. Do: (a) keep honoring "rarer pool first"
in `Each<A,B>`; (b) implement `Owner`/`FactionId` lookups as maintained
secondary indexes (hash multimap updated on add/remove), never as component
scans — §12 demands "all my units" be O(mine); (c) when snapshot building
shows up in profiles, split the hot replicated fields (position, basis,
speed, type) into a dedicated pool iterated linearly, so the packetizer
streams from dense memory instead of probing four pools per entity.

**E4 — Re-cut the snapshot for bandwidth (the real scaling wall).**
🟡 **Quantization done (E2a, 2026-07-04); delta pending (E2b).** As built: the
v2 format is **32 B/entity** (was 58). The header carries a full **int64
reference origin** (the viewer's position); positions are **3×i32 offsets** from
it (12 B, exact — no float loss — and the absolute world stays unbounded int64,
since only the small AOI-bounded offset is int32). Orientation is kept as the
**nose+roof basis quantized to i16 components** (12 B; 0/±1 exact) rather than a
smallest-three quaternion — chosen for robustness under blind CI over the ~2 B it
would have saved; speed is **u16 fixed-point** (1/256 unit). Original target and
the still-pending pieces below. Per-session **delta compression** against the
last-acked baseline with a periodic keyframe (the ack plumbing already exists
in the reliable layer; snapshots stay unreliable with baseline acks
piggybacked). Quantization rounds on the *server* (deterministic integer math)
so all clients see identical values — never quantize client-side.

**E5 — SIMD, but determinism first.** The sim's cross-run determinism is a
hard asset (golden tests, future replays). Rules: pin `/fp:strict` on
`GameLogic` and the server; no `-ffast-math`-style contraction; SIMD via
*explicit* intrinsics with fixed evaluation order, never autovectorization of
order-sensitive reductions. Profitable, safe targets: (a) broadphase
Chebyshev rejection — 4-wide `int64` compares (AVX2) over SoA position
arrays; (b) `StepFlight` basis rotation over batched SoA doubles (2-wide SSE2
/ 4-wide AVX) since every ship runs the same integrator; (c) snapshot
quantization/packing. AI think-rate staggering (`(index ^ tick) & 7`) already
amortizes the branchy code — leave it scalar.

**E6 — GPU offload belongs on the client only.** The server must stay
headless-deterministic — no GPU compute in `GameLogic`, ever. Client-side,
the wireframe aesthetic maps perfectly onto cheap GPU work: vertex-shader
line expansion + emissive post chain (§13.2.1), explosion debris as a GPU
particle burst seeded by `EntityDeath` (the legacy `exp_seed` look, computed
in a compute or vertex shader instead of CPU), starfield in a shader.
None of this touches simulation truth.

**E7 — Threading: single-threaded until measured, then phase-parallel.**
Sequence: S5's accumulator first (so overruns are *visible*), then the E1
grid (so the work is smaller), then profile with the BotClient harness. When
the tick budget actually breaks, parallelize the embarrassingly-parallel
phases behind a small job system — AI think, broadphase per cell, snapshot
build per session — with deterministic partitioning and a single ordered
merge point per phase. Never free-thread entity mutation. DB I/O (Phase F)
is async/batched off-thread from day one per §12.

**E8 — Instrument before Phase H, not during.** Minimum counters, cheap
enough to always-on: tick duration histogram + overrun count (from S5),
bytes/session/s per lane, entities per AOI snapshot, broadphase candidate-pair
counts (validates E1), reliable-lane resend rates, and per-phase tick-time
breakdown. These numbers are what Phase H *tunes*; without them the
100-player milestone is guesswork.

---

## 14. Consolidated roadmap

Replaces the retired roadmap's phase list and the prior review's matrix.
Deferred-but-designed gameplay (suns & cabin heat with sun-skimming fuel
scooping; missions after persistence; chat UI) remains in scope as noted in
§6/§13. Effort: S ≤ a day-ish, M = days, L = week(s).

| # | Item | Ref | Type | Effort | Unblocks |
|---|---|---|---|---|---|
| 1 | Persistence (SQL Server) + world-state rows + command log ✅ (done 2026-07-04) | §13.2.2 | Infra | L | everything durable |
| 2 | `ClientHello`-first handshake ✅ (done 2026-07-03) | S1 | Simplify | S | 3, 4 |
| 3 | Session token; endpoint ≠ identity; rate limits ✅ (done 2026-07-04) | §13.2.2 | Infra | S | 4, security |
| 4 | Reconnect grace + resume ✅ (done 2026-07-04) | §13.2.2 | Infra | S | player retention |
| 5 | `PlayerId`/`Owner` identity layer + relational index ✅ (done 2026-07-04; wallet stays ship-borne until F) | §13.2.3-1 | Arch | M | 12–17 |
| 6 | Spatial grid into combat/collision/scoop/ECM loops ✅ (done 2026-07-04; per-event ECM/fire/bomb scans stay linear by design — see IMPLEMENTATION.md D1) | E1 | Perf | M | fleet scale |
| 7 | Frame arena / scratch-buffer reuse ✅ (done 2026-07-04) | E2 | Perf | S | flat tick budget |
| 8 | Accumulator fixed timestep + tick metrics ✅ (done 2026-07-04) | S5, E8 | Simplify | S | honest profiling |
| 9 | Time sync (ping/offset) → lag-compensated fire ✅ (done 2026-07-04; laser rewound, missile/travel cone-rewind deferred; client-reported RTT clamped to a 15-tick window) | §13.2.2 | Infra | M | PvP fairness |
| 10 | Snapshot quantization + delta + budgets ✅ (done 2026-07-04: v2 format 58→32 B/entity with an int64 reference origin so the world stays unbounded; per-session delta vs an acked baseline + keyframes; distance-sorted send budget. Fleet-density delta-fragment reassembly is a noted follow-up) | E4 | Perf | M–L | bandwidth wall |
| 11 | Strategic AOI summary tier | §13.2.2 | Feature | M | empire visibility |
| 12 | First ordered unit (`UnitOrder` escort) | §13.2.3-2 | Feature | M | the Darwinia loop |
| 13 | Batched instanced wireframe + iconic LOD + post chain | §13.2.1 | Render | M | fleet battles, style |
| 14 | Fog of war (`KnownSystems`) + incremental manifest | §13.2.3-5, S2 | Feature | M | explore |
| 15 | Ownership + claimable/deployable outposts | §13.2.3-6 | Feature | L | expand |
| 16 | Drifting markets + traders-as-supply + hauler routing | §13.2.3-3/4 | Feature | L | exploit, emergence |
| 17 | `FactionId` + standings | §13.2.3-7 | Feature | M | diplomacy, mass PvP |
| 18 | Kill-VFX broadcast; missile-lock validation; chat + abuse controls | §13.2.2 | Feature | S–M | MMO polish |
| 19 | Travel protocol split; codec unification; band notes ✅ (done 2026-07-03); math-stack retirement rides item 13 | S2, S3, S6, S7 | Simplify | S | protocol hygiene |
| 20 | BotClient harness → 100-player load test ✅ (harness + CI smoke done 2026-07-04; the 100-bot soak is a manual run of the same binary) | §12 | Test | M | validates 6–10 |
| 21 | Delete client shield-regen fallback ✅ (done 2026-07-03, extended to the whole offline engine) | S4 | Simplify | XS | dogma integrity |

Sequencing spine: **1 → 2/3/4 → 5 → 6/7/8 → 9/10/11 → 12+**, with 13 (render)
and 19/21 (hygiene) parallelizable at any point, and 20 gating any entity-cap
increase.
