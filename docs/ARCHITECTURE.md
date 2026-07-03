# DeepspaceOutpost — Architecture & Game Design

**Status:** as-built (Phase G complete through G7), 2026-07-03.
This is the **canonical design document** for the game: the client/server
architecture, the authoritative simulation, the complete game rules, and the
network protocol with every message type. Companion documents:

- [`MIGRATION_ROADMAP.md`](MIGRATION_ROADMAP.md) — the phased migration history
  and per-phase status (how we got here).
- [`gameplay.md`](gameplay.md) — the Phase G (multiplayer gameplay) work-package
  plan this implementation followed.

A short [Future outlook](#12-future-outlook) at the end covers designed-but-unbuilt
work; everything else in this document describes code that exists and is tested.

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
  wall-clock) — 205 GameLogic tests + the NeuronCore protocol suites at the
  time of writing, CI-built on Windows/MSVC (x64 debug + release).

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
| `Tests/GameLogic/`, `Tests/NeuronCore/` | GoogleTest suites (headless). | respective libs |
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
| `'NRLB'` | **Reliable lanes** (both directions) | `[magic][lane u8]` + one `ReliableChannel` packet (`'NEVT'` seq/ack framing inside). |
| `'NMSG'` | **Message packet** (unreliable lane) | The framed record stream — today this carries `InputCommand` client→server. |

**Reliable lanes** (`Msg::MessageEndpoint`): one `ReliableChannel` per lane —
`Control(0)`, `Gameplay(1)`, `Bulk(2)` — each with its own sequence/ack space,
so a large cold Bulk payload (the galaxy manifest) can never head-of-line-block
a gameplay death or the session handshake. Receive drains Control → Gameplay →
Bulk. Idle lanes are silent. `ReliableChannel` is TCP-like at message level
(ordered, deduplicated, resent until acked) but stays on UDP.

**Framing** (`Messages/Framing.h`): an `'NMSG'` packet is
`magic u32 | PROTOCOL_VERSION u16 (=1) | lane u8` followed by zero or more
records, each `MessageId u16 | length u16 | payload`. The mandatory per-record
length bounds every decoder to exactly its own bytes — a malformed message
cannot run the reader into the next record.

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

### 4.4 Wire message catalog

#### Session & identity

**`AssignPlayer`** — `0x0001` · Control scope · Event · Control lane · S→C.
The connect handshake reply: "you control entity N."

| Field | Type | Meaning |
|---|---|---|
| `entityId` | u32 | the entity index this session controls |

**`ClientHello`** — `0x0002` · Control scope · Command · Control lane · C→S.
The opening handshake: protocol version + the commander name the player chose.
The server sanitizes (printable ASCII, ≤ 20 chars, trailing spaces trimmed) and
de-duplicates (`-2`, `-3`, …) before adopting it.

| Field | Type | Meaning |
|---|---|---|
| `protocolVersion` | u32 | client's `PROTOCOL_VERSION` |
| `commanderName` | string | requested display name (raw; server sanitizes) |

**`PlayerInfo`** — `0x0301` · Wire · Event · Gameplay · S→C.
One roster entry, broadcast to everyone on join, name change, or wanted-level
change (crime, decay, death, hyperspace cooling).

| Field | Type | Meaning |
|---|---|---|
| `entityId` | u32 | which ship this describes |
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

**`CargoManifest`** — `0x0303` · Wire · Event · Gameplay · S→C (owner only).
The full per-commodity hold, resent whenever it changes outside a trade (a
scoop, or a respawn emptying it) — `PlayerStatus.cargoUsed` can't convey the
breakdown.

| Field | Type | Meaning |
|---|---|---|
| `units` | vector\<i32\> | held units per commodity, index 0..16 |

**`Chat`** — `0x0300` · Wire · Event · Gameplay · Both. *(Registered; UI not
yet wired — see Future outlook.)*

| Field | Type | Meaning |
|---|---|---|
| `sender` | u32 | sending entity index |
| `text` | string | UTF-8 line |

#### Input

**`InputCommand`** (alias `Net::ClientInput`) — `0x0100` · Wire · Command ·
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
`Teleport=6` (fuel-gated hyperspace jump), `Refuel=7`, `JumpDrive=8`
(in-system fast jump). Teleport and JumpDrive are intercepted by the server
loop and routed through `HyperspaceSystem` (§6.8); the rest hit
`ProcessStationRequest`.

