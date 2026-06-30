# Message.md — A Unified Message System for NeuronCore

> **Status:** Implementation in progress — **Phases 0–2 landed**, Phase 3 next (see §14 and the
> table just below). v2 folded in a commercial-engine architecture review; v2.1 resolved the four
> open questions (now §16: one catalog; `DamageApplied` server-internal; validation-now/
> crypto-deferred; raw-float with on-demand quantization). The settled decisions:
>
> | Decision | Choice |
> |---|---|
> | Scope of the concept | **Unified bus + wire** — one message concept drives an in-process pub/sub bus *and* the client↔server wire protocol, built on shared definitions. |
> | Existing schemas | **Fold in / replace incrementally** — `ClientInput`, `GameEvents`, `StationProtocol` become catalog entries; bespoke encode/decode deleted after byte-parity. |
> | Payload style | **Typed structs + auto-serialize** — each message is a C++23 struct describing its fields once; a concept-constrained serializer derives the codec. |
> | Catalog authoring | **Structs + registration macro** — idiomatic structs, each `REGISTER_MESSAGE`'d into a runtime+compile-time registry for tooling/CI. |
> | Version compatibility | **Lockstep, minimal versioning** — all peers run the same version; **cross-version interop is explicitly out of scope** (no capability negotiation / deprecation windows). Record-length framing and id-non-reuse are kept anyway (decode robustness + ABI hygiene, nearly free). |
> | Determinism / replay | **Design for it now, build later** — deterministic ordering key + dispatch generations now; capture/replay harness is a later phase. |
> | Reliable lanes | **Split control / gameplay / bulk** — multiple `ReliableChannel`s so a large manifest can't head-of-line-block a death or session-control message. |

### Implementation status (at a glance)

| Phase | Scope | Status |
|---|---|---|
| 0 | Mechanism: `NeuronCore/Messages/` (id/traits/`NetEntityId`/codec/registry/bus/framing) + tests | ✅ Landed |
| 1 | In-process bus on the server: combat effects via `FireWeapon`/`Crime`/`EntityKilled` | ✅ Landed |
| 2 | Unreliable lane: `InputCommand` on `'NMSG'`, `'NCMD'` deleted (byte-parity proven) | ✅ Landed |
| 3a | Fold reliable schemas (`GameEvents` + `StationProtocol`) into the catalog, single channel | ✅ Landed |
| 3b-1 | Fold `GalaxyManifest` onto a catalog id; retire `EventType` / delete `GameEvents.h` | ✅ Landed |
| 3b-2 | Split physical Control/Gameplay/Bulk reliable lanes (`MessageEndpoint`) | ✅ Landed |
| 4a | Client-side `MessageBus`; route inbound facts through it (symmetric with server) | ✅ Landed |
| 4b | Key polling → `ActionTriggered` (LocalOnly) → command-builder → `InputCommand` | ✅ Landed |
| 5 | Tooling & hardening (decoder, schema/compat export, fuzz, race tests, metrics) | ⏳ Pending |

> **Verification note.** The project targets MSVC; this environment has no Windows toolchain, so
> the std-only message headers and the winsock-free test suites are built locally with clang++ 18
> and g++ 13 (`-std=c++23 -Wall -Wextra`), and the MSVC build of the client/server objects is
> confirmed by CI on the branch.
>
> **Consolidation with PR #7 (GUI/GraphicsCore).** That work introduced a second, client-side
> pub/sub (`NeuronCore/EventManager` + `Event.h`). Per decision, its event pub/sub was removed so
> `Msg::MessageBus` is the **single** event/pub-sub mechanism; `EventManager` is reduced to its
> Win32 window-message dispatch (`WndProc`/`AddEventProcessor`), which the platform layer uses.

**Catalog so far** (concrete messages that exist in code today):

| Message | Home | Scope / Lane / Dir | Phase |
|---|---|---|---|
| `InputCommand` | `NeuronCore/Messages/Defs/` | Wire · Unreliable · C→S | 2 |
| `AssignPlayer` | `NeuronCore/Messages/Defs/CoreEvents.h` | Control · Control · S→C | 3a |
| `EntityDespawn` | `NeuronCore/Messages/Defs/CoreEvents.h` | Wire · Gameplay · S→C | 3a |
| `EntityDeath` | `NeuronCore/Messages/Defs/CoreEvents.h` | Wire · Gameplay · S→C | 3a |
| `Chat` | `NeuronCore/Messages/Defs/CoreEvents.h` | Wire · Gameplay · both | 3a |
| `StationRequest` | `NeuronCore/StationProtocol.h` | Wire · Gameplay · C→S | 3a |
| `StationResponse` | `NeuronCore/StationProtocol.h` | Wire · Gameplay · S→C | 3a |
| `FireWeapon` | `GameLogic/CombatMessages.h` | LocalOnly · — · — (command) | 1 |
| `Crime` | `GameLogic/CombatMessages.h` | LocalOnly · — · — (fact) | 1 |
| `EntityKilled` | `GameLogic/CombatMessages.h` | LocalOnly · — · — (fact) | 1 |

