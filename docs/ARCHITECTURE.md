# DeepspaceOutpost — Architecture & Game Design

**Status:** as-built, verified against the code 2026-07-08. This is the
**single canonical design document** for the game: the client/server
architecture, the authoritative simulation, the game rules, the network
protocol with every message type, the locked design decisions (§12), the open
architectural work (§13), and the remaining roadmap (§14).

Sections 1–11 describe code that exists and is tested. Three companion
documents exist: **`docs/interaction.md`** (the canonical pointer-first
interaction design), **`input.md`** (repo root — the Homeworld-style
camera/selection migration, H1–H6, now implemented; it supersedes
interaction.md's camera and button-binding grammar) and
**`docs/IMPLEMENTATION.md`** (the track-by-track build log — the *history* of
how each phase landed lives there, not here).

---

## 1. System overview

DeepspaceOutpost is a **server-authoritative multiplayer** remake of the classic
*Elite* gameplay loop — fly, trade, fight, scoop, run from the law, jump between
systems — evolving toward a 4X/RTS-style MMO (§12). One dedicated server owns
the entire game simulation; every client is a **thin presentation layer** that
sends *intent and orders* and renders *replicated state*.

The single load-bearing rule:

> **The server simulates; the client renders. The only things client and server
> share are data schemas — never behaviour.**

Concretely:

- A client cannot set its position, speed, credits, cargo, shields or fuel. It
  sends what it *wants* (unit orders, ability activations, station requests);
  the server validates that intent against ownership, legality, range and the
  ship's performance envelope, and the results come back as replicated
  snapshots and events.
- All gameplay code lives in a headless, OS-free static library (`GameLogic`)
  the client does not link. The client keeps only presentation: rendering,
  interpolation, HUD, audio, input capture.
- Every gameplay rule is unit-tested headlessly (no DX11, no sockets, no
  wall-clock) — ~300 GameLogic tests (see CI for the live count) + the
  NeuronCore/NeuronClient/NeuronServer suites, CI-built on Windows/MSVC
  (x64 debug + release).

### Topology

```
                 UDP :40000
 ┌──────────┐   InputCommand (unreliable, 30 Hz heartbeat + ack)  ┌────────────────────┐
 │  Client   │ ────────────────────────────────────────────────►  │      Server        │
 │ (DX11 UI, │   UnitOrder / AbilityRequest / StationRequest /     │  30 Hz fixed tick  │
 │  render,  │   TravelRequest / Chat / Ping / hellos (reliable)   │  ECS world = truth │
 │  audio)   │ ◄────────────────────────────────────────────────  │  sessions, AOI     │
 └──────────┘   WorldSnapshot delta/keyframe (unreliable, AOI)    └────────────────────┘
                EntityDeath/Despawn, ExplosionAt, UnitOrderAck,
                PlayerInfo/Status, CargoManifest, StationResponse,
                TravelResponse, StrategicSummary, GalaxyChunk, Chat
                (reliable lanes)
```

Clients connect through the `ClientHello` handshake (§4.6); nothing is spawned
for an endpoint until a valid, version-checked hello arrives.

---

## 2. Project layout

| Directory | Role | Links against |
|---|---|---|
| `NeuronCore/` | Header-only shared **engine + protocol**: ECS, int64 math, message system, serialization, reliability, snapshot schema (quantization/delta/budget), station protocol, galaxy chunks, spatial grid, ownership index. **Data and mechanism only — no game rules.** | — |
| `GameLogic/` | **Server-only** authoritative simulation: flight, combat, AI, orders, escorts, economy, stations, loot, collisions, cabin heat, hyperspace, sessions, spawning, AOI, chat moderation. Headless. | NeuronCore |
| `NeuronServer/` | Server-side engine services: the persistence service (single writer thread + pluggable stores), datagram pump, accumulator tick pacer + tick metrics, on-change send caches. | NeuronCore |
| `Server/` | The dedicated host: UDP socket loop, fixed tick, session I/O, world bootstrap (procedural galaxy or durable DB rows), ODBC store, CSPRNG tokens. | GameLogic, NeuronServer, NeuronCore |
| `NeuronClient/` | Client-side engine: DX11 device (`GraphicsCore`), 3D scene pass (`Scene3D` + instancing + `SceneGlow`), native 2D (`Render2D` + `TextRenderer` + the `GuiWindow`/overlay framework), camera + controllers, input cores (gestures, unified pointer front door, selection + band, move gizmo + movement grid, order menu), replication client (socket + interpolation), sound. | NeuronCore |
| `DeepspaceOutpost/` | The game client: flight HUD (`RenderGameHud`), native GUI windows (charts/market/station), pointer-first order input, camera rig, HUD mirrors of replicated state. | NeuronClient, NeuronCore |
| `BotClient/` | Headless bot client over the real net stack (load testing; CI smoke). | NeuronCore |
| `Tests/GameLogic/`, `Tests/NeuronCore/`, `Tests/NeuronClient/`, `Tests/NeuronServer/` | GoogleTest suites (headless). | respective libs |
| `GameData/Models/` | Ship meshes (JSON), converted from the legacy tables. | — |
| `tools/dbseed` | Standalone tool that generates the durable galaxy rows + baseline markets into SQL Server. | — |

Dependency direction is strictly downward: the client never includes
`GameLogic`, the server never includes render/UI code, and `NeuronCore` includes
nothing above it.

### Coding conventions that matter architecturally

- **DirectXMath** (`XMVECTOR`/`XMMATRIX`, `NeuronCore/GameMath.h`) is the
  standard math for client/render code; the legacy `LegacyVector*`/`Matrix33`
  wrappers are **fully retired** — no source file references them (do not
  reintroduce).
- The **authoritative simulation** uses `Neuron::Math::Vector3i64` (absolute
  world position) and `Vector3d` (orientation/velocity) — double precision by
  design, for cross-run determinism the golden tests rely on.
- **No wall-clock randomness anywhere in GameLogic.** Every random draw comes
  from a caller-owned, seeded LCG stream (`x = x*1664525 + 1013904223`); the
  server keeps separate streams for loot, AI and hyperspace so the sequences
  cannot perturb each other. Deterministic in, deterministic out.
- **All HLSL is compiled offline, never at runtime.** Each `shaders/*.hlsl`
  is compiled by fxc into a `shaders/CompiledShaders/<name>.h` byte array at
  build time (`NeuronClient/CMakeLists.txt`), and the renderers create their
  shaders from those arrays. There is no `D3DCompile` path and no
  `d3dcompiler` dependency.

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
- `OwnershipIndex` (NeuronCore) is the maintained relational index mapping a
  `playerId` to every entity it owns — "all my units" is O(mine), never a
  component scan.

### 3.2 World space & floating origin

- The world is an **absolute `int64³` space**. Positions never wrap; systems
  are scattered across ±100M units. Distance gates use **Chebyshev** ranges
  (no multiplies on absolute coordinates → no overflow), computing true/squared
  distances only over small in-range deltas.
- The client renders **camera-relative** (floating origin): every frame it
  rebases replicated `int64` positions around the viewer before touching float
  math, so precision never degrades far from origin.

### 3.3 Fixed tick

The server advances the world on a fixed 30 Hz (33 ms) timestep driven by an
**accumulator pacer** (`NeuronServer/TickPacer.h`): when behind it runs up to
`TICK_MAX_CATCHUP = 5` catch-up ticks (dropping the backlog beyond that and
counting the overrun in the tick metrics), when ahead it sleeps the remainder.
All rates are expressed in ticks. The client renders at display rate and
interpolates between snapshots.

---

## 4. Networking & protocol

### 4.1 Transport stack

Everything rides **UDP on port 40000**. Each datagram is routed by its leading
magic:

| Magic | Stream | Contents |
|---|---|---|
| `'NSNP'` | **Snapshot** (unreliable, server→client) | AOI world-state deltas/keyframes; superseded by the next one, never retransmitted. |
| `'NRLB'` | **Reliable lanes** (both directions) | `[magic][lane u8][token u64]` + one `ReliableChannel` packet (its own `'NEVT'` seq/ack framing inside). |
| `'NMSG'` | **Message packet** (unreliable lane) | `[magic][version][lane][token u64]` + the framed record stream — today this carries `InputCommand` client→server. |

**Reliable lanes** (`Msg::MessageEndpoint`): one `ReliableChannel` per lane —
`Control(0)`, `Gameplay(1)`, `Bulk(2)` — each with its own sequence/ack space,
so a large cold Bulk payload (the galaxy chart) can never head-of-line-block
a gameplay death or the session handshake. Receive drains Control → Gameplay →
Bulk. Idle lanes are silent. `ReliableChannel` is TCP-like at message level
(ordered, deduplicated, resent until acked) but stays on UDP.

**Framing** (`Messages/Framing.h`): an `'NMSG'` packet is
`magic u32 | PROTOCOL_VERSION u16 (=3) | lane u8 | token u64` followed by zero
or more records, each `MessageId u16 | length u16 | payload`. The mandatory
per-record length bounds every decoder to exactly its own bytes — a malformed
message cannot run the reader into the next record.

**Session token:** both client→server framings (`'NMSG'` and `'NRLB'`)
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
snapshot into datagrams holding only whole entities; the galaxy chart ships
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
  uint32_t playerId; uint32_t entityId; std::string name; int32_t wantedLevel;
  auto Fields() { return std::tie(playerId, entityId, name, wantedLevel); }
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
ids stay (permanent ABI). Gameplay/combat/VFX events allocate from the
game-specific band (`0x1000+`).

### 4.4 Wire message catalog

#### Session & identity

**`ClientHello`** — `0x0002` · Control scope · Command · Control lane · C→S.
The opening handshake and the **front door**: the server spawns nothing until a
valid, version-checked hello arrives. Carries the protocol version + the
commander name the player chose. The server sanitizes (printable ASCII,
≤ 20 chars, trailing spaces trimmed) and de-duplicates (`-2`, `-3`, …) before
adopting it. Fields: `protocolVersion u32`, `commanderName string`.

**`HelloAck`** — `0x0003` · Control scope · Event · Control lane · S→C.
The handshake was accepted: "you are player P, controlling entity N." Sent
once, on the first valid `ClientHello` (and re-queued on a resume). The client
stamps the token on every subsequent datagram; the server authenticates by it.

| Field | Type | Meaning |
|---|---|---|
| `sessionToken` | u64 | the session's identity — a CSPRNG token stamped on every later client datagram |
| `playerId` | u32 | the player identity (§12 "Account → Empire → owns N entities"); stable across reconnects |
| `entityId` | u32 | the PRIMARY entity this player controls |
| `protocolVersion` | u16 | the server's `PROTOCOL_VERSION` (echo) |

**`HelloReject`** — `0x0004` · Control scope · Event · Control lane · S→C.
The handshake was refused and no session was provisioned. Fields:
`reason u8` (`HelloRejectReason`, `ProtocolMismatch = 1`).

**`Ping`** / **`Pong`** — `0x0006` / `0x0007` · Control · Control lane.
~1 Hz time sync: `Ping{clientTimeMs u32, rttMs u32}` C→S,
`Pong{clientTimeMs u32, serverTick u32}` S→C. Feeds the smoothed RTT the
lag-compensated fire path uses (§6.2).

**`PlayerInfo`** — `0x0301` · Wire · Event · Gameplay · S→C.
One roster entry, broadcast to everyone on join, name change, or wanted-level
change (crime, decay, death, hyperspace cooling). The roster is keyed by
**player**, not hull. Fields: `playerId u32` (roster key), `entityId u32`
(PRIMARY ship, nameplate anchor), `name string`, `wantedLevel i32`.

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
| `laserTemp` | i32 | laser temperature; ≥ 242 locks the trigger |
| `cabinTemp` | i32 | cabin heat from sun proximity (0–255; at max the hull cooks) |

**`CargoManifest`** — `0x0303` · Wire · Event · Gameplay · S→C (owner only).
The full per-commodity hold (`units vector<i32>`, index 0..16), resent whenever
it changes outside a trade (a scoop, or a respawn emptying it).

**`Chat`** — `0x0300` · Wire · Event · Gameplay · Both. A chat line. The
client sends `{sender ignored, text}`; the server rate-limits and sanitizes it
(§6.14) and rebroadcasts with `sender` stamped to the **authenticated
`playerId`** so clients can mute by identity. Fields: `sender u32 (playerId;
0 = server/system line)`, `text string`.

#### Input

**`InputCommand`** — `0x0100` · Wire · Command · **Unreliable** lane · C→S.
The per-frame heartbeat. Self-superseding: the server keeps the highest
`sequence` and drops stale datagrams. A static trait forbids queuing it on a
reliable lane. With the pointer-first client the flight axes are always sent
as **zero** (movement is a `UnitOrder`); the ability flags are still the live
activation path (see `AbilityRequest` below), and `ackSnapshotTick` carries
the delta-stream ack.

| Field | Type | Meaning |
|---|---|---|
| `sequence` | u32 | monotonic; latest wins |
| `rollAxis` / `pitchAxis` / `throttle` | f32 | legacy flight axes — always 0 from the pointer-first client; the server still clamps and applies them |
| `fire` | bool | fire the front laser this frame (unused by the client; attack is an order) |
| `fireMissile` | bool | launch a missile this frame |
| `missileTarget` | u32 | locked target index, or `0xFFFFFFFF` (none) — validated server-side (range + cone) |
| `ecm` | bool | fire the ECM burst |
| `energyBomb` | bool | detonate the energy bomb |
| `escapePod` | bool | eject in the escape pod |
| `ackSnapshotTick` | u32 | latest snapshot baseline the client holds (delta ack) |

#### Orders & abilities

**`UnitOrder`** — `0x1010` · Wire · Command · Gameplay · C→S. The
pointer-first movement/command verb: "unit U, do X (at/to T)". Validated
server-side (`PlanUnitOrder`): ownership through the `OwnershipIndex`,
docked state, target type legality, Move-distance clamp; ordering an attack
on a protected victim is a crime attributed to the owner at order time.

| Field | Type | Meaning |
|---|---|---|
| `unitId` | u32 | the ordered unit (must be owned by the sender) |
| `order` | u8 | `OrderKind`: `Stop=1`, `Move=2`, `Approach=3`, `Dock=4`, `Attack=5`, `Collect=6`, `Escort=7` (`Patrol=8`, `Route=9` reserved) |
| `target` | u32 | target entity (Attack/Dock/Collect/Approach/Escort) |
| `targetX/Y/Z` | i64 ×3 | world point (Move) |

**`UnitOrderAck`** — `0x1011` · Wire · Event · Gameplay · S→C. The verdict:
`{unitId u32, order u8, status u8}` with `OrderStatus`: `Accepted=0`,
`NotYours=1`, `BadTarget=2`, `Illegal=3`, `OutOfRange=4`, `Docked=5`,
`Rejected=6`.

**`AbilityRequest`** — `0x1014` · Wire · Command · Gameplay · C→S. One-shot
equipment activation on a reliable lane: `{kind u8, target u32}` with
`AbilityKind`: `FireMissile=1`, `Ecm=2`, `EnergyBomb=3`, `EscapePod=4`.
The server handler is live and tested, but **the client still activates
abilities through the `InputCommand` flags** — unifying onto this message and
retiring the unreliable flags is an open protocol item (§14).

#### Lifecycle & VFX

**`EntityDespawn`** — `0x0200` · Wire · Event · Gameplay · S→C. An entity left
the world (reaped player, expired canister, docked trader, fled NPC…) — the
thing absence can't convey on the snapshot stream. Client drops it from view
and clears any missile lock on it. Fields: `entityId u32`.

**`EntityDeath`** — `0x0201` · Wire · Event · Gameplay · S→C. A kill
(`victim u32, killer u32`). For the victim's own session this triggers the
death/respawn sequence; for everyone else it plays the explosion and removes
the wreck. (The dying *player's* death is sent only to that session — the ship
respawns immediately, so others never see it flicker out; bystanders get
`ExplosionAt` instead.)

**`ExplosionAt`** — `0x1005` · Wire · Event · Gameplay · S→C (broadcast).
A world-anchored kill VFX: `{x/y/z i64, scale u8}`. Broadcast on player kills
so the killer sees the explosion even though the victim entity respawns
elsewhere the same tick.

**`EcmPulse`** — `0x0202` · Wire · Event · Gameplay · S→C (broadcast). A ship's
ECM burst fired (`source u32`) — a player's activation or an NPC's automatic
defence. Plays the classic buzz; the downed missiles arrive as `EntityDeath`
events alongside.

**`EscapePodUsed`** — `0x0203` · Wire · Event · Gameplay · S→C (owner only).
Your pod ejected (`entityId u32`): the ship is lost (cargo gone, record
cleared, tank refilled) and you are already respawned docked. The client flips
to the docked flow.

#### Station & economy

**`StationRequest`** — `0x0400` · Wire · Command · Gameplay · C→S.
Fields: `kind u8`, `commodity u16` (commodity index for Buy/Sell, `EquipItem`
for Equip), `quantity u16`, `stationId u32` (unused; a travel-era residue).

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
`EscapePod=6` (1000 Cr), `EscortFighter=7` (5000 Cr, max 4 escorts — spawns an
owned escort, §6.13). Ownership is authoritative.

**`StationResponse`** — `0x0401` · Wire · Event · Gameplay · S→C.
Fields: `kind u8` (echo), `status u8`, `credits i32` (resulting wallet),
`commodity u16` (echo), `cargo u16` (resulting held quantity).

#### Travel

**`TravelRequest`** — `0x1000` · Wire · Command · Gameplay · C→S. Take me
somewhere; the server validates fuel/range/mass-lock through
`HyperspaceSystem` (§6.8). Fields: `kind u8` (`Hyperspace=1`,
`InSystemJump=2`), `systemId u32` (destination; ignored by InSystemJump).

**`TravelResponse`** — `0x1001` · Wire · Event · Gameplay · S→C. The outcome;
position/fuel changes ride the snapshot stream and `PlayerStatus`. Fields:
`kind u8` (echo), `status u8` (`Arrived=0`, `Witchspace=1`, `Jumped=2`,
`NotEnoughFuel=3`, `OutOfRange=4`, `UnknownSystem=5`, `MassLocked=6`,
`Rejected=7`).

#### Strategic

**`StrategicSummary`** — `0x1004` · Wire · Event · Gameplay · S→C. The
low-rate strategic tier (§12 decoupled clocks): per viewer, every 30 ticks
(~1 Hz), an aggregate of the viewer's current system (radius 8M units):
`{systemId u32, friendlyCount u16, hostileCount u16, alert u8}`.

#### Bulk

**`GalaxyChunkRequest`** — `0x1002` · Wire · Command · Bulk · C→S. The client
**pulls** the galaxy chart in bounded ranges (`baseIndex u32`, `count u16`,
server-clamped to 64) instead of receiving a connect-time fire-hose.

**`GalaxyChunk`** — `0x1003` · Wire · Event · Bulk · S→C. One slice:
`total u32 | baseIndex u32 | systems vector<entry>` through the generic codec
(entries are nested records: `id u32 | x,y,z i64 | name string | government u8
| economy u8 | techLevel u8 | population u16 | productivity u16`), at most 16
entries per message so each fits a safe datagram. An out-of-range request is
answered with an empty chunk still carrying `total`. The client requests the
next range as each completes, until it holds all `total` systems; the chart
renders progressively meanwhile.

#### Snapshot stream (not a catalog message)

`'NSNP'` datagrams, unreliable, per-viewer. A compact quantized encoding
(32 bytes/entity). **Keyframe** header (41 bytes):
`magic u32 | version u16 (=2) | tick u32 | viewerId u32 | refX,Y,Z i64 ×3 |
complete u8 | count u16`, then `count ×` `EntitySnapshot` (32 bytes):

| Field | Type | Meaning |
|---|---|---|
| `id` | u32 | entity index |
| `x, y, z` | **i32 ×3** | position as an **offset from the header's `ref` origin** — see below |
| `noseX..Z` | **i16 ×3** | forward direction, quantized (×32767; 0/±1 exact) |
| `roofX..Z` | **i16 ×3** | up direction (side = nose × roof), quantized |
| `speed` | **u16** | units/tick along nose, 1/256-unit fixed-point |
| `type` | i16 | renderable ship type (legacy `SHIP_*`; see §6.10) |

**Reference origin (keeps the world unbounded int64).** The header carries a
full `int64` reference (`ref`, set to the viewer's position); each entity's
position is a compact `int32` *offset* from it. The absolute world stays
**unbounded int64** — only the offset is int32, and every entity in a snapshot
is within the viewer's area of interest, so the offset always fits int32
however large the galaxy grows. Position is **exact** (integer subtraction, no
float loss); the offset saturates rather than wraps if an out-of-AOI entity is
ever handed in. Orientation/speed quantization is deterministic integer math
(identical on every client — the "server-side rounding" the delta stage relies
on); the decoded in-memory `EntitySnapshot` is unchanged (int64 pos + float
basis), so the interpolator and render path are untouched. Codecs live in
`NeuronCore/Quantization.h`.

The `complete` flag marks a whole-tick snapshot; only a complete snapshot may
become a delta baseline (a packetizer split repeats the header per datagram
but only the whole set is `complete`).

Snapshots are **area-of-interest filtered** per viewer: entities within ±1 cell
of a 100 000-unit grid around the viewer, **plus** the local system's
landmarks (planet + station + sun) out to 2 000 000 units so celestial bodies
never pop out mid-approach. Packetized to whole entities ≤ 1200 bytes (the
reference origin is repeated in each split datagram so each decodes
independently). Later snapshots supersede earlier ones; loss is never
repaired, only outrun.

**Delta stream.** Rather than re-sending every visible entity each tick, the
server sends a small **delta** against the snapshot the client last
**acknowledged** — only changed entities (new/moved) and removed ids — plus an
occasional full **keyframe** (forced every 30 ticks, or when no acked baseline
is held, or for a crowded multi-datagram AOI). A delta shares the `'NSNP'`
magic with a distinct version (3) and a 46-byte header naming its
`baselineTick` plus changed/removed counts. The client **acks** the latest
baseline tick by piggybacking it on `InputCommand` (`ackSnapshotTick`). Both
sides keep a bounded ring of recent snapshots (48), so a lost delta self-heals
(the server keeps deltaing against the still-acked older baseline) and
reordering is safe (a delta resolves its baseline by tick). Change detection
compares **absolute** positions (stable under the moving reference origin) at
their **quantized** resolution. Codecs: `SnapshotDelta.h` (diff/apply) and
`SnapshotStream.h` (encoder/decoder).

**Send budget.** Each viewer's per-tick state is capped
(`SNAPSHOT_SEND_BUDGET_BYTES = 4800`): an overloaded AOI keeps the entities
**closest** to the viewer and sheds the farthest, sorted by distance then id so
the trim is identical on every client. Applied before delta-encoding; the shed
count feeds the `[metrics]` line (`dropped=`). See `SnapshotBudget.h`.

### 4.5 Server-internal messages (never on the wire)

The server's own combat pipeline is decoupled through an in-process
`MessageBus` with `LocalOnly` messages (`GameLogic/CombatMessages.h`; ids in
the non-wire half, not `REGISTER_MESSAGE`'d — they never serialize):

| Message | Id | Meaning |
|---|---|---|
| `FireWeapon{shooter, weapon, target}` | `0x8101` | a fire request (from `InputCommand` flags or an Attack order); resolved against the world |
| `Crime{offender, victimTeam, firstOffence}` | `0x8102` | a protected victim was fired on; police dispatch on first offence |
| `EntityKilled{victim, killer}` | `0x8103` | something died; ONE subscriber decides what a death does |
| `EcmFired{ship}` | `0x8104` | an ECM burst fired → the server broadcasts `EcmPulse` |
| `PodEjected{ship}` | `0x8105` | a pod ejected → owner notify + roster/cargo refresh |
| `ActionTriggered{action, param}` | `0x8200` | **client**-local: raw input → command-builder bridge (this one *is* a registered catalog message, `Dir=None`) |

### 4.6 Canonical sequences

**Connect:** client sends `ClientHello{version, name}` (Control, token 0) →
server version-checks it (mismatch ⇒ `HelloReject`, no session), else — after
the commander's durable state loads, if persistence is on — spawns the player
entity, sanitizes/dedupes the name, mints a CSPRNG session token, and replies
`HelloAck{token, playerId, entityId, version}` (Control) → the client stamps
that token on every subsequent datagram → server broadcasts the full
`PlayerInfo` roster → snapshots + `PlayerStatus`/`CargoManifest` begin flowing.
Input from an endpoint that has not completed this handshake — or that carries
the wrong/no token — is ignored. Once connected the client **pulls** the galaxy
chart in bounded ranges (`GalaxyChunkRequest` → `GalaxyChunk`, Bulk) until it
holds all systems.

**Order → kill → respawn:** RMB on an enemy → `UnitOrder{Attack}` → server
validates (`PlanUnitOrder`; a protected victim ⇒ `Crime` at order time) and
acks → `StepOrders` steers the ship and publishes `FireWeapon` when aligned →
`ResolveFireWeapon` resolves the laser **lag-compensated** (targets rewound
through the 15-tick transform-history ring at `now − RTT/2 − interpDelay`) and
applies damage, may publish `Crime` (wanted +1, police launch from the nearest
station with a warrant) and/or `EntityKilled` → the death subscriber pays the
killer (`CreditKill`), scatters loot (`DropLoot` / `DropPlayerCargo`),
broadcasts `ExplosionAt`, and for a player victim: sends that session
`EntityDeath`, restores hull/shields, clears wanted, sweeps every NPC `focus`
off them, respawns them docked at the nearest station, broadcasts the
refreshed `PlayerInfo`, resends `CargoManifest` (now empty). NPC victims are
destroyed and broadcast to everyone.

**Hyperspace:** chart click → HYPERSPACE button → `TravelRequest{Hyperspace,
systemId}` → server runs `Hyperspace()` → `TravelResponse{status}` (+ a
`PlayerInfo` broadcast if the wanted level cooled) → the new position rides the
next snapshot; fuel rides `PlayerStatus`.

---

## 5. The authoritative server

### 5.1 Tick pipeline

Every 33 ms tick (`GameServer::RunTick`), in this order:

1. **Drain the socket.** Route datagrams by magic: `InputCommand` →
   `ServerSessions::OnInput` (applied only to a live, handshaken session — an
   unknown endpoint is ignored; stale-sequence drop; intent applied to
   `FlightIntent`; ability flags become `FireWeapon` bus messages) · reliable
   datagrams → `ServerSessions::OnReliable` (an unknown endpoint gets a
   pending, entity-less shell so its `ClientHello` can be received) →
   per-session `MessageEndpoint`.
2. **Dispatch the bus** — fire commands resolve to `Crime`/`EntityKilled` facts.
3. **Reliable requests** — per session: `ClientHello` (spawn / resume /
   reject), `StationRequest` (commerce via `ProcessStationRequest`; an
   `Equip{EscortFighter}` routes to the escort purchase), `TravelRequest`,
   `UnitOrder` (validate + plan), `AbilityRequest`, `GalaxyChunkRequest`,
   `Ping`, `Chat` (rate-limit + sanitize + broadcast). Gameplay requests are
   gated on the session being live. Roster rebroadcast on membership change.
4. **Apply completed persistence loads** — a hello that was deferred on a DB
   load finishes spawning here (`ApplyCompletedLoads`).
5. **Advance simulation:** `SafeParkSilent` (zero intent of silent sessions) →
   `StepOrders` (active orders steer their units by writing `FlightIntent`;
   Attack orders publish `FireWeapon` when aligned) → `StepAi` (NPC tactics,
   §6.7) → `GameLogic::Tick` (`StepLaunchCruise` → `StepFlightInput` →
   `StepFlight` → `StepMotion`; the launch-cruise step drives a freshly
   undocked hull out of the station bay, §6.9)
   → `CompleteDockOrders` (a Dock order in range docks the unit) →
   `SpawnDirector::Step` (pirates near players, every 600 ticks, NPC cap 12)
   → `StepTraders` (lane traffic, every 900 ticks, cap 2) → shield regen
   every 8 ticks → `StepEquipment` (laser cooling, ECM recharge).
6. **Capture transform history** — the 15-tick ring lag-compensated fire
   rewinds against.
7. **Combat resolution** — `StepMissiles` (homing + detonation) + `StepCombat`
   (NPC auto-fire) + `StepCollisions` (§6.9) + `StepCabinHeat` (sun proximity,
   §6.9) → all kills published as `EntityKilled`; bus dispatched (deaths
   resolve; double-reports are guarded).
8. **Loot** — `StepLoot` (age canisters) + `ScoopSystem` (players vacuum or
   smash canisters; changed holds get a `CargoManifest`).
9. **Wanted decay** — every 600 ticks each record cools 1 level; changed
   players get a roster refresh.
10. **Reap** idle sessions, diff live entities → `EntityDespawn` broadcasts.
11. **Send** — per session: AOI snapshot (+ landmarks), budget-trimmed then
    delta/keyframe-encoded against the client's acked baseline; on-change
    `PlayerStatus`; `StrategicSummary` every 30 ticks; then flush all
    reliable lanes.
12. **Persist** — player snapshots every 150 ticks (change-gated), market
    write-back every 900 ticks (change-gated); tick metrics summary.

Pairwise systems (combat scans, collisions, scooping, missiles, AI target
scans) run through the shared spatial **broadphase** (`Broadphase.h` over
`NeuronCore/SpatialGrid.h`) with per-tick scratch buffers (`FrameScratch.h`)
— no per-tick allocation churn on the hot path.

### 5.2 Sessions (`ServerSessions`)

- Keyed by the client's **current endpoint** (`addr<<16 | port`), but the
  IDENTITY is the **session token**: a second index maps token → endpoint.
  A valid, version-checked `ClientHello` (`OnHello`) spawns the player entity
  (see §5.3), mints a CSPRNG token, and queues `HelloAck`; a token-less
  reliable datagram from an unknown endpoint first gets a pending, entity-less
  **shell** (`OnReliable`) so that hello can be received. Every later client
  datagram is authenticated by token *before* it touches a session
  (`Authenticate`); a correct token from a new address re-binds the session
  there (NAT rebind heals). Input (`OnInput`) applies only to a live,
  correctly-tokened session. `Session::Live()` (entity valid) distinguishes a
  connected player from a pending shell; pending shells are excluded from the
  roster and reaped on the idle timeout, and their token index is pruned.
- Token-less (pre-handshake) datagrams are **rate-limited** per endpoint
  (`RATE_MAX_UNAUTH` per `RATE_WINDOW_TICKS`, muted `RATE_MUTE_TICKS` on
  breach); authenticated input is capped at `MAX_INPUTS_PER_TICK = 8`.
- **Reconnect grace:** a pending shell reaps after `SESSION_TIMEOUT_TICKS`
  (300 ≈ 10 s), but an authenticated session survives `SESSION_GRACE_TICKS`
  (1800 ≈ 60 s) of silence so it can reconnect. `SafeParkSilent` zeros the
  flight intent of a live session silent past `SESSION_PARK_TICKS` (45 ≈ 1.5 s)
  so a disconnected ship stops rather than flies away. A hello on a live
  session is a **resume** (`HelloResult::Resumed`): keep the entity + token,
  re-queue `HelloAck`, re-send roster/cargo/status.
- Latest-sequence-wins input application.
- Owns the commander-name pipeline: sanitize → cap (20) → de-dupe → stored as
  the authoritative per-player record on the session → roster broadcast.
- Owns the per-player **identity + records**: each session gets a `playerId`,
  the `OwnershipIndex` maps it to every entity it owns (primary ship +
  escorts), and the name/score records live here — off the hull. Chat
  rate-limit state (`ChatLimiter`) is per session.
- `Broadcast(msg)` queues a catalog message to every session's proper lane.

### 5.3 A player entity (as spawned)

`WorldTransform`, `Flight`, `FlightIntent`, `FlightCaps` (0.121 rad/tick roll &
pitch, 60 u/t max speed), `Wallet` (1000 = 100.0 Cr), `CargoHold` (20 t),
`DockState`, `Equipment` (3 missiles), `Fuel` (70/70 tenths), `PlayerTag`,
`Combatant` (Team Player, 255 energy, laser 10, range 6000, autoEngage
**false**, 150 ticks spawn grace), `Shields` (255/255), `Wanted` (0),
`Owner` (the session's playerId), `ShipGear` (laser heat + ECM cooldown),
`CabinHeat`, `NetType` (Sidewinder — the stock starter hull). The commander name and score are
**not** components: they are per-player session records. (`Wallet`
deliberately stays ship-borne until multiple hulls trade concurrently.)

---

## 6. Game design (the rules, as implemented)

All quantities in server ticks (~30/s), world units, tenths of a credit and
tenths of a light year, matching the legacy fixed-point scales.

### 6.1 Flight

The intent model: an order (or an NPC brain) writes normalized axes into
`FlightIntent`; `ResolveIntent` clamps to the hull's `FlightCaps` — **nothing
can out-turn or out-run its ship** — then the integrator applies roll/pitch to
the ship's orthonormal basis (side/roof/nose) and advances the position along
the nose with a sub-unit `carry` remainder, a faithful port of the legacy
`rotate_vec` model inverted to ship-carries-its-frame. Player ships, escorts
and NPCs all fly through the *same* pipeline — *everything flies by intent*;
only the intent's author differs (an `ActiveOrder`, the AI, or legacy axes).

### 6.2 Combat

- **Teams:** Player=0, Pirate=1, Police=2, Station=3, Trader=4. Same team never
  auto-fights; player primaries (autoEngage=false) fire only on command but may
  target **other players** — that is how PvP exists at all. Player-owned
  escorts auto-engage in defence of their owner.
- **Player laser:** an Attack order (or a legacy `fire` flag) resolves through
  `ResolvePlayerFire`: nearest enemy within 6000 units inside a cos ≥ 0.9
  (~25°) aiming cone; damage is the ship's laser strength. The shot is
  **lag-compensated**: the target is rewound through a 15-tick transform
  history ring at `now − RTT/2 − interpDelay` (RTT from Ping/Pong, clamped to
  the ring) — favour-the-shooter.
- **NPC lasers:** `StepCombat` — each auto-engaging combatant fires at most
  once per `fireInterval` (10 ticks), damage accumulates and resolves
  simultaneously (fire order can't matter).
- **Target memory:** `Combatant.focus` — a locked, live, in-range enemy
  outranks the nearest-enemy scan for **both** fire and AI flight, so an NPC
  shoots what its pilot is flying against. Police warrants (§6.3) live here.
- **Shields (players):** a hit lands on the shield **facing the attacker**
  (sign of the attacker-direction · nose); overflow drains the energy bank
  (`ApplyDamageToShields`, legacy `damage_ship`). NPCs have no shields — damage
  hits energy directly. Regen every 8 ticks: while the bank is over half, one
  point bleeds into each non-full shield, then the bank recovers one (capped
  255) — legacy `regenerate_shields`.
- **Missiles:** real homing entities — speed 180 u/t, life 240 ticks, detonate
  within 400 units for 250 damage (through the same directional-shield path).
  Launched by players (missile ability + lock; the lock is **validated
  server-side** with the same range + cone gate as the laser) and by hurting
  NPCs (panic launch). Missiles carry no `Combatant`, so they can't be
  targeted, pay no bounty, never mass-lock, and never collide.
- **Spawn grace:** 150 ticks of damage immunity on spawn and respawn; shots
  pass through, collisions don't bite, and the countdown is owned by
  `StepCombat` alone.

### 6.3 Crime & the law

- Firing on a **protected** victim — Station, Police, Trader, or a **clean**
  player (wanted 0) — raises the shooter's `Wanted` by 1 and publishes `Crime`.
  Ordering a unit to attack a protected victim is the same crime, attributed to
  the **owner** at order time. A wanted player is *fair game*: attacking them
  is legal.
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
cargo hold as canisters at the wreck → broadcast `ExplosionAt` at the wreck →
restore energy/shields to max, grant respawn grace, zero the wanted record →
**respawn docked at the nearest station anywhere in the world** (hold emptied,
capacity/equipment/credits/score kept) → sweep NPC grudges → refresh the
roster → resend the (empty) `CargoManifest`. Only the dying session receives
`EntityDeath` for the player, so nobody else sees the respawned ship blink.
NPC deaths broadcast `EntityDeath` to everyone, pay bounties, drop loot, and
destroy the entity.

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
  triggers a `CargoManifest` resend (+ pickup sound client-side). A Collect
  order steers the ship onto the canister and the same rules apply.

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
| Max speed | 60 | 60 | 30 | 60 |
| Bounty | 50 | — (crime) | — (crime) | 100 |
| Spawned by | every 600 ticks near a player (cap 12 NPCs) | Crime (2, station launch, warrant) | every 900 ticks on a lane (cap 2) | witchspace misjump (1–4) |

NPCs are **speed-matched to the player's 60-unit cap** — the legacy
Viper-vs-Cobra speed edge is retired in favour of a manageable pace; the
chase pressure comes from collision avoidance + numbers instead.

Behaviour building blocks (constants in `AiSystem.h`):

- **Collision avoidance:** each `StepAi` call builds a shared obstacle grid
  (`BuildAvoidGrid`: every hull, plus planets/suns) and a thinking NPC first
  checks its forward path (`AvoidObstacles`, 6000-unit lookahead): anything
  demanding a berth — a station (2500), a planet/sun (6000), or a
  **same-team** hull (1200) — pulls the nose away for that think.
  Enemy/prey hulls and the current combat `focus` are exempt, so attack runs
  still press home. Deterministic: no RNG, integer positions, fixed
  tie-break.
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

### 6.8 Travel

- **Fuel:** 0–70 tenths (7.0 LY), full at spawn. Scale:
  `UNITS_PER_TENTH_LY = 500 000` ⇒ a full tank spans ~35M units, calibrated
  against the real galaxy (mean nearest-neighbour ≈ 14.5M) so refuelling gates
  onward travel without stranding anyone.
- **Hyperspace** (works docked or in flight): cost = Euclidean distance / 500k,
  floored at 1 tenth. Rejections: `OutOfRange` (beyond a full tank),
  `NotEnoughFuel` (beyond the current tank), `UnknownSystem`. On success: fuel
  deducted, wanted halved, undocked, relocated to the destination station +
  2000 units — **arriving in flight**, fly in and dock.
- **Witchspace:** rand255 > 253 (~0.8 %) per jump — fuel still spent, the ship
  is flung 20M units off the destination into deep space, marked `Witchspace`,
  and ambushed by 1–4 Thargoids. **Kills made in witchspace pay no bounty**
  (score still counts). A later clean jump clears the marker.
- **In-system jump** (the legacy `jump_warp`): shoves the ship up to 200k units
  toward the nearest planet, stopping 6000 clear of the kill radius — **unless
  mass-locked** by any other hull (ship or station) or a planet within 75k
  units. Canisters/missiles never mass-lock (no `Combatant`), matching the
  legacy cargo/rock exemption.

### 6.9 Collisions & environment

Per tick (`StepCollisions`), Chebyshev ranges, damage repeats while contact
holds (grinding, the legacy near-fatal feel):

| Contact | Range | Effect |
|---|---|---|
| ship ↔ ship | 600 (scoop-calibrated) | 100/tick to **both**, routed through the facing-shield path; a ram kill credits the surviving hull (bounty/loot flow normally) |
| ship ↔ station | 1000 | 200/tick to the **ship only** — the fortress doesn't notice; the safe way in is the dock request (range 5000) |
| ship ↔ planet | 4000 | instant death (legacy zero-altitude), killer = the planet |

Exempt: docked ships (inside the station, not in space), spawn/respawn grace
(without consuming it), and anything without a `Combatant`. Police and trader
launches (offset 2000) are born clear of every contact range, and trader lanes
end at a **gate** 6000 above the planet, outside the kill zone.

**Undock is a fly-out, not a teleport** (`LaunchSystem.h`): undocking places
the hull *at* the station bay facing outward and attaches a transient
`LaunchCruise` component; `StepLaunchCruise` (the first step of
`GameLogic::Tick`) forces a straight, level 0.35-throttle cruise until the
ship has travelled `LAUNCH_OFFSET` (2000) units, then releases control to the
player. The launch starts inside the station contact range, so the undock
grants launch immunity (`invulnTicks = RESPAWN_GRACE_TICKS`, covering the
~95-tick exit) — the station doesn't grind the ship and the vulnerable
low-speed launch is protected. Hyperspace arrival still teleports to the
destination station + 2000 units (§6.8).

**Suns & cabin heat** (`StepCabinHeat`): every system has a sun entity
(offset 300 000 above the planet). Inside its 60 000-unit Chebyshev heat band
a ship's `CabinHeat` rises +6/tick (cools −3/tick outside); at 255 the hull
takes 4 energy/tick **bypassing shields** until it cooks (a heat death is
self-credited — no bounty exploit). The payoff: a ship fitted with a **fuel
scoop** skimming the band gains +1 fuel tenth/tick — the classic sun-skimming
refuel, dangerous by construction. `PlayerStatus.cabinTemp` mirrors the dial
to the HUD.

### 6.10 World generation

- **No special home system.** The universe is a uniform field of systems with
  no privileged origin. New commanders are placed **docked at a system chosen
  from their name** (`GameLogic::DockAtNameChosenSystem`: a SplitMix64 of the
  name picks a system, deterministic and varied per player, no wall-clock RNG
  per §8), so players scatter across the galaxy; a returning commander wakes
  wherever they last were (persisted `lastSystemId`; −1 falls back to the
  nearest station). Dynamic spawning (`SpawnDirector`) provides pirates near
  players.
- **Procedural galaxy:** 256 systems scattered over ±100M units; each system
  gets a planet entity, a station entity (orbit +8000 x) with a market, a sun
  entity, and legacy-style name/attributes. Shipped to clients as pulled
  chunks. **Locations are loaded, not just seeded:** when the DB is seeded,
  the server lays the universe out from the durable `systems` rows (stable
  ids + positions), so the map survives generator/config changes and
  `station_markets` can key against it with a real foreign key. The seed
  (`0xC0FFEE`) is retained only to *generate* those rows once (via
  `tools/dbseed`) and as the fallback when there is no DB — an
  unseeded/persistence-off server regenerates the identical galaxy in memory.
  The one place that maps generator ↔ rows ↔ manifest is `Server/GalaxyRows.h`.
- **Replicated ship types** (`NetType.type` = legacy `SHIP_*`): Sun −2,
  Planet −1, Missile 1, Coriolis 2, Alloy 4, Cargo 5, Rock 8, Shuttle 9,
  Transporter 10, Viper 16, Sidewinder 17 (the player's stock starter hull),
  Thargoid 29. The client maps them through
  `RenderTable.h` onto meshes/billboards/glyphs; 0 draws a default ship.

### 6.11 Scoring & bounties

Bounty sources: explicit `Bounty` component (pirate 50 = 5.0 Cr, Thargoid 100)
or wanted-derived for fugitive players (20/level). `CreditKill`
(`KillRewards.h`) pays the killer's wallet in place and returns the earned
`KillCredit{bounty, score}`; the server routes the score to the killer's
session record — only a player killer with a wallet earns; missiles' own
detonations credit their owner; witchspace withholds the money but not the
score.

### 6.12 Equipment

The purchased items work, all server-validated (`EquipmentSystem`); one-shot
activations arrive as `InputCommand` flags (or `AbilityRequest`) and resolve
through `FireWeapon`:

- **ECM** (activation, 32 energy, 32-tick recharge): downs EVERY in-flight
  missile within 12 000 units — anyone's, including your own (the legacy burst
  was indiscriminate). NPCs get the legacy *automatic* defence instead: each
  tick a missile homing on an ECM-fitted target has a 16/256 chance of being
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

### 6.13 Orders & escorts

The indirect-control layer (§12): the player **selects** a unit and **orders**
it; a server-side `OrderSystem` executes the order by writing `FlightIntent`
through the same steering the NPC autopilot uses — no new movement math, and
the anti-cheat boundary is unchanged (the client still cannot move an inch).

- **Orders** (`UnitOrder` → `PlanUnitOrder` → `ActiveOrder` component →
  `StepOrders`): `Stop`, `Move` (to a world point, distance-clamped),
  `Approach`, `Dock` (completes via `CompleteDockOrders` in dock range),
  `Attack` (sets `focus`; fires with NPC fire discipline when aligned),
  `Collect` (steer onto a canister; scoop rules apply), `Escort` (follow a
  friendly). Validation: ownership (`OwnershipIndex`), docked state, target
  type, legality — ordering an attack on a protected victim raises `Wanted`
  at order time.
- **Escorts** (`EscortSpawn.h`): `Equip{EscortFighter}` while docked
  (5000 Cr, cap 4 per player) spawns a Viper-hulled, Team-Player,
  auto-engaging fighter owned by the buyer (`GrantOwnership`), born with a
  default `ActiveOrder{Escort → owner}`. It flies, fights and dies through
  the existing AI/combat/replication paths; it can be selected and re-ordered
  like the primary ship. (Escort *persistence* across server restarts is an
  open item — §14.)

### 6.14 Chat

`Chat` is live end-to-end. Server side (`HandleChat` + `ChatModeration.h`):
per-session rate limit (6 lines per 300-tick window — a breach gets a
"chatting too fast" system line), sanitize (control bytes stripped, capped at
200 chars), then broadcast with `sender` stamped to the **authenticated
playerId** (never client-supplied). Client side: an 8-line scrollback over the
HUD, Enter to open/submit the input line, and a client-side `/mute <name>`
list keyed by playerId. (Server-persisted mute and AOI-scoped delivery are
open items — §14.)

---

## 7. Client presentation layer

The client is deliberately dumb. It keeps:

- **Rendering:** DX11, low-poly meshes, camera-relative floating origin.
  Replicated entities are drawn from interpolated snapshots as WORLD-frame
  records (`ReplicatedScene`), rebased about the camera's floating origin;
  `Scene3D` composes each model with the Camera's `View()` and `Projection()`
  matrices (DirectXMath, left-handed) and the hardware z-buffer resolves
  visibility. Ships render through a single **solid** GPU mesh path
  (`draw_solid_ship` → `Scene3D::SubmitModel`); the planet is one lit green 3D
  sphere; the sun a billboard. The retro-vector art direction is realized by
  the low-poly meshes. Batched solid-mesh instancing (`DrawIndexedInstanced`
  per hull type) and an emissive glow post pass (`SceneGlow`) are built and
  toggleable in the Options window — both **default off** pending an in-app
  visual pass (§14). `RenderTable.h` maps `NetType` → mesh/billboard/glyph +
  palette row (`RenderFor`), and `ShouldDrawAsGlyph` draws far contacts as
  symbology instead of meshes (currently a contact blip; the vector glyph set
  and the grid-backed cull are open — §14).
- **Interpolation:** `SnapshotInterpolator` samples between the two freshest
  snapshots with the interpolation delay; the alpha is clamped, so the client
  **never extrapolates** past the newest server state (no dead-reckoning —
  a deliberate honesty trade; prediction is future work, §14).
- **The free camera: the camera is decoupled from the ship.** The player
  flies the CAMERA; the active ship renders on screen like any other entity.
  `NeuronClient/Camera` is the one view/projection source (eye/lookAt/up →
  `View()`; the legacy ~41.1° vertical field of view at the live aspect →
  `Projection()`), with two `CameraController`s toggled on F12. Since the
  Homeworld-style input migration (`input.md` H1–H6) the default is the
  **focus-point orbit** camera: it orbits a persistent focus point,
  **RMB-drag rotates**, wheel/pinch and **MMB-drag (vertical)** zoom, and the
  **LMB+RMB chord**, a **two-finger touch drag**, or the arrow/WASD keys
  **pan** the focus in the screen plane; **F / double-tap** eases the focus
  onto the entity under the cursor and follows it (`FocusOn`/
  `FocusSettled`). F12 switches to the **first-person free-fly** observer
  (RMB-drag looks; arrow keys move, PgUp/PgDn vertical, Shift boosts). The
  game-side `CameraRig` gathers input through the unified pointer front door
  (`input_win.*` — one gesture stream for mouse and touch, with MMB and
  chord state), anchors the camera behind the ship on spawn and re-anchors
  after teleports (hyperspace/respawn), and publishes the int64 **floating
  origin** (the eye) the frame is rebased around — float precision never
  degrades far from the world origin (§3.2). The remaining CPU-projected
  effects (explosion debris, firing beams, the lock reticle, the dust)
  derive their pixel math from the SAME projection matrix, so there is one
  optics path. (Server rules are untouched: shields still resolve front/aft
  by attack direction on the hull.)
- **Input — pointer-first order control** (`docs/interaction.md`; button
  bindings as rebuilt by the Homeworld migration, `input.md` H1–H6). The
  player does not fly the hull: they **select** a unit and **order** it, and
  the server executes. The pointer grammar, as built: **LMB click** = select
  the entity under the cursor (screen-ray pick); **LMB drag** = **band
  (marquee) select** — the rubber-band rectangle selects every owned unit
  inside it on release; selection is a multi-unit *set*
  (`NeuronClient/input/Selection.h`, headless-tested) decoupled from the
  missile-lock target, which stays a single derived output; **RMB click** =
  the contextual default order for the target under the cursor — planet/sun
  → Approach, station → Dock, canister → Collect, ship → Attack, empty
  space → Move to the cursor-ray ∩ camera-up-plane point (with a
  Homeworld-style move gizmo, vertical drag for elevation) — sent as a
  reliable `UnitOrder`, acked by `UnitOrderAck` (order toast + info card
  give feedback); **RMB drag** = camera rotate (§ camera above); **RMB
  hold** = a radial menu with the full legal order set (attacking a *clean*
  player is deliberately menu-only friction); **M** = the persistent
  **movement grid** (`NeuronClient/input/MovePlan.h`, a two-step
  point-then-elevation state machine with Shift as the elevation modifier).
  A non-modal **ability bar** puts Stop/Missile/ECM/Bomb/Pod/Jump one click
  away (Bomb/Pod hold-to-confirm ~0.6 s); a **nav strip** opens
  charts/market/status/equip. **Touch** is co-primary: a `WM_POINTER`
  gesture recognizer (tap/double-tap/long-press/drag/pan/pinch) maps one
  finger to camera rotate, **two fingers to pan** (consumed by the rig),
  pinch to zoom and double-tap to focus. Ability presses publish
  `ActionTriggered` (LocalOnly bus) and currently ride the `InputCommand`
  flags (§4.4). The per-frame `InputCommand` continues as the heartbeat that
  carries the delta-stream ack, with **zero flight axes**. The keyboard is
  optional accelerators only — F1–F12, F (focus), M (grid), Esc
  (window-close), and the camera pan/fly keys; the legacy combat keys are
  retired. *(Caveat: the H1–H6 pure cores are unit-tested, but the
  client-glue has not yet had its Windows/MSVC build + manual input pass —
  see `input.md`.)*
- **Native GUI windows** (`GuiWindow` over `Render2D`): the **charts**
  (`ChartWindow` — click a system to select, drag to pan, wheel/pinch to
  zoom, its own HYPERSPACE button → `TravelRequest`; F5/F6 open
  galactic/short-range), the market/equip/commander screens, and the
  **docked hub**: docking shows the camera-space 3D scene (your ship at the
  station) with a small `StationMenuWindow` floating over it (Launch +
  Market/Equip/Commander/Inventory/Options). Launch / F1 / hyperspace drop
  you straight into flight. Every screen is a native window or the
  full-window scene — there is **no letterbox**: the fixed 512×514 retro
  canvas and its centering/scaling path are gone, and the 2D and 3D fill the
  client window 1:1.
- **Native 2D stack:** all client 2D draws through
  `Neuron::Graphics::Render2D`. Per frame: `RenderScene` runs the game's
  world draw (models → `Scene3D::SubmitModel`, then the depth-tested scene
  pass renders over the dust); `RenderCanvas` then opens the native HUD pass
  **`RenderGameHud`** (`HudRender.cpp`) — deferred scene overlays (debris
  pixels, the target reticle, the intro title), the self-gated flight
  dashboard (scanner console, dials, compass, missiles, the selection/order
  overlays, the ability bar and nav strip, the chat scrollback), and the
  centred overlay text — and the GUI overlay (windows) renders on top. Text
  everywhere is the shared bitmap-font sheet via `TextRenderer`
  (`g_gameFont`) with its shader outline. What survives of the legacy 2D
  layer is a thin engine seam — `platform/GameScene.h/.cpp` (the 3D scene
  pass + live scene/viewport size and projection; the `gfx_*` seam functions
  live here) and `GamePalette.h` (the `GFX_COL_*` palette indices + `IMG_*`
  sprite ids).
- **HUD mirrors:** shields/energy/fuel/credits/missiles/cargo/wanted/score/
  laser & cabin temperature from `PlayerStatus` + `CargoManifest`; the roster
  (`PlayerInfo`) for ship labels and chat names; the market/chart from
  `StationResponse`/the pulled galaxy chunks; the strategic rollup from
  `StrategicSummary`. The scanner/compass mirror is **camera-relative** (what's
  around the view); the docking-proximity gate stays **ship-relative** (it is
  about the hull). There is **no offline simulation**: a disconnected client
  shows a connection-lost banner and retries (~every 2 s).
- **Presentation effects:** death/explosion VFX (world-anchored, from
  `EntityDeath`/`ExplosionAt`, re-using the legacy debris animation), a
  star-warp flourish on jump, sounds (launch, hits, ECM, hyperspace, scoop
  beep). The scene background is a two-layer **starfield** (`stars.md`,
  shipped): the streaming **dust field** — the classic Elite speed cue,
  driven by the CAMERA's motion (`set_starfield_motion`) — now rendered as
  textured soft sprites (`Starburst.dds`, additive blending) with a
  power-law magnitude distribution, distance-falloff brightness, spectral
  colour and a gentle twinkle; behind it a dense, window-filling **parallax
  backdrop** (`draw_backdrop`) that pans with the look direction but never
  dollies. Both draw depth-disabled behind the ships. There is no skybox
  cubemap.
- **No local config files.** The MMO client keeps no on-disk settings; HUD
  layout values are baked in (`elite.cpp`). The in-session Options window
  (incl. the instancing/glow toggles) applies for the running session only;
  nothing persists client-side. Durable player state is the server's job.

A `TravelResponse{Hyperspace, Arrived|Witchspace}` flips the client from the
station screen into flight; position updates always come from snapshots.

---

## 8. Determinism & testing

- **Everything gameplay is headless-testable**: no sockets, no GPU, no clock,
  no global RNG. ~300 GameLogic tests + the NeuronCore protocol suites run in
  CI (Windows, MSVC, x64 debug + release) and locally.
- **Parity tests** pin the wire ABI: golden byte layouts for the folded legacy
  codecs, round-trips for every catalog message, and registry governance
  (unique ids, scope/id-band consistency, wire direction present).
- **Behavioural tests** run real mini-simulations: AI steering convergence
  through the actual flight integrator from five orientations, flee-escape-
  despawn cycles, seeded witchspace misjumps, collision exemptions, trader
  lane runs, order execution, escort purchase/defence, cabin-heat
  scooping/cooking — same seed, same world ⇒ bit-identical positions.
- The engine **forbids wall-clock randomness**; any new system takes an
  explicit seeded stream. This is what makes golden-run tests possible and is
  a hard prerequisite for the future replay/reconciliation work.
- The **BotClient** harness drives real sessions over the real net stack
  (CI smoke; the 100-bot soak is a manual run of the same binary).

---

## 9. Security & anti-cheat posture

- The client is untrusted by construction: it holds no authoritative state and
  every `Command` is validated (docked checks, stock/credit checks, hold
  space, fuel gates, range gates, aim cones, per-hull intent clamps, order
  ownership/legality, missile-lock range+cone).
- Wire hygiene: per-record length bounds; string/vector caps (4096); truncated
  or foreign buffers fail decode safely; wrong-direction messages are
  rejectable by trait; LocalOnly ids cannot be serialized at compile time.
- Entity references from the wire are bare indices resolved through
  `LiveEntity` (generation check) before use.
- Names are sanitized (printable ASCII, length-capped, de-duplicated)
  server-side; chat is rate-limited and sanitized, and the sender identity is
  server-stamped.
- Datagrams are authenticated by the session token: the server keys sessions
  by a CSPRNG token, not by source address, and drops a wrong/no-token client
  datagram before decoding it. Token-less (pre-handshake) datagrams are
  rate-limited per endpoint; authenticated input is capped per tick.
- Not yet addressed (future, §14): encryption (tokens travel in cleartext — a
  same-path attacker can read them; TLS/DTLS or a challenge exchange), and
  finer input *cadence* sanity beyond the per-tick cap.

---

## 10. Key constants (quick reference)

| Constant | Value | Where |
|---|---|---|
| Tick rate | 30 Hz (33 ms accumulator, max 5 catch-up ticks) | ServerConfig.h, TickPacer.h |
| Server port | 40000 UDP | ServerConfig.h |
| Safe datagram payload | 1200 B | Replication.h |
| Snapshot send budget | 4800 B (4 datagrams) | SnapshotBudget.h |
| Snapshot keyframe interval / baseline ring | 30 ticks / 48 | SnapshotStream.h |
| AOI cell / radius | 100 000 / ±1 cell | ServerConfig.h |
| Landmark visibility | 2 000 000 | ServerConfig.h |
| Session timeout / grace / park | 300 / 1800 / 45 ticks | ServerConfig.h |
| Dock range | 5000 | ServerConfig.h |
| Player laser range / cone | 6000 / cos 0.9 | ServerConfig.h |
| Lag-compensation history | 15 ticks | TransformHistory.h |
| Launch offset / undock fly-out distance | 2000 (cruise at 0.35 throttle) | StationServices.h, LaunchSystem.h |
| Max shields / energy | 255 / 255 | CombatSystem.h |
| Shield regen cadence | 8 ticks | ServerConfig.h |
| Respawn grace | 150 ticks | CombatSystem.h |
| Wanted decay | 1 level / 600 ticks | ServerConfig.h |
| Fugitive threshold | 8 | CombatSystem.h |
| Bounties: pirate / thargoid / per-wanted-level | 50 / 100 / 20 | KillRewards.h, CombatSystem.h |
| Missile speed / life / damage / detonate | 180 / 240 / 250 / 400 | MissileSystem.h |
| Loot life / scoop range | 3600 ticks / 600 | LootSystem.h |
| Ship / station contact, planet kill | 600 / 1000 / 4000 | CollisionSystem.h |
| Ram / station-scrape damage | 100 / 200 per tick | CollisionSystem.h |
| Sun heat band / heat gain / cool / cook damage | 60 000 / +6 / −3 / 4 per tick | CabinHeatSystem.h |
| Sun-scoop fuel gain | +1 tenth per tick in band | CabinHeatSystem.h |
| NPC turn cap / engage range | 7/152 rad/tick / 16 384 | AiSystem.h |
| NPC / trader / player max speed | 60 / 30 / 60 | AiSystem.h, FlightInput.h |
| AI avoid lookahead / ship / station / planet berth | 6000 / 1200 / 2500 / 6000 | AiSystem.h |
| Pirate spawn cadence / NPC cap | 600 ticks / 12 | ServerConfig.h, SpawnDirector.h |
| Trader cadence / cap / dock range | 900 ticks / 2 / 1500 | SpawnDirector.h, AiSystem.h |
| Fuel max / price / units-per-tenth-LY | 70 / 2 / 500 000 | StationServices.h, HyperspaceSystem.h |
| ECM cost / recharge / range | 32 energy / 32 ticks / 12 000 | EquipmentSystem.h |
| Missile auto-jam chance | 16/256 per tick | MissileSystem.h |
| Energy bomb radius | 16 384 | EquipmentSystem.h |
| Laser heat per shot / lock / cool | +8 / 242 / −1 per tick | EquipmentSystem.h |
| Escort price / cap per player | 5000.0 Cr / 4 | StationServices.h |
| Witchspace odds / displacement | >253 of 256 (~0.8 %) / 20M | HyperspaceSystem.h |
| Mass-lock / in-system hop | 75 000 / ≤200 000 | HyperspaceSystem.h |
| Galaxy systems / extent / station orbit | 256 / ±100M / 8000 | GalaxyGen.h |
| Strategic cadence / radius | 30 ticks / 8 000 000 | GameServer, StrategicView.h |
| Chat rate / length cap | 6 per 300 ticks / 200 chars | ChatModeration.h |
| Persist cadence: players / markets | 150 / 900 ticks (change-gated) | GameServer |
| Commander name cap | 20 chars | ServerSessions.h |
| String / vector wire caps | 4096 / 4096 | Serialize.h |

---

## 11. Message id inventory (complete)

| Id | Message | Scope | Lane | Dir |
|---|---|---|---|---|
| `0x0001` | AssignPlayer *(RETIRED — id reserved)* | Control | Control | S→C |
| `0x0002` | ClientHello | Control | Control | C→S |
| `0x0003` | HelloAck | Control | Control | S→C |
| `0x0004` | HelloReject | Control | Control | S→C |
| `0x0005` | *reserved* (the never-shipped `AssignControl`) | — | — | — |
| `0x0006` | Ping | Control | Control | C→S |
| `0x0007` | Pong | Control | Control | S→C |
| `0x0100` | InputCommand | Wire | Unreliable | C→S |
| `0x0200` | EntityDespawn | Wire | Gameplay | S→C |
| `0x0201` | EntityDeath | Wire | Gameplay | S→C |
| `0x0202` | EcmPulse | Wire | Gameplay | S→C |
| `0x0203` | EscapePodUsed | Wire | Gameplay | S→C (owner) |
| `0x0210` | *retired* (hand-encoded galaxy manifest → `GalaxyChunk 0x1003`) | — | — | — |
| `0x0300` | Chat | Wire | Gameplay | Both |
| `0x0301` | PlayerInfo | Wire | Gameplay | S→C |
| `0x0302` | PlayerStatus | Wire | Gameplay | S→C (owner) |
| `0x0303` | CargoManifest | Wire | Gameplay | S→C (owner) |
| `0x0400` | StationRequest | Wire | Gameplay | C→S |
| `0x0401` | StationResponse | Wire | Gameplay | S→C |
| `0x1000` | TravelRequest | Wire | Gameplay | C→S |
| `0x1001` | TravelResponse | Wire | Gameplay | S→C |
| `0x1002` | GalaxyChunkRequest | Wire | Bulk | C→S |
| `0x1003` | GalaxyChunk | Wire | Bulk | S→C |
| `0x1004` | StrategicSummary | Wire | Gameplay | S→C |
| `0x1005` | ExplosionAt | Wire | Gameplay | S→C |
| `0x1010` | UnitOrder | Wire | Gameplay | C→S |
| `0x1011` | UnitOrderAck | Wire | Gameplay | S→C |
| `0x1014` | AbilityRequest | Wire | Gameplay | C→S |
| `0x8101–0x8105` | FireWeapon / Crime / EntityKilled / EcmFired / PodEjected — server-internal bus, never serialized (§4.5) | LocalOnly | — | — |
| `0x8200` | ActionTriggered (client-local) | LocalOnly | — | None |

Plus the non-catalog streams: `'NSNP'` snapshots (§4.4) and the
`'NRLB'`/`'NEVT'` reliability framing (§4.1).

---

## 12. Locked design decisions & trajectory

These are **owner-locked**; everything in §13–§14 honors them.

| Topic | Decision |
|---|---|
| Platform | Windows client **and** Windows server (MSVC, DX11) |
| Authority | Server-authoritative; clients send intent, render replicated state |
| Trajectory | Gameplay evolves from space-flight toward a **4X / RTS-style MMO** (many units per player, empire/economy/territory, less twitch) — as an *extension*, never a rewrite |
| World | One **seamless** absolute `int64³` space, no visible segments; an invisible cell partition underneath for interest management and future multi-process sharding |
| Identity | **Account → Empire/Faction → owns N entities.** A player is *not* bound to one avatar; camera & interest are view-driven |
| Input | Command/intent protocol (validated orders with costs/preconditions) — **unit orders**; the anti-cheat boundary |
| Interaction | **Pointer-first indirect control** (`docs/interaction.md`, camera/selection grammar as rebuilt by `input.md`): mouse and touch are the primary devices, one shared pointer grammar (select + band-select → order, context menus, move gizmo + movement grid, ability bar) over a Homeworld-style focus-orbit camera; attack is an order the server executes; the keyboard is optional accelerators only — nothing is keyboard-exclusive |
| Streaming | Multi-resolution AOI: a high-detail **tactical** tier + a low-detail **strategic** tier (territory/fleet summaries) |
| Transport | Raw winsock UDP + the custom reliability layer; hand-rolled binary hot path |
| Persistence | Microsoft SQL Server; async batched writes off the sim thread; the world simulates while players are offline; **never** per-tick positions to SQL |
| Entity model | In-house sparse-set ECS in NeuronCore; indexed spatially **and** relationally (owner/faction/group/tag) |
| Sim cadence | **Decoupled clocks**: sim tick, command intake, and replication are separate rates; tactical replicates faster than strategic |
| Logic boundary | `GameLogic` is server-only; the client shares **data schemas only**, never behavior |
| Scale model | **Replication, not lockstep** — determinism kept for replays/tests; 100-player scale via interest-managed state replication |
| Test harness | Headless `BotClient` over the real net stack for the 100-player load milestone |
| Aesthetic | The faithful **low-poly / retro-vector** look is the art direction, not a placeholder — rendering work amplifies it, never replaces it |

**Standing invariant (persistence-readiness):** every durable-in-spirit piece
of state lives in a plain serializable component (`Wallet`, `CargoHold`,
`Fuel`, `Wanted`, `Equipment`, `CabinHeat`, …) or a plain per-player session
record (name/score), so persistence serializes state without refactoring
gameplay.

**Track status (2026-07-08):** connection/persistence, identity,
performance/harness and netcode-depth tracks are **done** (handshake-first
connect, session tokens, reconnect/resume, SQL persistence + durable galaxy,
playerId/ownership, spatial broadphase, frame scratch, accumulator timestep,
lag-compensated fire, quantized delta snapshots with budgets, strategic tier).
The pointer-first interaction track is **done** except small residues, and the
Homeworld-style camera/selection migration (`input.md` H1–H6: focus-orbit
default, band select, movement grid, MMB/chord/two-finger pan) has **landed**
(pending its Windows manual input pass); polish items G1–G4 (kill VFX,
missile-lock validation, chat, suns/cabin heat) are **done**; the first
ordered unit (escort) is **done**; the legacy math-stack retirement is
**done** (no `LegacyVector*`/`Matrix33` left in source); the starfield visual
pass (`stars.md`) is **done**. Open: the 4X feature tier (fog of war,
territory, living economy, factions), missions, the render residues, and the
engineering items in §13–§14. The full build history lives in
`docs/IMPLEMENTATION.md`.

---

## 13. Open architectural work

What remains from the standing architectural review, current as of
2026-07-05. (Everything the review previously recommended that has since
shipped — handshake inversion, codec unification, travel split, offline-engine
deletion, accumulator timestep, session tokens, reconnect, persistence, lag
compensation, snapshot quantization/delta/budget, strategic tier, identity
layer, spatial broadphase, frame arena, order-based interaction — is described
as-built in §3–§7 and logged in `IMPLEMENTATION.md`.)

### 13.1 Protocol & simplification

- **Unify ability activation onto `AbilityRequest`.** Both activation paths
  are live today: the client sets the unreliable `InputCommand` flags, while
  the reliable `AbilityRequest 0x1014` handler sits ready server-side. Move
  the client onto `AbilityRequest`, then re-cut `InputCommand` to the pure
  heartbeat/ack `{sequence, ackSnapshotTick}` it already is in spirit (the
  axes are always zero). One activation path, one less unreliable-loss edge
  case on one-shot items (bomb/pod).
- **Two math stacks, not three (S6) — DONE.** DirectXMath for presentation,
  `Vector3i64`/`Vector3d` for simulation. The third stack is gone: the
  `LegacyVector*`/`Matrix33` wrappers have been fully retired — no source
  file references them (verified 2026-07-08). Do not reintroduce them (the
  `explosion.md` donor code names them; the port maps every use onto
  DirectXMath).
- **What is *not* over-engineered — do not "simplify" these.** The five-trait
  message catalog, the three reliable lanes, the sparse-set ECS, the separate
  seeded LCG streams, Chebyshev gating on `int64` coordinates, and
  intent-based flight for players *and* NPCs are all proportionate mechanism:
  each buys a compile-time guarantee, a head-of-line-blocking fix, or the
  determinism the test strategy depends on. The snapshot stream staying
  outside the catalog codec is also correct — it is a packed hot path with its
  own packetizer.

### 13.2 The 4X feature tier (designed, not built)

The Darwinia-style **indirect control** seam is in place (orders, escorts,
ownership); these build on it. All require nothing new architecturally — they
reuse orders, the ownership index, persistence, and the trader autopilot.

1. **Fog of war over the galaxy.** All 256 systems ship to every client
   today, so "explore" is a chart screen. Per-player `KnownSystems{bitset}`
   (persistence-ready component); known by visiting, buying charts at
   high-tech stations (an economy sink), or scout units. The chunk pull
   becomes incremental and the strategic tier filters by it. AOI already
   hides *entities*; this extends the same idea to *map knowledge*.
2. **Ownership & territory.** Stations gain `Owner`; claiming is a validated
   station transaction (charter purchase / deployed beacon), conferring small
   concrete privileges first (fee share, docking lists). A deployable outpost
   kit is cargo with tonnage semantics, deployed via a command message,
   spawning a structure entity — replicated, persisted, rendered by the
   existing paths (one new `NetType`). Territory then *emerges* as influence
   radii around owned structures; no map-painting system.
3. **A living economy.** Keep `GenerateMarket` as the *baseline*; make
   stock/prices state that drifts back toward it, with player and NPC trade
   pushing against it. Make the ambient traders *be* the supply chain: a
   docking trader delivers goods (stock up, price down). Piracy causes
   scarcity, routes decay as exploited, blockades become emergent gameplay.
   Owned haulers reuse the trader autopilot with a `Route{stationIds[]}`
   order (the enum value is already reserved); above that, a per-empire flow
   pass at strategic cadence emits hauler orders greedily. The
   `station_markets` persistence rows already exist as the durable substrate.
4. **Factions — outgrow the five-value team enum.** Keep `Team` for NPC
   *archetype* rules (police discipline, pirate preferences); add a
   server-issued `FactionId` + small standings table for *allegiance*.
   "Protected victim" (§6.3) generalizes to "clean player not at war with
   you"; declared wars suspend wanted consequences between belligerents —
   consensual mass PvP without touching the police system for everyone else.

Deliberately **not** planned: planetary landings, crafting trees,
player-built capitals, sharded mega-galaxy, voice. None is required by the 4X
loop, and each strains the thin-client / 1200-byte / 30 Hz envelope that keeps
this codebase testable and honest.

### 13.3 Engineering & performance

- **E5 — SIMD, but determinism first.** The sim's cross-run determinism is a
  hard asset (golden tests, future replays). Rules: pin `/fp:strict` on
  `GameLogic` and the server; no fast-math contraction; SIMD via *explicit*
  intrinsics with fixed evaluation order, never autovectorization of
  order-sensitive reductions. Profitable, safe targets: broadphase Chebyshev
  rejection (4-wide int64 compares over SoA), `StepFlight` basis rotation
  (batched SoA doubles), snapshot quantization/packing. AI think staggering
  already amortizes the branchy code — leave it scalar.
- **E7 — Threading: single-threaded until measured, then phase-parallel.**
  One thread runs everything, and that is *correct today* — do not
  parallelize ahead of profiling with the BotClient harness. When the tick
  budget actually breaks, parallelize the embarrassingly-parallel phases
  behind a small job system — AI think, broadphase per cell, snapshot build
  per session — with deterministic partitioning and a single ordered merge
  point per phase. Never free-thread entity mutation. The invisible cell
  partition is also the future shard boundary; keep systems cell-local (no
  system should casually scan the whole world when the grid can answer).
- **ECS storage discipline (E3 residue).** Sparse-set is right at this scale
  — do not migrate to archetypes. Keep "rarer pool first" in `Each<A,B>`;
  keep `Owner` lookups on the maintained `OwnershipIndex`, never component
  scans; if snapshot building shows up in profiles, split the hot replicated
  fields into a dedicated pool iterated linearly.
- **Netcode tails:** client-side **prediction/reconciliation** (the client
  today never extrapolates — honest but laggy at high RTT); delta-fragment
  reassembly for a *persistently* multi-datagram (extreme fleet-density) AOI,
  which currently falls back to full snapshots; missile/travel cone-rewind
  (only the laser is lag-compensated).
- **Security tails:** encryption (tokens travel in cleartext; DTLS or a
  challenge exchange), server-persisted chat mute + AOI-scoped chat delivery.

---

## 14. Roadmap (remaining work)

Completed items are logged in `docs/IMPLEMENTATION.md` §14; this table is only
what's open. Effort: S ≤ a day-ish, M = days, L = week(s).

| # | Item | Ref | Type | Effort | Unblocks |
|---|---|---|---|---|---|
| 1 | Fog of war (`KnownSystems`) + incremental chunk filtering | §13.2-1 | Feature | M | explore |
| 2 | Ownership + claimable/deployable outposts | §13.2-2 | Feature | L | expand |
| 3 | Drifting markets + traders-as-supply + hauler `Route` orders | §13.2-3 | Feature | L | exploit, emergence |
| 4 | `FactionId` + standings | §13.2-4 | Feature | M | diplomacy, mass PvP |
| 5 | Missions (after the 4X tier settles) | §12 | Feature | L | quests, direction |
| 6 | Ability path unification (`AbilityRequest`) + `InputCommand` re-cut | §13.1 | Simplify | S | protocol hygiene |
| 7 | Render residues: in-app visual pass → instancing/glow default-on; vector glyph set + grid-backed client cull; GPU explosion debris (plan: `explosion.md`) | §7 | Render | M | fleet battles, style |
| 8 | Interaction residues: widget drag-scroll/steppers, chart pointer name-search *(two-finger camera pan shipped with the `input.md` migration)* | §7 | UX | S | touch polish |
| 9 | Escort persistence + respawn re-target | §6.13 | Feature | S–M | durable fleets |
| 10 | Client-side prediction / reconciliation | §13.3 | Netcode | L | high-RTT feel |
| 11 | Delta-fragment reassembly for fleet-density AOI | §13.3 | Netcode | M | extreme density |
| 12 | Encryption (DTLS / challenge exchange) | §9 | Security | M | hostile networks |
| 13 | Server-persisted chat mute + AOI-scoped chat | §6.14 | Social | S | abuse controls |
| 14 | ~~Math-stack retirement~~ **done 2026-07-08** — no `LegacyVector*`/`Matrix33` left in source | §13.1 | Simplify | — | codebase hygiene |
| 15 | 100-bot manual soak (BotClient) before any entity-cap increase | §12 | Test | M | validates scale |
| 16 | SIMD hot paths under `/fp:strict` discipline | §13.3 | Perf | M | fleet scale |
| 17 | Phase-parallel tick (only after profiling shows the budget breaking) | §13.3 | Perf | L | entity counts |

Sequencing spine: **1 → 2 → 3 → 4 → 5** for the 4X tier, with 6–9 (hygiene +
polish) parallelizable at any point, 15 gating any entity-cap increase, and
16–17 strictly profile-driven.