`StationStatus`: `Ok=0`, `NotDocked=1`, `NoStock=2`, `NotEnoughCredits=3`,
`HoldFull=4`, `NoCargo=5`, `BadCommodity=6`, `CantDock=7`, `AlreadyOwned=8`,
`DockingRefused=9` (fugitive turned away), `NotEnoughFuel=10`, `OutOfRange=11`,
`MassLocked=12`, `Arrived=13` (jump landed you in flight), `Witchspace=14`
(misjump — ambush).

`EquipItem`: `Missile=1` (30.0 Cr, max 4), `LargeCargoBay=2` (400 Cr, +15 t),
`Ecm=3` (600 Cr), `FuelScoop=4` (525 Cr), `EnergyBomb=5` (900 Cr),
`EscapePod=6` (1000 Cr). *(Ownership is authoritative; ECM/bomb/pod behaviour
lands in G8.)*

#### Bulk

**Galaxy manifest chunk** — id `0x0210`, Bulk lane, S→C, sent once on connect.
Hand-encoded (fixed layout with a fixed-size name array, not the generic
codec): `total u32 | baseIndex u32 | count u16` then `count ×` entries of
`id u32 | x,y,z i64 | name char[12] (NUL-padded) | government u8 | economy u8 |
techLevel u8 | population u16 | productivity u16` (47 bytes each). The client
sizes its chart table from `total` and fills chunks as they arrive.

#### Snapshot stream (not a catalog message)

`'NSNP'` datagrams, unreliable, per-viewer. Header:
`magic u32 | version u16 (=1) | tick u32 | viewerId u32 | count u16` (16 bytes),
then `count ×` `EntitySnapshot` (58 bytes):

| Field | Type | Meaning |
|---|---|---|
| `id` | u32 | entity index |
| `x, y, z` | i64 ×3 | absolute world position |
| `noseX..Z` | f32 ×3 | forward direction |
| `roofX..Z` | f32 ×3 | up direction (side = nose × roof) |
| `speed` | f32 | units/tick along nose (dead-reckoning) |
| `type` | i16 | renderable ship type (legacy `SHIP_*`; see §6.10) |

Snapshots are **area-of-interest filtered** per viewer: entities within ±1 cell
of a 100 000-unit grid around the viewer, **plus** the local system's
landmarks (planet + station) out to 2 000 000 units so celestial bodies never
pop out mid-approach. Packetized to whole entities ≤ 1200 bytes. Later
snapshots supersede earlier ones; loss is never repaired, only outrun.

### 4.5 Server-internal messages (never on the wire)

The server's own combat pipeline is decoupled through an in-process
`MessageBus` with LocalOnly messages (ids in the non-wire half):

| Message | Id | Meaning |
|---|---|---|
| `FireWeapon{shooter, weapon, target}` | `0x8101` | a fire request (synthesized from `InputCommand`); resolved against the world |
| `Crime{offender, victimTeam, firstOffence}` | `0x8102` | a protected victim was fired on; police dispatch on first offence |
| `EntityKilled{victim, killer}` | `0x8103` | something died; ONE subscriber decides what a death does |
| `ActionTriggered{action, param}` | `0x8200` | **client**-local: raw input → command-builder bridge |

### 4.6 Canonical sequences

**Connect:** client sends `InputCommand` → server spawns the player entity,
replies `AssignPlayer` (Control) + galaxy manifest (Bulk) → client sends
`ClientHello{version, name}` → server sanitizes/dedupes, broadcasts the full
`PlayerInfo` roster → snapshots + `PlayerStatus`/`CargoManifest` begin flowing.

**Fire → kill → respawn:** `InputCommand.fire` → server publishes `FireWeapon`
→ `ResolveFireWeapon` applies damage, may publish `Crime` (wanted +1, police
launch from the nearest station with a warrant on the offender) and/or
`EntityKilled` → the death subscriber pays the killer (`CreditKill`), scatters
loot (`DropLoot` / `DropPlayerCargo`), and for a player victim: sends that
session `EntityDeath`, restores hull/shields, clears wanted, sweeps every NPC
`focus` off them, respawns them docked at the nearest station, broadcasts the
refreshed `PlayerInfo`, resends `CargoManifest` (now empty). NPC victims are
destroyed and broadcast to everyone.