Reliable messages now ride `Msg::MessageEndpoint` — one `ReliableChannel` per **Control/Gameplay/
Bulk** lane, routed by each message's `Lane` trait (3b-2). `GalaxyManifest` carries a reserved
catalog `MessageId` (`Net::GALAXY_MANIFEST_ID`, **Bulk** lane) but keeps its bespoke chunked encode
(fixed `char[]` array); `EventType`/`GameEvents.h` are **deleted** — every client↔server datagram is
now a catalog message (`InputCommand` on the unreliable lane; everything else on a reliable lane).

---

## 1. Goals & non-goals

### Goals
- **One mechanism** to express "X happened / do Y", identical whether sender and receiver
  are two systems in one process or two machines across the network.
- **Drive interactions through messages** at the *edges*: key presses, commands, lifecycle,
  deaths, despawns, docking, trades, chat — decoupling producers from consumers.
- **All gameplay client ↔ server traffic is cataloged messages.** (The pre-session
  security/transport envelope may live below the catalog — see §13.)
- **Flexible parameters on demand (C++23)** without a flag-day wire break.
- **Zero behaviour in NeuronCore.** The bus, codec, and registry are *mechanism* only;
  behaviour (handlers) stays per-side (server in `GameLogic`/`Server`, presentation in
  `NeuronClient`/`DeepspaceOutpost`).
- **Deterministic & testable**, socket-free core; replay-ready ordering.
- **Commercial-scalable**: validated inbound, bounded allocation, observable, multi-lane.

### Non-goals
- Not the simulation scheduler. **Hot deterministic simulation stays direct ECS/system
  iteration**; messages are for *edges* only (§5).
- Not replacing snapshots/replication (bulk self-superseding state stays snapshots).
- Not a new transport (reuse UDP + `ReliableChannel`).
- **Not cross-version compatible** (lockstep; see decision table).
- No third-party libraries (native-first, std-only core).

---

## 2. Where we are today (the landscape this replaces)

| Concern | Today | Lives in |
|---|---|---|
| Client → server intent | `ClientInput` + `WriteInput`/`ReadInput`, unreliable, latest-seq-wins | `NeuronCore/ClientInput.h` |
| Reliable events | `EventType` (u16) + per-event `Encode*`/`Decode*` (Despawn, Death, Chat, AssignPlayer, …) | `NeuronCore/GameEvents.h` |
| Docked request/response | `StationRequest`/`StationResponse` + encode/decode over `ReliableChannel` | `NeuronCore/StationProtocol.h` |
| Reliable transport | `ReliableChannel` (seq/ack/resend/ordered), magic `'NEVT'` | `NeuronCore/ReliableChannel.h` |
| Byte codec | `DataWriter`/`DataReader` (LE, bounds-checked) | `NeuronCore/DataWriter.h`, `DataReader.h` |
| In-process events | **None** — systems call directly; `Server/Main.cpp` inlines fire→crime→death→despawn; `StepCombat()` returns `vector<Kill>` | `Server/Main.cpp`, `GameLogic/*` |
| Client consumption | `ReplicationClient::PollEvent()` → raw `ReliableMessage`; `main.cpp` switch-decodes `EventType` | `NeuronClient/*`, `DeepspaceOutpost/main.cpp` |

**Facts that shape the design**
- The reliable side already is `ReliableMessage { uint16_t type; vector<uint8_t> payload }`
  — we generalise `type` → `MessageId` and replace hand codecs with generated ones.
- The server is a **fixed ~30 Hz tick** (drain → apply → simulate → broadcast): the natural
  drain-and-dispatch point. The bus is **queued/deferred**, not recursive-immediate.
- **`ECS::EntityId` has a `generation`, but snapshots/events send the bare `index`** and the
  server resolves with `Registry::LiveEntity(index)`. Under slot recycling a stale index can
  resolve to a *different* entity — a latent authority bug the new system must close (§9).

---

## 3. Three separated concerns (the core of v2)

The review's central point: one *struct* can be the schema for a local event and a wire
payload, but that must **not** mean every local event is automatically network-capable or
that dispatch and transport are the same thing. We separate three axes explicitly:

1. **Message schema** — typed struct + stable `MessageId` + field description. (§4)
2. **Dispatch policy** — `MessageScope` + `Kind` + delivery timing, governing *local*
   routing. (§5)
3. **Network policy** — per-message lane/reliability, governing *wire* routing. (§6)

```
        schema (what it is)          dispatch (how it routes locally)     transport (how it crosses the wire)
   struct + MessageId + Fields()  ──►  MessageScope / Kind / generation  ──►  lane + reliability (only if Wire)
```

### 3.1 `MessageScope` — keeps local events out of the wire ABI

```cpp
enum class MessageScope : uint8_t {
  LocalOnly,   // never serialized; e.g. KeyPressed. Not part of the wire ABI.
  Wire,        // travels client↔server; permanent network ABI.
  Control,     // session/handshake/clock — Wire, but on the control lane (§6).
  DebugOnly,   // dev builds only; stripped from release wire.
  Tooling,     // capture/trace/inspector streams; never gameplay.
};
```
A `static_assert` forbids serializing a `LocalOnly` message; `KeyPressed` *cannot* become a
forever-supported packet by accident.

