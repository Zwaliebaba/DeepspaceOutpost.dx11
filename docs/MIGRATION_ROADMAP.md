# DeepspaceOutpost → MMO Migration Roadmap

Target: turn the single-player *Elite: The New Kind* port into a
**server-authoritative MMO** supporting **up to 100 concurrent players** as a
first milestone.

> **Sequencing rule (per project owner):** *modernize/decouple the client
> first*, **then** split logic between client and server. Phases A → B reflect
> that explicitly.

---

## 0. Locked design decisions

| Topic | Decision |
|---|---|
| Platform | **Windows** client **and** Windows server |
| Authority | **Server-authoritative** (clients send input, render replicated state) |
| World | **Single open 3D world**, **absolute `int64` X/Y/Z** coordinate field |
| Coordinates | Server stores absolute `int64³`; client uses a **floating origin** for its 32-bit render math |
| Streaming | **Area-of-Interest (AOI)** — server only sends each client the entities near it |
| Transport | **Raw winsock UDP** + a custom reliability/ordering layer (built on NeuronCore's existing winsock) |
| Persistence | **Microsoft SQL Server** (accounts, ships, inventory, market, world state) |
| Wire format | **Hand-rolled binary** for the hot path (snapshots/input); JSON (NeuronCore::Json) for cold path (handshake/config) |
| Entity model | **ECS** — an **in-house** Entity Component System in NeuronCore; the de-globalized `Universe` *is* the ECS world (introduced ECS-first at A2, not bolted on later) |
| Logic boundary | **`GameLogic` is server-only authority** (AI, economy, combat, spawning). Client links only **`GameShared`** — the deterministic motion/component slice it needs for prediction. |
| Test harness | A **headless `BotClient`** (no render/audio) drives scripted/AI bots over the real net stack for load/soak testing (esp. the 100-player test). |
| Code standards | Follow the repo's **`.github/coding-standards.md`** as the single source of truth (see §2.1). |
| Docs location | All project docs live in **`docs/`** (matches the standards' layout). |
| "Modernize client" scope | **Decouple simulation from rendering + de-globalize state into the ECS**; keep the faithful wireframe look |

---

## 1. Current-state analysis (grounded in the code)

### What exists
- **`DeepspaceOutpost/`** — the complete game. Ported C logic (`main`, `space`,
  `threed`, `swat`, `docked`, `shipdata`, `pilot`, `trade`, `missions`, …) plus
  a modern C++23 **`platform/`** layer (DX11, XAudio2, Win32 input/dialog/font/image).
- **`NeuronCore/`** — shared engine lib: tasks/threads (`TasksCore`), timers
  (`TimerCore`), JSON, filesystem, math. **Already includes `winsock2`/`ws2tcpip`.**
  Currently WinRT-bound, which is fine given Windows-only servers.
- **`NeuronClient/` / `NeuronServer/`** — placeholder libs (just include NeuronCore).
- **`Server/`** — placeholder console exe (a 10-second timer loop).
- Build: CMake + C++23 + **MSVC/Windows only**.

### The five migration blockers
1. **Global singleton state, sized for one player.** `elite.h` exposes
   `extern struct commander cmdr;`, `extern struct player_ship myship;`,
   `extern struct local_object local_objects[MAX_LOCAL_OBJECTS];` (renamed from
   the old `universe[]`) and many loose flight
   globals (`flight_speed`, `front_shield`, `energy`, `mcount`, …). There is no
   "player N" — the whole program *is* one player.
2. **`MAX_LOCAL_OBJECTS == 20`, local-system-only.** The `local_objects` array
   is a tiny fixed set of the objects around one player. An open `int64³` world needs a
   **dynamic, ECS-backed entity store** with spatial indexing (§2.3).
3. **Simulation and rendering are fused.** Measured `gfx_*` call counts:

   | File | Role | `gfx_` calls | Disposition |
   |---|---|---:|---|
   | `pilot.cpp` | NPC AI | **0** | → server sim core, ~as-is |
   | `trade.cpp` | economy/market | **0** | → server sim core, ~as-is |
   | `elite.cpp` | state | **0** | → split: data to core, screens to client |
   | `planet.cpp` | planet gen | **0** | → server (gen) + client (render) |
   | `shipface.cpp` | face/normal data | **0** | → shared data |
   | `swat.cpp` | combat/tactics | **6** | → split: tactics to server, draw to client |
   | `threed.cpp` | 3D projection + raster | **9** | → render-only (client) |
   | `stars.cpp` | starfield | **15** | → client (cosmetic) |
   | `space.cpp` | **universe sim + draw** | **29** | → **surgical split** (the hard one) |
   | `missions.cpp` | mission UI text | **36** | → client UI |
   | `docked.cpp` | station/trade UI | **98** | → client UI |

   `update_local_objects()` in `space.cpp` both **moves** objects and **draws**
   them in one pass — this fusion is the core thing to break.
4. **Local-file persistence.** `file.cpp` saves the 256-byte `commander` block
   with `fopen/fwrite`. Must become server-side authoritative storage in MS SQL.
5. **Deterministic, integer physics & procedural galaxy** (`random.cpp` seeds,
   integer/fixed-point math). This is the **good news** — it's reproducible and
   cheap to replicate, ideal for an authoritative server.

---

## 2. Target architecture

Modules and their dependency direction (lower layers never depend on higher
ones — per the standards' *Layers and Dependencies* rule):

| Module | Kind | Role | Depends on |
|---|---|---|---|
| `NeuronCore` | lib | Engine foundation: **ECS**, math/fixed-point, `Vector3i64`, tasks, timers, JSON, raw-UDP socket, binary serialize. No gameplay. | — |
| `GameShared` | lib | **Deterministic, headless** gameplay shared by client *and* server: ECS component definitions (`Transform`, `Motion`, `ShipDef`…), the **motion/physics integration** system, ship-data tables. The *only* sim code the client needs (for prediction). | `NeuronCore` |
| `GameLogic` | lib | **SERVER-ONLY authority**: AI/tactics, economy/market, combat resolution, missions, spawning/encounters. Headless, no `gfx`. | `GameShared` |
| `NeuronClient` | lib | Client engine: D3D11 graphics, audio, input, GUI **plus** client networking (reliability, prediction/reconciliation, snapshot interpolation). **Does not link `GameLogic`.** | `GameShared` |
| `NeuronServer` | lib | Server net + AOI/replication + persistence (MS SQL). | `GameLogic` |
| `DeepspaceOutpost` | **exe** | Game client: main loop, game-specific rendering (wireframe/HUD via render queue), input, UI. | `NeuronClient`, `GameShared` |
| `BotClient` | **exe** | **Headless test client** — scripted/AI bots, **no render/audio**, for heavy load & soak testing. Same net stack as the real client. | `NeuronClient`, `GameShared` |
| `Server` | **exe** | Dedicated server host: loop, sessions, fixed-tick scheduler. | `NeuronServer`, `GameLogic` |

```
  client input  ──UDP──▶   Server   ──UDP snapshots──▶  client / BotClient
   (DeepspaceOutpost or BotClient)   (authoritative sim: GameLogic over GameShared/ECS)
```

**Why the split (answers "shouldn't GameLogic be server-only?"):** yes — the
*authority* (AI, economy, combat resolution, spawning) lives in `GameLogic`,
**server-side only**. The client never runs it. But server-authoritative play
**with client prediction** (Phase E) needs the client to integrate *its own
ship's* motion locally; that small **deterministic** slice lives in `GameShared`,
which both sides link. If you ever drop prediction in favour of pure
interpolation, `GameShared` shrinks to just the ECS component definitions and the
client becomes effectively logic-free.

**Headless BotClient (answers "can I have a headless client for bot testing?"):**
yes — and it falls out almost for free once A1 makes the sim headless. `BotClient`
links the *same* `NeuronClient` + `GameShared` net/prediction stack as the real
game but swaps the DX11/audio/UI front-end for a **null render sink** and a
**bot-input driver** (scripted flight paths or simple AI). It connects over the
real UDP protocol, so N bot processes (or N bots per process) drive genuine
server load — this is the harness used for the 100-player load test in Phase H.

### 2.1 House style (single, project-wide)

The **single source of truth** is the repo's **`.github/coding-standards.md`**.
It was updated so **functions/methods are `PascalCase`** (matching the NeuronCore
engine, which becomes the reference for the house style). Summary:

| Element | House style | Example |
|---|---|---|
| Namespaces | `PascalCase`, nested under `Neuron::` | `Neuron::GameLogic` |
| Types (class/struct/enum) | `PascalCase`; `enum class` w/ explicit base | `Universe`, `RenderQueue`, `EntityId`, `enum class ShipType : int8_t` |
| Methods & free functions | **`PascalCase`** | `Tick()`, `AddEntity()`, `Normalize()` |
| Parameters | `_camelCase` (Neuron convention) | `_value`, `_deltaTime` |
| Member variables | `m_` + `camelCase` | `m_running`, `m_entities` |
| Locals | `camelCase` | `entityCount` |
| Constants / macros / `constexpr` | `UPPER_SNAKE_CASE` | `ENGINE_VERSION`, `MAX_ENTITIES` |
| Integer world vector | `PascalCase`, matches `Math::Vector3` | **`Vector3i64`** (not `Vec3i64`) |
| New file names | `PascalCase.cpp/.h` for engine types/modules; keep `snake_case` when editing existing ported files | `Universe.h`, vs. `space.cpp` |

**Outliers to migrate** (folded into the phases, not a big-bang rewrite):
- **Platform layer** is the main non-conformant code now: `camelCase` methods
  (`Renderer::init` → `Init`, `clearCanvas` → `ClearCanvas`), trailing-underscore
  members (`palette_` → `m_palette`), and `kPascalCase` constants
  (`kCanvasWidth` → `CANVAS_WIDTH`). Converted file-by-file as the render seam
  (A1) touches it.
- **Ported Elite logic** stays `snake_case` only while still legacy; each file
  adopts the house style as it is modernized into the engine (A1–A4). New
  functions added to a still-legacy file temporarily match that file's local
  `snake_case` (e.g. `render_local_objects()` beside the renamed
  `update_local_objects()` in `space.cpp`) and are renamed when the file moves
  to the engine.
- **NeuronCore** is already conformant on method casing (`Startup`, `Normalize`).

> **Native-First (from the standards):** new subsystems must add genuine
> behavior, not thinly wrap a native API. The in-house ECS, the UDP
> reliability layer, and the binary serializer all qualify (real abstractions /
> invariants); a bare pass-through around `winsock` would not. Prefer std
> containers except where the ECS storage's measured layout needs justify a
> custom container.

### 2.2 Legacy de-naming — *done first, frees the `Universe` name*

So the **new** `Neuron::Universe` is clean and never confused with the old
20-object local array, the legacy "universe" identifiers were renamed up front
(complete, repo-wide, behaviour-preserving — pure identifier rename):

| Legacy (removed) | Renamed to | Notes |
|---|---|---|
| `struct univ_object` | `struct local_object` | the old per-object record |
| `universe[]` | `local_objects[]` | the fixed local-bubble array |
| `MAX_UNIV_OBJECTS` | `MAX_LOCAL_OBJECTS` | (was 20) |
| `update_universe()` | `update_local_objects()` | move+draw pass (split in A1) |
| `clear_universe()` | `clear_local_objects()` | |
| `move_univ_object()` | `move_local_object()` | |
| `univ` (local ptr) | `obj` | object pointer in `threed`/`swat` |

The name **`Universe`** is now reserved exclusively for the new clean,
house-style global world container introduced in A2/A3.

### 2.3 Entity model — in-house ECS (`Neuron::ECS`)

The de-globalized `Universe` **is** an Entity Component System, built in-house in
NeuronCore (no third-party dependency), introduced **ECS-first at A2**.

- **Why ECS:** the open `int64³` world holds thousands of heterogeneous entities
  (players, NPC ships, missiles, cargo, asteroids, stations, sun/planet). Data-
  oriented component storage gives cache-friendly iteration for the server tick,
  composition replaces the current `if (type == …)` branching in `swat`/`pilot`,
  and **components map 1:1 onto replication** — AOI picks entities, the snapshot
  serializes their *replicated* components as deltas.
- **Design:** sparse-set (or archetype) storage; stable `EntityId` (generational
  handle); systems run in a **fixed, deterministic order** each tick (required
  for client prediction/reconciliation). Components are plain `PascalCase`
  structs (`Transform`, `Motion`, `Combat`, `Ai`, `ShipDef`, `Renderable`,
  `NetReplicated`); systems are `PascalCase` (`MotionSystem`, `TacticsSystem`).
- **Migration is faithful, not a rewrite:** at A2 each `local_object` becomes one
  entity with *fat* components mirroring the existing fields; the existing loops
  become systems one at a time, each verified against the Phase 0 golden runs.
  Components are split/refined only after behavior is locked.
- **Replication hooks** are part of the ECS from the start: a component marked
  `NetReplicated` is automatically eligible for AOI snapshots (Phase D), so the
  netcode never reaches into game structs directly.

---

## 3. Phase plan

Each phase is independently shippable/testable. Phases A0–A4 are the
"modernize the client first" work and must land before B+.

### Phase 0 — Baseline & safety net
- Stand up a **build/test path**: CMake build on a Windows runner (this repo is
  MSVC/DX11; it cannot be compiled in the Linux dev container — wire CI early).
- Add a minimal **test harness** (e.g. a headless tick test) so refactors are
  verifiable. Snapshot a few deterministic sim runs as golden references.
- Tag the current single-player build as a known-good baseline.

### Phase A — Modernize & decouple the client *(do this first)*
**Goal: a headless, multi-instance simulation core extracted from rendering,
with no global singletons — without changing on-screen behavior.**

- **A0 — Legacy de-naming *(done).*** Repo-wide, behaviour-preserving rename of
  the old `universe`/`univ_object` identifiers to `local_objects`/`local_object`
  (see §2.2) so the new clean `Neuron::Universe` owns that name. This lands
  first, before any new `Universe` code is written.
- **A1 — Render seam.** Stop game logic from calling `gfx_*` directly. Introduce
  a `RenderQueue` (list of draw commands: line/polygon/sprite/text). The sim
  *emits* draw commands; the platform layer consumes them. Convert `space.cpp`,
  `threed.cpp`, `swat.cpp` so `update_local_objects()` does **move-only**, and a
  new `render_local_objects()` walks the entity list to emit draw commands.
- **A2 — De-globalize into `Neuron::Universe`, which *is* an ECS world.**
  Instead of a plain container that gets replaced later, the de-globalized
  `Universe` is the **in-house ECS registry from the start** (see §2.3). Each
  legacy `local_objects[i]` becomes **one entity**; the `local_object` fields
  migrate into a few *fat* components first (`Transform`, `Motion`, `Combat`,
  `Ai`, `ShipDef`, `Renderable`) so the data layout is faithful before it is
  refined. `cmdr`/`myship`/flight globals become components on a **player
  entity**. The loops (`update_local_objects`, `tactics`, AI from `pilot.cpp`)
  are converted into **systems**, one at a time, each guarded by the Phase 0
  golden-run tests. Mechanical and large; do it file-by-file behind the A1 seam.
- **A3 — `int64³` coordinates + floating origin.** Make `Transform.position`
  an absolute **`Vector3i64`** component; add a uniform-grid/loose-octree
  **spatial index** over the ECS world (reused later for AOI). Implement the
  **floating-origin** transform: render/physics math stays 32-bit relative to
  the local ship while the authoritative world position is `int64`. Keep the
  deterministic integer physics. (Smaller now that A2 already built the ECS.)
- **A4 — Split into `GameShared` + `GameLogic` libraries** (both **headless** —
  no DX11/audio, fixed-timestep tick):
  - **`GameShared`** (links both client and server): the ECS component
    definitions and the **deterministic motion/physics integration** — the only
    sim slice the client needs for prediction — plus the ship-data tables.
  - **`GameLogic`** (**server-only**): the authority systems — AI/tactics
    (`pilot`, `swat` resolution), economy (`trade`), spawning/encounters,
    missions, damage/legal status. Links `GameShared`; **the client never links
    it.**
  - Move the already-pure files (`pilot`, `trade`, `planet` gen, `elite` data)
    into `GameLogic` first; the deterministic motion split out of `space` goes
    into `GameShared`.
- **Deliverable:** single-player game still runs identically, but now on top of a
  headless, de-globalized, `int64`-world ECS — with the **server authority
  (`GameLogic`) cleanly separable from the shared prediction slice
  (`GameShared`)**. **This is the keystone.**

### Phase B — Server foundation + headless BotClient
- Flesh out **`Server/`**: a real host loop with a **fixed-tick scheduler**
  (e.g. 20–30 Hz sim), session manager, and a console/admin surface. Drop the
  placeholder 10-second timer.
- Link `NeuronServer` → `GameLogic`; run one authoritative `Universe`.
- Spawn NPCs/economy server-side using the existing deterministic generators.
- **Stand up the `BotClient` exe** (`NeuronClient` + `GameShared`, **no
  render/audio**): a null render sink plus a bot-input driver (scripted flight
  first, simple AI later). Even before the network exists it runs against an
  in-process loopback server — giving an automated, headless integration test of
  the sim from day one, and the foundation for the Phase H load test.

### Phase C — Networking (raw winsock UDP + reliability)
- **UDP datagram endpoint** in NeuronCore (non-blocking, IPv4/IPv6). Calls
  `winsock` directly per *Native-First* — it earns its keep by owning the
  non-blocking setup and feeding the reliability layer, not by thinly renaming
  `sendto`/`recvfrom`.
- **Custom reliability layer**: sequence numbers, ack/ack-bitfield, retransmit,
  ordered + unordered channels, fragmentation/reassembly for large snapshots,
  connection handshake, heartbeat/timeout, basic congestion pacing.
- **Hand-rolled binary protocol**: fixed-layout, versioned message structs
  (login, input command, snapshot, event, chat). JSON only for handshake/config.
- **Loopback harness** first (client+server in one process) before going to wire.

### Phase D — Replication & Area-of-Interest (for the `int64` field)
- **Spatial index** over the `int64³` world (uniform grid / hashed cells, or a
  loose octree) so neighbor queries are O(local).
- **AOI per client**: each tick, compute the entities within the player's
  interest radius; send **delta snapshots** (baseline + changes) only for those.
- **Entity lifecycle events**: enter-AOI (full state) / leave-AOI (despawn).
- **Priority/bandwidth budgeting** so 100 players stay within send limits.

### Phase E — Client prediction & reconciliation
- Client runs the **same `GameLogic`** for the **local ship only**: apply input
  immediately (prediction), then **reconcile** against authoritative snapshots
  (replay un-acked inputs on correction).
- **Snapshot interpolation** for remote entities (render in the past by ~100ms).
- **Floating origin on the client** so the camera/render stay in 32-bit space
  while the world is `int64`.

### Phase F — Persistence (Microsoft SQL Server)
- **Data-access layer** in `NeuronServer` (ODBC / SQL Server Native Client),
  kept behind an interface so it's mockable and swappable.
- **Schema**: accounts/auth, characters/commanders (migrate the 256-byte
  `commander` block into normalized tables), ship loadout, inventory/cargo,
  per-system market state, world persistence, audit/anti-cheat log.
- **Async, batched writes** off the sim thread (snapshot the dirty set, flush on
  a worker) so DB latency never stalls the tick.
- **Login flow**: authenticate → load character → spawn into world; periodic +
  on-logout save.

### Phase G — Multiplayer gameplay
- **Shared combat** (server resolves hits/damage/kills authoritatively;
  `swat.cpp` tactics run server-side; lasers/missiles are server events).
- **Shared economy/trading** (server owns market prices and station stock; trade
  becomes a validated request/response).
- **Player visibility & identity** (names, factions, legal status across players).
- **Chat / social**, **docking with shared stations**, basic **PvP rules**.

### Phase H — Hardening & scale to 100
- **Load test** with 100 `BotClient` instances (the Phase B headless bot harness)
  driving input over the real UDP protocol.
- Profile tick time, bandwidth/player, DB throughput; tune AOI radius, snapshot
  rate, and grid cell size.
- **Anti-cheat**: input validation, rate limits, server-side bounds checks
  (authority already blocks most movement/teleport cheats).
- **Resilience**: reconnect/resume, graceful disconnect, server save-on-crash.

### Phase I — Operations
- Server config (ports, tick rate, DB connection), structured logging/metrics,
  versioned protocol with a handshake check, build/deploy scripts, backups.

---

## 4. Key technical design notes

- **`int64³` world + floating origin.** Absolute positions are `int64` to span a
  huge field without precision loss. Each client (and the sim's render pass)
  rebases to a local origin near the player so existing 32-bit/fixed-point math
  is reused unchanged. Rebase when the player crosses a cell boundary.
- **Determinism.** Keep the integer/fixed-point physics; it makes prediction,
  reconciliation, and reproducible bug reports tractable. Avoid float
  nondeterminism in shared sim paths.
- **From 20 objects to thousands.** `MAX_LOCAL_OBJECTS=20` is replaced by the
  ECS world (§2.3); AOI keeps the *per-client* working set small even though the
  world total is large.
- **Tick model.** Fixed timestep server tick (20–30 Hz); snapshots at the tick
  rate or a fraction; clients interpolate between snapshots and predict locally.
- **Reuse what's already clean.** `pilot/trade/planet/elite` (0 `gfx_` calls)
  move to the sim core almost verbatim — start there to build momentum.

---

## 5. Risks & open items
- **MSVC/DX11-only build** can't be compiled or run in this Linux container —
  CI on a Windows runner (Phase 0) is required to verify every phase.
- **A1/A2 are the largest, riskiest refactor** (touching `space.cpp` +
  ~260 `gfx_` call sites). Do it incrementally behind the render seam with the
  golden-run tests guarding behavior.
- **Reliability layer over raw UDP is non-trivial** — budget real time for
  retransmit/ordering/fragmentation and fuzz/packet-loss testing.
- **Open balance/design questions** to resolve before Phase G: PvP rules &
  safe zones, death/insurance handling, how the open `int64` field maps onto
  Elite's discrete star systems (one continuous space, or systems as regions
  within the field?), and starting interest radius / max entities per AOI.

---

## 6. Suggested immediate next step
Begin **Phase 0 + Phase A1**: stand up the Windows CI build, add a headless
golden-run test, and introduce the `RenderQueue` seam in `space.cpp` /
`threed.cpp` so the simulation stops calling `gfx_*` directly. Everything else
builds on that decoupling.