**Hyperspace:** chart crosshair → `StationRequest{Teleport, stationId=systemId}`
→ server runs `Hyperspace()` → `StationResponse{status = Arrived | Witchspace |
NotEnoughFuel | OutOfRange | CantDock}` (+ a `PlayerInfo` broadcast if the
wanted level cooled) → the new position rides the next snapshot; fuel rides
`PlayerStatus`.

---

## 5. The authoritative server

### 5.1 Tick pipeline

Every ~33 ms, in this order:

1. **Drain the socket.** Route datagrams by magic: `InputCommand` →
   `ServerSessions::OnInput` (spawn-on-first-contact, stale-sequence drop,
   intent applied to `FlightIntent`; `fire`/`fireMissile` become `FireWeapon`
   bus messages) · reliable datagrams → per-session `MessageEndpoint`.
2. **Dispatch the bus** — fire commands resolve to `Crime`/`EntityKilled` facts.
3. **Roster upkeep** — on membership change, rebroadcast all `PlayerInfo`.
4. **Reliable requests** — per session: `StationRequest` (Teleport/JumpDrive
   routed through `HyperspaceSystem`, everything else through
   `ProcessStationRequest`), `ClientHello` (name adoption).
5. **`StepAi`** — NPC tactics write `FlightIntent`s (see §6.7).
6. **`GameLogic::Tick`** — `StepFlightInput` (intent → controls through caps)
   → `StepFlight` (orientation/position integration) → `StepMotion` (simple
   velocity movers, e.g. drifting canisters).
7. **Spawning** — `SpawnDirector::Step` (pirates near players, every 600 ticks,
   NPC cap 12) and `StepTraders` (lane traffic, every 900 ticks, cap 2).
8. **Shield regen** — every 8 ticks, players' shields/energy recharge.
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

- Keyed by UDP endpoint (`addr<<16 | port`). First contact spawns the player
  entity (see component list below) and queues `AssignPlayer` + the manifest.
- Latest-sequence-wins input application; idle reaping after 300 ticks.
- Owns the commander-name pipeline: sanitize → cap (20) → de-dupe → mirror to
  the authoritative `PlayerRecord` → roster broadcast.
- `Broadcast(msg)` queues a catalog message to every session's proper lane.

### 5.3 A player entity (as spawned)

`WorldTransform`, `Flight`, `FlightIntent`, `FlightCaps` (0.121 rad/tick roll &
pitch, 100 u/t max speed), `Wallet` (1000 = 100.0 Cr), `CargoHold` (20 t),
`DockState`, `Equipment` (3 missiles), `Fuel` (70/70 tenths), `PlayerTag`,
`Combatant` (Team Player, 255 energy, laser 10, range 6000, autoEngage
**false**, 150 ticks spawn grace), `Shields` (255/255), `Wanted` (0),
`PlayerRecord` (name, score), `NetType` (Viper hull for now).

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
  within 6000 units inside a cos ≥ 0.9 (~25°) aiming cone; damage derives from
  laser strength (`LaserDamageTo`).
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
sun entities exist yet — see Future outlook).

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
killer's wallet and bumps `PlayerRecord.score` — only a player killer with a
wallet earns; missiles' own detonations credit their owner; witchspace
withholds the money but not the score.

---

## 7. Client presentation layer

The client is deliberately dumb. It keeps:

- **Rendering:** DX11, legacy wireframe meshes, camera-relative floating
  origin. Replicated entities are drawn from interpolated snapshots
  (`SnapshotInterpolator` + dead-reckoning on `speed`).
- **HUD mirrors:** shields/energy/fuel/credits/missiles/cargo/wanted/score from
  `PlayerStatus` + `CargoManifest`; the roster (`PlayerInfo`) for ship labels;
  the market/chart from `StationResponse`/manifest. Local shield regen runs
  **only** when disconnected (single-player fallback path).
- **Input:** raw keys → `ActionTriggered` (LocalOnly bus) → command builder →
  one `InputCommand` per frame. Station screens send `StationRequest`s.
  The hyperspace key on a chart sends `Teleport`; the jump key sends
  `JumpDrive`; the server answers both.