### 3.2 `Kind` — command vs fact

```cpp
enum class MessageKind : uint8_t {
  Command,   // a REQUEST that may be rejected; must be validated; usually client→server.
  Event,     // a FACT that already happened; trusted when server-origin; observe-only.
};
```
This tells the inbound pipeline what to validate (commands are validated; server-origin
events are trusted) and tells handlers what they may assume.

---

## 4. The message definition (C++23, parameters on demand)

No C++26 reflection; flexible parameters come from a single field-description point plus
concept-constrained generic serialization.

### 4.1 A message describes its own fields once

```cpp
// NeuronCore/Messages/Defs/InputCommand.h  (illustrative)
struct InputCommand
{
  static constexpr MessageId    Id    = MessageId::InputCommand;
  static constexpr MessageScope Scope = MessageScope::Wire;
  static constexpr MessageKind  Kind  = MessageKind::Command;
  static constexpr Lane         Lane  = Lane::Unreliable;     // self-superseding
  static constexpr Direction    Dir   = Direction::ClientToServer;

  uint32_t  tick        = 0;      // sim-tick / client clock stamp (§9)
  uint32_t  sequence    = 0;      // latest-wins
  float     rollAxis    = 0.0f;
  float     pitchAxis   = 0.0f;
  float     throttle    = 0.0f;
  bool      fire        = false;
  bool      fireMissile = false;
  NetEntityId missileTarget{};    // generation-stamped reference (§9)

  constexpr auto Fields()       { return std::tie(tick, sequence, rollAxis, pitchAxis,
                                                  throttle, fire, fireMissile, missileTarget); }
  constexpr auto Fields() const { return std::tie(tick, sequence, rollAxis, pitchAxis,
                                                  throttle, fire, fireMissile, missileTarget); }
};
REGISTER_MESSAGE(InputCommand);   // §7 — adds it to the registry + duplicate-id CI
```

### 4.2 The generic codec (concept-constrained fold over the field tuple)

```cpp
template <typename T>
concept Message = requires (T t) {
  { T::Id } -> std::convertible_to<MessageId>;
  t.Fields();
};

inline void WriteField(Net::DataWriter& w, uint8_t v)  { w.WriteU8(v); }
inline void WriteField(Net::DataWriter& w, uint32_t v) { w.WriteU32(v); }
inline void WriteField(Net::DataWriter& w, float v)    { w.WriteF32(v); }
inline void WriteField(Net::DataWriter& w, bool v)     { w.WriteU8(v ? 1 : 0); }
inline void WriteField(Net::DataWriter& w, const std::string& v);   // len-prefixed, capped
inline void WriteField(Net::DataWriter& w, NetEntityId v);          // index+generation
// enums via std::to_underlying; Vector3i64; std::optional<T> (presence flag); fixed arrays…

template <Message M>
std::vector<uint8_t> Encode(const M& m)
{
  Net::DataWriter w;
  std::apply([&](auto&... fs){ (WriteField(w, fs), ...); }, m.Fields());
  return w.Bytes();
}

template <Message M>
bool Decode(Net::DataReader& r, M& out)
{
  std::apply([&](auto&... fs){ (ReadField(r, fs), ...); }, out.Fields());
  return r.Ok();   // sticky bounds-check; the record length (§6.1) bounds the reader
}
```

The fold inlines to exactly the `DataWriter` calls written by hand today (zero overhead),
and a `Fields()`/member mismatch is a compile error rather than silent wire drift.

### 4.3 "Parameters on demand"
- **Designated-initializer construction** — supply only what matters:
  `bus.Publish(Chat{ .sender = me, .text = "hi" });`
- **`std::optional<T>` fields** — presence-flagged (1 byte); attach a parameter only when
  relevant.
- *(Trailing-field forward-compat is **out of scope** under lockstep versioning — see the
  decision table. We do not rely on old readers tolerating new fields.)*

### 4.4 Serialization hardening (rec 9)
- Little-endian, fixed-width integers only (already the `DataWriter` contract).
- Enums serialized via explicit underlying type (`std::to_underlying`).
- Strings UTF-8, **length-prefixed and capped**; `std::vector`/array reads are **bounded**
  (mirror `GALAXY_NAME_MAX`) — no unbounded allocation from a hostile length.
- `bool` encoded as `u8`; never raw struct layout.
- **Float policy (resolved, §16 Q4):** raw IEEE-754 now for presentation/intent; world
  position is already `int64` (exact). **Rule:** any float feeding divergence-sensitive
  authoritative math is promoted to the codec's **fixed-point leaf** per-field *when
  identified* — not pre-emptively. Documented per field in the catalog.
- Never serialize pointers, platform/STL-layout-dependent types, or transient handles.
- Entity references use `NetEntityId` (§9), never a bare recyclable ECS index.

---

## 5. The in-process MessageBus (edges only)

Header-only, std-only, NeuronCore mechanism. **Used for edges, not core simulation.**

