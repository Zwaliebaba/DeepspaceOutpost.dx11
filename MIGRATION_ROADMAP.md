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
| "Modernize client" scope | **Decouple simulation from rendering + de-globalize state**; keep the faithful wireframe look |

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
   `extern struct univ_object universe[MAX_UNIV_OBJECTS];` and many loose flight
   globals (`flight_speed`, `front_shield`, `energy`, `mcount`, …). There is no
   "player N" — the whole program *is* one player.
2. **`MAX_UNIV_OBJECTS == 20`, local-system-only.** The "universe" is a tiny
   fixed array of the objects around one player. An open `int64³` world needs a
   **dynamic entity store** with spatial indexing.
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

   `update_universe()` in `space.cpp` both **moves** objects and **draws** them
   in one pass — this fusion is the core thing to break.
4. **Local-file persistence.** `file.cpp` saves the 256-byte `commander` block
   with `fopen/fwrite`. Must become server-side authoritative storage in MS SQL.
5. **Deterministic, integer physics & procedural galaxy** (`random.cpp` seeds,
   integer/fixed-point math). This is the **good news** — it's reproducible and
   cheap to replicate, ideal for an authoritative server.

---

## 2. Target architecture

```
            ┌──────────────────────────── NeuronCore (shared) ────────────────────────────┐
            │  math · fixed-point · int64 vec3 · tasks · timers · JSON · UDP socket wrap    │
            └──────────────────────────────────────────────────────────────────────────────┘
                       ▲                                              ▲
          ┌────────────┴───────────────┐                ┌─────────────┴──────────────┐
          │        NeuronClient        │                │        NeuronServer         │
          │  net client · reliability  │                │  authoritative Universe sim    │
          │  prediction/reconciliation │                │  AOI/replication · MS SQL   │
          │  snapshot interpolation    │                │  reliability · binary proto │
          └────────────┬───────────────┘                └─────────────┬──────────────┘
                       ▲                                              ▲
          ┌────────────┴───────────────┐                ┌─────────────┴──────────────┐
          │   DeepspaceOutpost (.exe)  │                │        Server (.exe)        │
          │  render (DX11) · input ·   │   ──UDP──▶      │  host loop · sessions ·     │
          │  UI/HUD · audio · predict  │   ◀─snapshot─   │  tick scheduler · persist   │
          └────────────────────────────┘                └─────────────────────────────┘
```

The same headless **Universe sim** code links into both: the server runs it
authoritatively; the client runs a copy for **prediction** of the local ship.

### 2.1 Naming conventions

New engine code must follow the convention of the layer it lands in — the repo
already has three:

| Layer | Types | Methods / functions | Params | Members | Constants |
|---|---|---|---|---|---|
| **Neuron engine** (`NeuronCore/Client/Server`, incl. new `GameLogic`) | `PascalCase` | **`PascalCase`** (`Startup`, `Normalize`) | `_camelCase` (`_value`) | `m_` (`m_running`) | `UPPER_CASE` (`ENGINE_VERSION`) |
| **Platform layer** (`DeepspaceOutpost/platform/`) | `PascalCase` | **`camelCase`** (`init`, `clearCanvas`) | `camelCase` | trailing `_` (`palette_`) | `kPascalCase` (`kCanvasWidth`) |
| **Ported Elite logic** (`space.cpp`, …) | `snake_case` / `struct vector` | **`snake_case`** (`update_universe`) | `snake_case` | file globals | `UPPER_CASE` macros |

**Rules for the migration:**
- All new MMO subsystems — `GameLogic`, `Universe`, networking, AOI,
  replication, persistence — are **Neuron engine** code → use the **PascalCase**
  method style, `_camelCase` params, `m_` members, `UPPER_CASE` constants, and
  live under the `Neuron::` namespace (e.g. `Neuron::GameLogic`).
- New types use `PascalCase` everywhere (`Universe`, `RenderQueue`, `EntityId`,
  `Vector3i64`). The 64-bit integer world vector is named **`Vector3i64`** to
  match `Neuron::Math::Vector3`, *not* `Vec3i64`.
- Functions added **inside ported `.cpp` files** keep that file's local
  `snake_case` (e.g. the new `render_universe()` sits next to `update_universe()`
  in `space.cpp`) until the file is fully modernized into the engine layer.
- New code added **inside `platform/`** matches the platform `camelCase` /
  `kPascalCase` style (e.g. the `RenderQueue` consumer on the platform side).

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

- **A1 — Render seam.** Stop game logic from calling `gfx_*` directly. Introduce
  a `RenderQueue` (list of draw commands: line/polygon/sprite/text). The sim
  *emits* draw commands; the platform layer consumes them. Convert `space.cpp`,
  `threed.cpp`, `swat.cpp` so `update_universe()` does **move-only**, and a new
  `render_universe()` walks the entity list to emit draw commands.
- **A2 — De-globalize into a `Universe` object.** Move `cmdr`, `myship`,
  the flight globals, and the universe array into a `Universe` struct passed by
  reference. (Mechanical but large; do it file-by-file behind the A1 seam.)
- **A3 — Dynamic entity store + `int64³` coordinates.** Replace
  `univ_object universe[20]` with a growable entity container keyed by a stable
  `EntityId`. Add absolute `Vector3i64` position to each entity. Implement the
  **floating-origin** transform: render math stays 32-bit relative to the local
  ship; the world position is `int64`. Keep the deterministic integer physics.
- **A4 — Extract `GameLogic` into a library** that links into both
  `NeuronClient` and `NeuronServer`, compiles **headless** (no DX11/audio), and
  ticks via a fixed timestep. Move the already-pure files (`pilot`, `trade`,
  `planet` gen, `elite` data) in first; then the split `space`/`swat`.
- **Deliverable:** single-player game still runs identically, but now on top of
  a headless, de-globalized, `int64`-world sim core. **This is the keystone.**

### Phase B — Server foundation
- Flesh out **`Server/`**: a real host loop with a **fixed-tick scheduler**
  (e.g. 20–30 Hz sim), session manager, and a console/admin surface. Drop the
  placeholder 10-second timer.
- Link `NeuronServer` → `GameLogic`; run one authoritative `Universe`.
- Spawn NPCs/economy server-side using the existing deterministic generators.

### Phase C — Networking (raw winsock UDP + reliability)
- **UDP socket wrapper** in NeuronCore over the existing winsock includes
  (non-blocking, IPv4/IPv6).
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
- **Load test** with 100 simulated clients (bot harness driving input).
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
- **From 20 objects to thousands.** `MAX_UNIV_OBJECTS=20` is replaced by a
  dynamic store; AOI keeps the *per-client* working set small even though the
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