- **Presentation effects:** death/explosion VFX (a world-anchored replicated
  explosion re-using the legacy debris animation), sounds (launch, hits, ECM,
  hyperspace, scoop beep), the break-pattern screen transitions.

A `StationResponse{Teleport, Arrived|Witchspace}` flips the client from the
station screen into flight; position updates always come from snapshots.

---

## 8. Determinism & testing

- **Everything gameplay is headless-testable**: no sockets, no GPU, no clock,
  no global RNG. 205 GameLogic tests + NeuronCore protocol suites run in CI
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
- Not yet addressed (future): rate limiting, encryption/authentication,
  server-side sanity on input *cadence* (a client can send at > 30 Hz; only
  the latest wins, so the damage is bounded).

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
| Witchspace odds / displacement | >253 of 256 (~0.8 %) / 20M | HyperspaceSystem.h |
| Mass-lock / in-system hop | 75 000 / ≤200 000 | HyperspaceSystem.h |
| Galaxy systems / extent / station orbit | 256 / ±100M / 8000 | GalaxyGen.h |
| Commander name cap | 20 chars | ServerSessions.h |
| String / vector wire caps | 4096 / 4096 | Serialize.h |

---

## 11. Message id inventory (complete)

| Id | Message | Scope | Lane | Dir |
|---|---|---|---|---|
| `0x0001` | AssignPlayer | Control | Control | S→C |
| `0x0002` | ClientHello | Control | Control | C→S |
| `0x0100` | InputCommand | Wire | Unreliable | C→S |
| `0x0200` | EntityDespawn | Wire | Gameplay | S→C |
| `0x0201` | EntityDeath | Wire | Gameplay | S→C |
| `0x0210` | GalaxyManifest chunk (hand-encoded) | Wire | Bulk | S→C |
| `0x0300` | Chat *(UI pending)* | Wire | Gameplay | Both |
| `0x0301` | PlayerInfo | Wire | Gameplay | S→C |
| `0x0302` | PlayerStatus | Wire | Gameplay | S→C (owner) |
| `0x0303` | CargoManifest | Wire | Gameplay | S→C (owner) |
| `0x0400` | StationRequest | Wire | Gameplay | C→S |
| `0x0401` | StationResponse | Wire | Gameplay | S→C |
| `0x8101` | FireWeapon | LocalOnly (server) | — | — |
| `0x8102` | Crime | LocalOnly (server) | — | — |
| `0x8103` | EntityKilled | LocalOnly (server) | — | — |
| `0x8200` | ActionTriggered | LocalOnly (client) | — | — |

Plus the two non-catalog streams: `'NSNP'` snapshots (§4.4) and the raw
`'NRLB'`/`'NEVT'` reliability framing (§4.1).

---

## 12. Future outlook (designed, not built)

In rough priority order — details in
[`MIGRATION_ROADMAP.md`](MIGRATION_ROADMAP.md) and [`gameplay.md`](gameplay.md):

- **G8 — equipment behaviours:** ECM (destroys in-flight missiles in range;
  new input bit + an `EcmPulse` event for the classic sound/flash), energy
  bomb (one-shot area kill), escape pod (ship lost, respawn docked, credits
  kept), laser temperature (overheat blocks fire). NPC ECM chance hooks into
  the existing panic-missile path. The purchased flags already exist
  server-side; G8 makes them *do* something.
- **Suns & cabin heat:** add sun entities to worldgen, port
  `update_cabin_temp` (heat ramp → death) and sun-skimming fuel scooping —
  deferred from G6/G7 so suns, heat and the fuel payoff land together.
- **Chat (dropped from Phase G scope for now):** the `Chat` message exists;
  needs client UI + server relay/sanitization.
- **Phase F — persistence (SQL Server):** the top structural gap — today a
  server restart wipes commanders (names, credits, cargo, equipment, score,
  wanted). Session-scoped by explicit decision until F lands.
- **Replication depth (Phase D/E leftovers):** delta compression,
  quantization, a strategic AOI tier, client-side prediction for the local
  ship (the starfield currently drifts against the replicated world).
- **Scale hardening (Phase H):** the BotClient load harness and profiling
  toward the 100-concurrent-player milestone; O(n²) pair loops in combat/
  collisions will need the spatial grid once fleets grow.
- **Operations (Phase I):** structured logging, metrics, admin tooling.