| Use the bus for | Keep as direct ECS/system work |
|---|---|
| inbound commands, network ingress/egress | per-tick combat resolution, motion integration |
| facts/notifications (`EntityDeath`, `Chat`, `TradeResult`) | per-projectile/per-stat-delta inner loops |
| UI / audio / VFX / camera triggers | anything that defines simulation order/determinism |

> **`Hit` is deliberately NOT a bus message.** Combat resolution stays the existing direct
> `StepCombat()`/`ResolvePlayerFire()` path; only its **batched outputs** (`EntityDeath`,
> optionally a coalesced `DamageApplied`) and the inbound `FireWeapon` *command* are messages.
> `Crime` is a fact the combat system emits, not a per-hit event. This keeps the hot path off
> the bus and simulation order explicit.

### 5.1 Dispatch model — generations within a tick (rec 8, refined)
- `Publish(msg)` enqueues; handlers run only during `Dispatch()`.
- `Dispatch()` drains in **generations**, all *within the same tick*: gen 0 = ingress +
  simulation outputs, gen 1 = their consequences, … capped at `MAX_GENERATIONS`. A handler
  that publishes is processed in the **next generation this tick**, never recursively
  mid-handler and never silently deferred to next frame. This resolves fire→death→despawn in
  one tick while keeping ordering deterministic and re-entrancy-safe.
- **Deterministic ordering key** (replay-ready, rec 8): within a generation, messages order
  by `(tick, sourceConnectionId, sequence, enqueueOrder)`. Defined now; the capture/replay
  harness that exploits it is a later phase.
- **Queue-overflow policy is explicit**, per scope: gameplay → warn + drop oldest with a
  counter; control → fatal (a dropped session message is unrecoverable). No silent drops.
- **Unsubscribe-during-dispatch** is safe (deferred removal); handlers get **subscription
  tokens** with defined lifetime.

### 5.2 Allocation & observability (rec 7, rec 10)
- API is designed so a **low/zero-allocation** implementation is possible later: small
  messages stored inline (small-buffer), large payloads behind explicitly owned buffers,
  queues frame-arena-backed. v1 may use `std::function`/type-erasure for cold handlers, but
  the **interface does not preclude** the arena path.
- Built-in **metrics**: per-`MessageId` counts, queue depth, dispatch time, drops,
  rate-limit rejections — feeding the tooling in §10.

### 5.3 Buses
One bus per side (server loop / client engine); identical type, different registered
handlers. This is how behaviour stays per-side while the mechanism is shared.

---

## 6. Wire binding — gameplay traffic *is* the catalog

### 6.1 Framing (rec 3 — record length is mandatory)
```
Envelope:  [ MAGIC 'NMSG' ][ PROTOCOL_VERSION u16 ][ lane u8 ][ records… ]
Record:    [ MessageId u16 ][ payloadLength u16 ][ payloadBytes… ]
```
- **`payloadLength` is mandatory** so a reliable packet can carry several messages and each
  decoder is bounded to its own record (the `DataReader` is constructed over exactly
  `payloadLength` bytes). This is decode-robustness, independent of versioning.
- `PROTOCOL_VERSION` is a single hard gate (lockstep): a mismatch drops the peer. No
  per-message negotiation.

### 6.2 Lanes (rec 6 — split now)
```cpp
enum class Lane : uint8_t {
  Control,      // reliable ordered, dedicated channel: handshake, AssignPlayer, clock, session
  Gameplay,     // reliable ordered: death, despawn, chat, station req/resp
  Bulk,         // reliable ordered, separate channel: galaxy manifest, station catalogs (cold/large)
  Unreliable,   // self-superseding sequenced latest-wins: InputCommand
};
```
- **Separate `ReliableChannel` instances per reliable lane**, so a large `Bulk` manifest
  cannot head-of-line-block a `Gameplay` death or a `Control` session message. `ReliableChannel`
  already exists; we instantiate a few keyed by lane and tag each packet's `lane` byte.
- Per-message **max payload** is declared and asserted; fragmentation is **disallowed except
  for approved `Bulk` messages**.
- The lane is a `static constexpr` on the message; the send path cannot misroute it.

### 6.3 Inbound pipeline — validate before publish (rec 5)
Untrusted bytes must never reach GameLogic directly:
```
1. parse envelope            (magic, version, lane)
2. validate packet/lane      (lane exists; reliable lanes drain their ReliableChannel)
3. decode record             (MessageId known? payloadLength within cap? Decode<T> Ok()?)
4. validate message:
     • direction valid for this peer (a client cannot send a server-origin Event)
     • allowed in current session state (e.g. nothing but handshake pre-login)
     • rate-limited (per-peer, per-MessageId)
     • authority: does this peer control/▾see the referenced NetEntityId? (§9)
     • no spoofing of another player/entity
5. publish to the SERVER bus
```
Direction/lane are also `static_assert`ed at **local** send sites — compile-time for our own
code, runtime for the network because a hostile client can emit any bytes.

### 6.4 The carve-out (rec 12, refined)
**All gameplay protocol traffic is cataloged messages.** Session/transport primitives
(handshake, clock-sync, ping, MTU, AssignPlayer) are cataloged messages on the **`Control`
lane**. The *only* things that may live **below** the catalog are primitives that must exist
*before* a decoder/session is trusted — encryption/auth negotiation and anti-cheat framing —
if and when those are added (§13). Raw snapshot packets remain their own format (§11).

---

## 7. Catalog & governance (rec 4 + decision: structs + registration macro)

The catalog is the **single source of truth** for the wire ABI and for tooling.

### 7.1 Authoring
Each message is an idiomatic struct (§4.1) plus one `REGISTER_MESSAGE(T)` that records, at
compile time *and* in a runtime registry: `Id`, name, `Scope`, `Kind`, `Lane`, `Direction`,
and field metadata (names/types/offsets, derived from `Fields()`). Tooling enumerates the
registry; no separate IDL to maintain.

### 7.2 `MessageId` is a permanent ABI
- **Never reuse** a value; retired ids stay **reserved** (commented, not deleted).
- **Subsystem-reserved ranges** with an invalid/reserved band:
  ```
  0x0000          Invalid (reserved)
  0x0001–0x00FF   core / session / control
  0x0100–0x01FF   input
  0x0200–0x02FF   replication control / lifecycle
  0x0300–0x03FF   chat / social
  0x0400–0x04FF   station / economy
  0x0F00–0x0FFF   debug / tooling
  0x1000–0x7FFF   game-specific extensions (wire)
  0x8000–0xFFFE   LocalOnly / Tooling (NEVER on the wire)
  0xFFFF          Invalid (reserved-max)
  ```
- **High bit = non-wire.** `MessageScope::LocalOnly`/`Tooling` ids set bit 15 (`>= 0x8000`),
  giving a one-line runtime assert: a `Wire` send of a high-bit id is a bug. Local events
  still get a stable id (for tracing/metrics) without consuming wire-ABI space.
- Each id documented with **owner + lifecycle**.
- **CI test** asserts no duplicate ids, that every `Wire` message has a lane + direction, and
  that no `LocalOnly`/`Tooling` id is `< 0x8000` (nor any `Wire` id `>= 0x8000`).

---

## 8. The initial catalog (folding today's protocols in)

| MessageId | Scope | Kind | Lane | Dir | Replaces |
|---|---|---|---|---|---|
| `InputCommand` | Wire | Command | Unreliable | C→S | `ClientInput` |
| `AssignPlayer` | Control | Event | Control | S→C | `EncodeAssignPlayer` |
| `GalaxyManifest` | Wire | Event | **Bulk** | S→C | `SendManifest` |
| `EntityDespawn` | Wire | Event | Gameplay | S→C | `EncodeDespawn` |
| `EntityDeath` | Wire | Event | Gameplay | S→C | `EncodeDeath` |
| `Chat` | Wire | Event | Gameplay | both | `EncodeChat` |
| `StationRequest` | Wire | Command | Gameplay | C→S | `StationProtocol` req |
| `StationResponse` | Wire | Event | Gameplay | S→C | `StationProtocol` resp |
| `FireWeapon` | Wire | Command | Unreliable\* | C→S | inline `in.fire`/`in.fireMissile` |
| `EntitySpawned` | Wire | Event | Gameplay | S→C | (new lifecycle) |
| `KeyPressed` / `ActionTriggered` | **LocalOnly** | Command | — | — | (new, client-local) |
| `Crime`, `DamageApplied` | LocalOnly\*\* | Event | — | — | inline crime/kill handling |

\* `FireWeapon` may stay folded into `InputCommand`'s fire bits initially; broken out only if
weapons grow parameters. \*\* server-internal facts; promoted to `Wire` only if a client needs
them (e.g. damage numbers).

---

## 9. Entity references, time, and identity (new — codebase-specific)

- **`NetEntityId{ uint32_t index; uint32_t generation; }`** is a first-class serializable
  field type. Client-originated references (e.g. `InputCommand::missileTarget`) carry the
  generation the client last saw; the server validates `index→generation` against the live
  `Registry` (extending today's `LiveEntity`) and **rejects stale/foreign references** in the
  inbound validation step (§6.3). Closes the recycle-aliasing hole.
- **Authority rule (resolves review Q5):** a client may reference **only entities it controls
  or currently sees in its AOI**. Arbitrary-entity references are rejected pre-publish.
- **Tick/clock stamping:** gameplay messages carry a `tick` (sim-tick / client clock). Used
  now for latest-wins + the deterministic ordering key, and ready for lag-compensation and
  the snapshot↔event race reconciliation (§11) without a later wire change.
- **Idempotent client handlers:** reliable delivery is exactly-once, but death-VFX,
  inventory-grant, etc. are written idempotently so replay or a future at-least-once lane
  can't double-apply.

---

## 10. Tooling & observability (rec 10 — enabled by the registry)
- **Packet-dump decoder** that reads the registry and prints any `'NMSG'` capture **without
  the game executable** (the registry is std-only and linkable standalone).
- **Message/packet trace capture** and **input/message replay** (built on §5.1's ordering
  key) for desync analysis.
- **Bandwidth per `MessageId`**, **CPU per handler**, queue-depth graphs, drop/rate-limit
  counters (from §5.2 metrics).
- **Schema-doc generator** from the catalog, and a **catalog-diff/compat report** between two
  builds (even under lockstep, useful to *see* what changed and force the version bump).

---

## 11. Snapshots stay separate (rec 11)
- Messages = discrete commands/events (observed exactly once). Snapshots = current replicated
  state. **Never** stream high-frequency state over reliable messages; **never** carry an
  irreversible once-only event in a snapshot.
- **Defined race handling**, with tests:
  - `EntityDeath` before a snapshot that still contains the entity → client suppresses the
    entity (today's `Forget`) and reconciles.
  - Snapshot drops the entity before `EntityDeath` arrives → client still plays/skips VFX per
    policy (the death is the authoritative VFX trigger).
  - Add explicit event/snapshot race tests alongside the existing transport tests.

---

## 12. MessageBus & codec — file layout (NeuronCore, mechanism only)
```
NeuronCore/Messages/
  MessageId.h        // enum class MessageId : uint16_t  + reserved-range constants
  MessageTraits.h    // MessageScope, MessageKind, Lane, Direction
  NetEntityId.h      // generation-stamped entity reference + leaf codec
  Serialize.h        // Message concept + WriteField/ReadField leaves + Encode/Decode
  Registry.h         // REGISTER_MESSAGE, runtime/compile-time catalog, duplicate-id check
  MessageBus.h       // Subscribe (token)/Publish/Dispatch (generations)/metrics
  Framing.h          // 'NMSG' envelope + record (length-prefixed) read/write, lane routing
  Defs/              // one header per message struct (InputCommand.h, EntityDeath.h, …)
Tests/NeuronCore/
  MessageCodecTests.cpp     // encode→decode + golden bytes per message/version
  MessageBusTests.cpp       // generations, ordering key, overflow policy, unsubscribe-in-dispatch
  MessageWireTests.cpp      // multi-record framing, per-lane ReliableChannel round-trip under loss
  MessageValidationTests.cpp// direction/state/rate/authority rejection; malformed/oversized fuzz
  CatalogGovernanceTests.cpp// no duplicate ids; every Wire msg has lane+direction; LocalOnly unserializable
```
Handlers (behaviour) live **outside** NeuronCore.

---

## 13. Security posture (resolved — Q3)
**Now:** the inbound **validation + per-peer rate-limiting** pipeline (§6.3) and authority
checks (§9). These are gameplay-integrity essentials, cheap, and protect the authoritative
sim from a buggy/hostile client — so they ship from Phase 2 (the moment untrusted bytes first
reach the server).
**Deferred (hooks reserved, not built):** encryption, authentication, connection tokens,
replay-protection nonces, anti-spoofing crypto. Rationale: the project is pre-account /
pre-persistence (NeuronServer's MS SQL layer is still a placeholder) and lockstep, so transport
crypto/auth is premature — there is no identity to protect yet. The framing reserves a
fixed-size **pre-catalog security envelope** slot (§6.4) so these slot in *below* the message
layer later **without touching the catalog**. Revisit when accounts/persistence land.

---

## 14. Phased rollout (each phase builds, tests green, game runnable)

**Phase 0 — Foundations (NeuronCore, no behaviour change). ✅ DONE.**
`MessageId` + reserved ranges, `MessageTraits`, `NetEntityId`, `Serialize` (concept + leaves
+ caps), `Registry`/`REGISTER_MESSAGE`, `MessageBus` (generations + overflow policy + metrics),
`Framing` (length-prefixed records + lanes). Shipped under `NeuronCore/Messages/` with codec/
bus/wire/governance suites in `Tests/NeuronCore/` (27 cases). Nothing wired into the game yet.

**Phase 1 — In-process bus on the server. ✅ DONE.**
Introduced `FireWeapon` (command) and `Crime` / `EntityKilled` (facts) in
`GameLogic/CombatMessages.h` (LocalOnly server messages; the wire `EntityDeath` stays the
existing `GameEvents` broadcast until the wire-folding phases). `Hit`/`DamageApplied` stay
off the bus by design — combat geometry is direct ECS work. `ResolveFireWeapon` delegates to
the unchanged `ResolvePlayerFire` / `SpawnMissile` and only publishes facts; police dispatch,
death broadcast/respawn and logging live in the `Server/Main.cpp` subscribers. The old
`applyShot` lambda and the per-kill loop body are gone — one `EntityKilled` subscriber unifies
the laser-kill and tick-kill paths. **Combat math unchanged.** Covered by
`Tests/GameLogic/CombatMessagesTests.cpp` (5 cases); verified locally on clang++ 18 / g++ 13.

**Phase 2 — Unreliable lane (`InputCommand`). ✅ DONE.**
`InputCommand` is now a catalog message (`NeuronCore/Messages/Defs/InputCommand.h`, Wire /
Command / Unreliable / C→S, registered via `REGISTER_MESSAGE`), carried on the `'NMSG'`
unreliable lane. `Net::ClientInput` is a thin alias of it, so the server `OnInput` and the
client input builder are unchanged. `ReplicationClient::SendInput` uses `PacketWriter`; the
server ingress decodes `InputCommand` on the Unreliable lane with id/decode/direction checks
(stale/dup still dropped by `OnInput`'s sequence). The bespoke `WriteInput`/`ReadInput` and the
`'NCMD'` magic/version are deleted — no transition window needed under lockstep. A golden
test proves the `InputCommand` payload is **byte-identical to the legacy `'NCMD'` field
layout**; `missileTarget` stays a bare index (NetEntityId promotion is a later step). Verified
locally on clang++ 18 / g++ 13 (38 message/input/combat cases). *Note: per-peer rate-limiting
and authority checks land with the reliable lane in Phase 3, when sessions carry more state.*

**Phase 3a — Fold reliable schemas into the catalog. ✅ DONE.**
`AssignPlayer`, `EntityDespawn`, `EntityDeath`, `Chat` are catalog messages in
`Messages/Defs/CoreEvents.h`; `StationRequest`/`StationResponse` became catalog messages in
`StationProtocol.h` (structs aliased into `Net::` so GameLogic/client call sites are unchanged).
A generic `Msg::SendReliable` / `Msg::TryDecode` carries them over the existing `ReliableChannel`
(type tag = `MessageId`, payload = generic `Encode`). All call sites migrated — `ServerSessions`
(AssignPlayer), `Server/Main` (death/despawn broadcast + station loop), `ReplicationClient`
(AssignPlayer decode + station send), client `main` (`process_server_events`). The bespoke
`Encode*`/`Decode*`/`Send*` in `GameEvents.h`/`StationProtocol.h` are deleted; golden tests prove
each payload is **byte-identical to the legacy layout**. Verified locally on clang++ 18 / g++ 13
(63 headless cases). *Deferred: per-peer rate-limiting/authority still pending (3b/later).*

**Phase 3b-1 — Manifest onto a catalog id; retire `EventType`. ✅ DONE.**
`GalaxyManifest` chunks now carry a reserved catalog `MessageId` (`Net::GALAXY_MANIFEST_ID`, Bulk
lane) instead of `EventType::GalaxyManifest`; the bespoke chunked encode stays (fixed `char[]`).
`GameEvents.h` and the `EventType` enum are **deleted** — every reliable datagram is identified by
a `MessageId`. Verified on 63 headless cases (clang++ 18 / g++ 13).

**Phase 3b-2 — Physical lane split. ✅ DONE.**
`Msg::MessageEndpoint` (NeuronCore, header-only) gives each peer one `ReliableChannel` per reliable
lane (Control/Gameplay/Bulk). Each lane's datagram is `[RELIABLE_MAGIC 'NRLB'][lane]` + that lane's
packet, so `ReliableChannel` is unchanged. `Send(msg)` routes by the message's `Lane` trait; inbound
datagrams route by the lane byte; `Receive` drains Control→Gameplay→Bulk so gameplay/control never
wait behind a bulk backlog; a lane emits a datagram only when it has something to (re)send or a new
ack to deliver (idle lanes silent). Wired into `ServerSessions` (Session owns a `MessageEndpoint`;
`AssignPlayer`→Control, manifest→Bulk, `Broadcast` routes by lane) and `ReplicationClient` (`m_events`
is an endpoint; `RELIABLE_MAGIC` routing both ends). The galaxy manifest now rides **Bulk**, so a
manifest backlog can't head-of-line-block a death. Headless `MessageEndpointTests` (6 cases incl. HOL
isolation + lossy-bulk) plus the existing channel/session tests cover it; 69 headless cases pass on
clang++ 18 / g++ 13. *(Moving the client `PollEvent`/switch onto bus subscriptions stays with the
Phase 4 client-bus work.)*

**Phase 3 is complete: every client↔server datagram is a catalog message on a typed lane.**

**Phase 4a — Client bus & presentation. ✅ DONE.**
The client now owns a `Msg::MessageBus` (`DeepspaceOutpost/main.cpp`), mirroring the server.
`process_server_events` decodes each inbound reliable fact to its catalog type and **publishes**
it onto the bus; independent subscribers react — commerce (`StationResponse`→commander),
death (`EntityDeath`→game-over for self, else forget + explosion), despawn (`EntityDespawn`→
forget). New presentation reactions (camera shake, kill feed, …) just `Subscribe<>` instead of
editing one switch — this is the "subscribe VFX/audio/camera/UI to inbound facts" capability.
The decode→publish→dispatch→subscriber path is covered headlessly by the bus + reliable-event
tests; the client wiring builds under MSVC (CI).

**Phase 4b — Key → `InputCommand` mapper. ✅ DONE.**
`ActionTriggered` (`Messages/Defs/InputActions.h`, LocalOnly, `Fire`/`LaunchMissile`) is the
client-local input-intent message. `send_player_input` publishes it from the polled keys
(`kbd_fire_pressed`, the missile-launch edge), a command-builder subscriber accumulates the
frame's actions, and the send folds them into the outgoing `InputCommand` — the
"key polling → `ActionTriggered` (LocalOnly) → command-builder → `InputCommand`" path, end to
end, decoupling *what the player did* from *how the command is built* (rebinding/record/replay
all drive the same path). Continuous flight (roll/pitch/throttle) stays the legacy rate-based
`PlayerFlight` state, normalized to axes at send time — it isn't key-event-shaped. Behaviour is
unchanged. Covered by `InputActionsTests` (codec + command-builder + LocalOnly/non-wire checks).

**Phase 5 — Tooling & hardening.**
Standalone packet-dump decoder, schema-doc + catalog-diff generators, message/bandwidth/CPU
metrics surfaced, malformed-packet fuzz + event/snapshot race tests, 100-bot trace run.
Collapse `PeekMagic` to one branch; update `AGENTS.md`/roadmap.

---

## 15. Commercial-scalability checklist (gate before committing the design)
- [ ] Reject malicious oversized strings/vectors without allocation spikes (§4.4).
- [ ] Rate-limit spammy clients before GameLogic sees messages (§6.3).
- [ ] Profile CPU per message type and per handler; bandwidth per message type (§5.2/§10).
- [ ] Replay a captured input/message stream via the deterministic ordering key (§5.1).
- [ ] Run 100 bot clients with message tracing enabled (§14 Phase 5).
- [ ] Fuzz malformed/oversized packets (§12 tests).
- [ ] Retire a `MessageId` without reuse; duplicate-id CI catches mistakes (§7.2).
- [ ] Multiple reliable lanes prevent bulk HOL-blocking gameplay/control (§6.2).
- [ ] `LocalOnly` events never enter the wire catalog (§3.1).
- [ ] Client-originated entity refs validated for authority/visibility (§9).

---

## 16. Resolved decisions (were the open questions)

**Q1 — One catalog vs separate local/wire catalogs → ONE catalog.**
Keep a single registry with `MessageScope` separating local from wire, reinforced by the
**high-bit id rule** (§7.2): `LocalOnly`/`Tooling` ids are `>= 0x8000` and can never serialize
onto the wire. One catalog means one registry for the standalone decoder, schema export,
compat-diff, duplicate-id CI, and metrics — splitting would duplicate all of that for no real
isolation gain, since scope + high-bit already give hard separation. Revisit only if local
types balloon into the hundreds, at which point they move to a sub-range/sub-namespace without
changing the mechanism.

**Q2 — `DamageApplied` to the client → NO; keep it server-internal (LocalOnly).**
Verified: `Net::EntitySnapshot` replicates position/orientation/speed/type but **not** health,
so the client has no authoritative health today and no HUD that needs per-hit numbers (Elite
lineage shows energy/shield *state*, not floating damage). `DamageApplied` stays a `LocalOnly`
server fact driving crime/death. **When** hit feedback is wanted, prefer **adding a health byte
to `EntitySnapshot`** (self-superseding state — matches the snapshot-for-state /
message-for-events split) over a per-hit reliable message. Reserve a `Wire` `DamageApplied`
`Event` (Gameplay lane) only for feedback that is inherently event-shaped — a one-shot "you
took N from X" toast a snapshot can't express. Reversible either way.

**Q3 — Security timing → validation + rate-limiting now; crypto/auth deferred.**
Resolved in §13: ship the inbound validation/rate-limit/authority pipeline from Phase 2;
defer encryption/auth/tokens/replay-protection (hooks reserved in the framing) until accounts
and persistence exist. This is the one most open to override — say the word to pull crypto/auth
into near-term scope.

**Q4 — Float quantization → raw IEEE-754 now; quantize per-field on demand.**
All peers are same-binary Windows/MSVC under lockstep, so identical-code float ops are
bit-reproducible for same-version replay — raw float is deterministic enough now. The one value
that *must* be exact, world position, is already `int64` (no FP). Intent axes (roll/pitch/
throttle ∈ [-1,1]) and presentation floats stay raw. The codec keeps a **fixed-point leaf**, so
if a specific gameplay float is ever shown to drive authoritative divergence (cross-compiler
replay, floating-origin math), it's promoted to a fixed-point wire type per-field — a localized
change, not a redesign. Rule recorded in §4.4: *any float feeding divergence-sensitive
authoritative math is quantized when identified.*

---

## 17. Summary
A **message** is the currency of *edge* interactions: a typed C++23 struct that describes its
fields once, is auto-serialized, carries explicit **scope / kind / lane / direction** traits,
and is registered into a single catalog that is the network ABI *and* the source for tooling.
Local events (`LocalOnly`) never reach the wire; gameplay wire traffic is validated before it
touches GameLogic; reliable traffic is split across control/gameplay/bulk lanes; dispatch is
deterministic and replay-ready. Hot simulation stays direct ECS work — the bus decouples the
edges, not the inner loop. The existing hand-rolled protocols fold in as the first catalog
entries and are deleted as parity is proven, leaving **one** mechanism for input, lifecycle,
combat edges, trade, and chat — with the headroom to grow into a commercial-quality engine
messaging layer rather than a convenient refactor.
