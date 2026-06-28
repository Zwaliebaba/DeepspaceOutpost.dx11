# Message.md — A Unified Message System for NeuronCore

> **Status:** Design / plan. No code yet. This document works out the concept and the
> phased path to land it. It reflects the three decisions taken up front:
> 1. **Unified bus + wire** — one message concept drives *both* an in-process pub/sub
>    bus (system → system, e.g. *shoot → hit → death*) *and* the client ↔ server wire
>    protocol, the wire layer built on the same message definitions.
> 2. **Fold in / replace incrementally** — `ClientInput`, `GameEvents`, and
>    `StationProtocol` become the first entries in the new message catalog and their
>    bespoke encode/decode is deleted once parity is proven.
> 3. **Typed structs + auto-serialize** — each message is a strongly-typed C++23 struct;
>    a concept-constrained templated serializer derives wire encode/decode from a single
>    field description, so no per-message `DataWriter` calls are hand-written.

---

## 1. Goals & non-goals

### Goals
- **One mechanism.** A single way to express "X happened / do Y" that works the same
  whether the sender and receiver are two systems in the same process or two machines
  across the network.
- **Drive interactions through messages.** Key presses, shots, hits, deaths, despawns,
  docking, trades, chat, lifecycle — all become messages, decoupling producers from
  consumers. Systems stop calling each other directly; they publish and subscribe.
- **All client ↔ server traffic is messages.** The wire protocol *is* the message
  catalog serialized. There is no second, hand-written network format.
- **Flexible parameters on demand (C++23).** A message struct declares its fields once;
  the serializer is generated from that. Optional/extension fields can be attached
  without touching the wire of existing messages.
- **Zero behaviour in NeuronCore.** The bus and the codec are *mechanism* — schema and
  plumbing only — so NeuronCore stays "shared data/protocol only" (see `AGENTS.md`).
  Behaviour (what a death *does*) stays per-side: server handlers in `GameLogic`,
  presentation handlers in `NeuronClient`/`DeepspaceOutpost`.
- **Deterministic & testable.** Pure, socket-free core (bytes in / bytes out, in-memory
  dispatch) so loss, ordering, and handler wiring are all unit-testable headlessly —
  matching how `ReliableChannel` is tested today.

### Non-goals
- Not a general actor framework or coroutine scheduler.
- Not replacing the **snapshot/replication** stream. Bulk entity state stays
  self-superseding snapshots (`SnapshotPacketizer`/`SnapshotInterpolator`); messages are
  for the things a snapshot can *never* express by superseding state (a kill, a key
  press, a trade) — exactly the line `ReliableChannel.h` already draws.
- Not a new transport. We reuse the existing raw-UDP socket, the `ReliableChannel`
  (seq/ack/resend) for must-arrive messages, and the unreliable datagram path for
  self-superseding ones.
- No third-party libraries (native-first; std-only core, consistent with the repo rule).

---

## 2. Where we are today (the landscape this replaces)

| Concern | Today | Lives in |
|---|---|---|
| Client → server intent | `ClientInput` struct + `WriteInput`/`ReadInput`, unreliable, "latest seq wins" | `NeuronCore/ClientInput.h` |
| Reliable events | `EventType` (u16) + per-event `Encode*`/`Decode*`/`Send*` (Despawn, Death, Chat, AssignPlayer, …) | `NeuronCore/GameEvents.h` |
| Docked request/response | `StationRequest`/`StationResponse` structs + encode/decode, ride `ReliableChannel` | `NeuronCore/StationProtocol.h` |
| Reliable transport | `ReliableChannel` (seq/ack/resend/ordered), magic `'NEVT'` | `NeuronCore/ReliableChannel.h` |
| Byte codec | `DataWriter` / `DataReader` (little-endian, bounds-checked) | `NeuronCore/DataWriter.h`, `DataReader.h` |
| In-process events | **None.** Systems call each other directly. `Server/Main.cpp` inlines fire→crime→death→despawn; `StepCombat()` returns `std::vector<Kill>` the main loop hand-processes. | `Server/Main.cpp`, `GameLogic/*` |
| Client consumption | `ReplicationClient::PollEvent()` returns raw `ReliableMessage`; `main.cpp` switch-decodes each `EventType` | `NeuronClient/ReplicationClient.*`, `DeepspaceOutpost/main.cpp` |

**Observations that shape the design**
- The reliable side already has the right shape: a `ReliableMessage { uint16_t type;
  vector<uint8_t> payload; }`. The new system generalises `type` into a **message id**
  and replaces hand-written `Encode*`/`Decode*` with generated codecs.
- The unreliable side (`ClientInput`) has its own magic/version. We unify this under one
  framing so *any* message can be sent reliably or unreliably.
- The server loop is a **fixed ~30 Hz tick** that drains datagrams → applies → simulates
  → broadcasts. That cadence is the natural place to **drain and dispatch** queued
  messages. The bus is therefore **queued/deferred by default**, not immediate-recursive.
- There is no thread-safety requirement *inside* a tick today (single-threaded sim loop),
  but `TasksCore` exists and the roadmap anticipates worker threads — so the queue design
  leaves room for a thread-safe ingress without forcing it now.

---

## 3. Core concept

A **Message** is an immutable, strongly-typed value describing *something that happened*
or *something requested*. Every message type has:

- a **stable `MessageId`** (u16) — its identity on the wire and in the catalog;
- a **field description** — declared once on the struct, used to generate the codec;
- **delivery traits** — reliability and direction (compile-time constants on the type).

The same struct is the in-process event *and* the wire payload. Publishing locally and
sending to a peer differ only in the **sink**: a local `MessageBus` vs a network channel.

```
              publish(msg)                       send(msg)
  Producer ───────────────► MessageBus ◄──────── network ingress (UDP)
  (system / input / UI)         │                        ▲
                                ▼                        │ WritePacket / datagram
                          Subscribers                network egress
                       (handlers per side)
```

The unifying idea: **a network endpoint is just another subscriber/publisher.** Inbound
datagrams are decoded into messages and published onto the local bus; outbound messages
addressed to a peer are serialized into that peer's channel. Local code never cares which
side of the wire a message came from — it subscribes to a type and reacts.

---

## 4. The message definition (C++23 "parameters on demand")

We do **not** have C++26 static reflection. The flexible-parameters requirement is met
with a **single field-description point per message** plus concept-constrained generic
serialization, fold expressions, and optional/extension fields.

### 4.1 A message is a struct that describes its own fields

Each message struct exposes its serializable fields *once*, as a tuple of references via a
`Fields()` member. The serializer walks that tuple; nothing else is hand-written.

```cpp
// NeuronCore/Messages/InputCommand.h  (illustrative)
namespace Neuron::Msg
{
  struct InputCommand
  {
    static constexpr MessageId   Id        = MessageId::InputCommand;
    static constexpr Reliability kDelivery = Reliability::Unreliable; // self-superseding
    static constexpr Direction   kDir      = Direction::ClientToServer;

    uint32_t sequence    = 0;
    float    rollAxis    = 0.0f;
    float    pitchAxis   = 0.0f;
    float    throttle    = 0.0f;
    bool     fire        = false;
    bool     fireMissile = false;
    uint32_t missileTarget = NO_MISSILE_TARGET;

    // The ONE description point. Order defines wire order. Add a field here and the
    // codec follows automatically.
    constexpr auto Fields() { return std::tie(sequence, rollAxis, pitchAxis,
                                              throttle, fire, fireMissile, missileTarget); }
    constexpr auto Fields() const { return std::tie(sequence, rollAxis, pitchAxis,
                                                     throttle, fire, fireMissile, missileTarget); }
  };
}
```

### 4.2 The generic codec (concept-constrained, fold over the field tuple)

```cpp
// NeuronCore/Messages/Serialize.h  (illustrative)
namespace Neuron::Msg
{
  // A type is a Message if it has a MessageId and a Fields() tuple.
  template <typename T>
  concept Message = requires (T t) {
    { T::Id } -> std::convertible_to<MessageId>;
    t.Fields();
  };

  // Leaf writers: one overload per primitive the wire understands. Reuses DataWriter.
  inline void WriteField(Net::DataWriter& w, uint8_t  v) { w.WriteU8(v); }
  inline void WriteField(Net::DataWriter& w, uint16_t v) { w.WriteU16(v); }
  inline void WriteField(Net::DataWriter& w, uint32_t v) { w.WriteU32(v); }
  inline void WriteField(Net::DataWriter& w, int32_t  v) { w.WriteI32(v); }
  inline void WriteField(Net::DataWriter& w, int64_t  v) { w.WriteI64(v); }
  inline void WriteField(Net::DataWriter& w, float    v) { w.WriteF32(v); }
  inline void WriteField(Net::DataWriter& w, bool     v) { w.WriteU8(v ? 1 : 0); }
  inline void WriteField(Net::DataWriter& w, const std::string& v) { /* len-prefixed */ }
  // … Vector3i64, EntityRef (index), enums (as underlying), std::optional<T>, etc.

  template <Message M>
  std::vector<uint8_t> Encode(const M& m)
  {
    Net::DataWriter w;
    std::apply([&](auto&... fs){ (WriteField(w, fs), ...); }, m.Fields()); // fold
    return w.Bytes();
  }

  template <Message M>
  bool Decode(Net::DataReader& r, M& out)
  {
    std::apply([&](auto&... fs){ (ReadField(r, fs), ...); }, out.Fields()); // fold
    return r.Ok();   // sticky bounds-check, exactly like today
  }
}
```

The serializer is **zero-overhead** (folds inline to the same `DataWriter` calls written
by hand today) and **type-safe** (a missing/extra field is a compile error in `Fields()`,
not a silent wire-format drift). `Vector3i64`, enums, `std::optional<T>`, fixed arrays,
and nested messages get leaf overloads so any field type "just works".

### 4.3 "Parameters on demand"

Three complementary mechanisms, all C++23-ergonomic:

1. **Designated-initializer construction** — supply only the parameters you care about,
   the rest defaulted:
   ```cpp
   bus.Publish(ChatMessage{ .senderId = me, .text = "hi" });
   bus.Publish(FireWeapon{ .shooter = me, .weapon = Weapon::Missile, .target = locked });
   ```
2. **Optional fields** (`std::optional<T>`) — encoded behind a 1-byte presence flag, so a
   message can carry a parameter *only when relevant* without breaking the wire for those
   that omit it. Adding a new trailing optional field is backward-compatible.
3. **Extension blocks** — a message may end with an optional `Extensions` map
   (id → bytes) for rare/forward-compat parameters that the receiver can ignore if it
   doesn't recognise them. Reserved for cold-path/debug use; hot messages stay fixed-layout.

> This keeps the *common* case compact and fixed-width (good for the hot path) while
> letting messages grow parameters "on demand" without a flag-day wire break.

### 4.4 Versioning

A single envelope `PROTOCOL_VERSION` (replacing the per-schema `INPUT_VERSION` /
`EVENT_VERSION`) is checked at the framing layer. Individual messages evolve by
*appending* fields (old readers stop at their known field count; trailing optionals are
presence-flagged), so most additions need no version bump. A bump is reserved for an
incompatible reshaping of an existing field.

---

## 5. The in-process MessageBus

A lightweight, header-only, std-only dispatcher in NeuronCore (mechanism, no behaviour).

### 5.1 Model
- **Typed subscribe:** `bus.Subscribe<Death>([](const Death& d){ … })` registers a handler
  keyed by `Death::Id`.
- **Typed publish:** `bus.Publish(Death{ .victim = v, .killer = k })` enqueues the message.
- **Queued/deferred dispatch:** `Publish` appends to an internal queue; `bus.Dispatch()`
  drains it and invokes handlers. This is called once per server tick and once per client
  frame, so message effects are ordered and re-entrancy-safe (a handler that publishes is
  processed on the next drain, not recursively mid-handler). A queue-depth guard bounds a
  publish storm.
- **Immediate option:** `bus.Emit(msg)` dispatches synchronously for the rare case that
  needs it (e.g. a query-like request inside one system). Default is queued.

```cpp
// NeuronCore/Messages/MessageBus.h  (illustrative)
namespace Neuron::Msg
{
  class MessageBus
  {
  public:
    template <Message M> void Subscribe(std::function<void(const M&)> fn);
    template <Message M> void Publish(const M& m);   // enqueue (typed → type-erased)
    void Dispatch();                                  // drain queue → handlers
  private:
    // MessageId → list<handler>; queue of (MessageId, erased payload).
  };
}
```

### 5.2 Where buses live
- **Server:** one bus owned by the server loop (or `ServerSessions`). Systems publish
  domain events (`Death`, `Despawn`, `Crime`, `TradeResult`); the loop's handlers turn the
  outbound-to-clients ones into channel sends.
- **Client:** one bus owned by the client engine. Input publishes `InputCommand`; the
  network ingress publishes decoded inbound messages (`Death`, `StationResponse`, …); UI
  / audio / VFX / camera subscribe. (Roadmap row: client-side explosion VFX simply
  subscribes to `Death` — no server entity needed.)
- The bus type is identical on both sides; only the registered handlers differ. This is
  how "behaviour stays per-side" is preserved while the *mechanism* is shared.

---

## 6. The wire binding — every client ↔ server byte is a message

### 6.1 Unified framing
One envelope replaces the three magics (`'NCMD'`, `'NEVT'`, and the bespoke input header):

```
[ MAGIC 'NMSG' ][ PROTOCOL_VERSION u16 ][ channel u8 ][ payload … ]
```

- **channel** selects the delivery lane:
  - `Reliable` → the payload is one or more `(MessageId, len, bytes)` records carried by
    `ReliableChannel` (seq/ack/resend/ordered) — unchanged transport, generalised content.
  - `Unreliable` → a single self-superseding message (e.g. `InputCommand`), latest-seq-wins
    just like `ClientInput` today.
- The existing `PeekMagic` switch in `Server/Main.cpp` collapses to a single `'NMSG'`
  branch that routes by `channel`, then decodes by `MessageId`.

### 6.2 Delivery traits are on the type
`Reliability` and `Direction` are `static constexpr` on each message (see §4.1), so the
send path *cannot* put a self-superseding message on the reliable lane by mistake, and a
client cannot emit a server-only message. A `static_assert` enforces direction at the
send call site.

### 6.3 Routing tables (catalog-driven)
A central **catalog** (`Messages/Catalog.h`) lists every message type once. From it we
generate, at compile time:
- `MessageId` ↔ type mapping for decode dispatch (`switch (id)` → `Decode<T>` → `Publish`),
- the reliable/unreliable classification,
- a golden **wire-format test** per message (encode → decode → compare), mirroring the
  existing `SnapshotTransportTests` / `StationProtocolTests` style.

### 6.4 Inbound pipeline (server)
```
recvfrom → Peek 'NMSG' → ReadPacket (reliable lane drains ReliableChannel)
        → for each (id,bytes): Decode<T> → bus.Publish(T)        ← network becomes a publisher
tick: bus.Dispatch()  → session/GameLogic handlers apply intent, resolve combat, etc.
      systems Publish outbound messages (Death, Despawn, StationResponse, AssignPlayer)
egress: outbound handler routes each to the addressed session's Reliable/Unreliable lane
```
Client mirrors this: input handler publishes `InputCommand` (unreliable egress), ingress
publishes decoded inbound onto the bus, presentation subscribes.

---

## 7. The initial message catalog (folding in today's protocols)

| MessageId | Struct | Replaces | Lane | Dir |
|---|---|---|---|---|
| `InputCommand` | flight intent + fire/missile | `ClientInput` (`WriteInput`/`ReadInput`) | Unreliable | C→S |
| `AssignPlayer` | controlled entity id | `EncodeAssignPlayer` | Reliable | S→C |
| `EntityDespawn` | entity id | `EncodeDespawn` | Reliable | S→C |
| `EntityDeath` | victim + killer | `EncodeDeath` | Reliable | S→C |
| `Chat` | sender + text | `EncodeChat` | Reliable | both |
| `GalaxyManifest` | chunk of system list | `SendManifest` | Reliable | S→C |
| `StationRequest` | dock/undock/buy/sell/equip/teleport | `StationProtocol` req | Reliable | C→S |
| `StationResponse` | authoritative result | `StationProtocol` resp | Reliable | S→C |

**New domain (in-process first, some later wire) messages this unlocks**
- `FireWeapon{ shooter, weapon, target }` — published by input/AI; combat subscribes.
- `Hit{ attacker, victim, weapon, damage }` — published by combat resolution; drives death,
  crime flagging, and (client) hit VFX/sound on its own subscription.
- `Crime{ offender, victimTeam }` — replaces the inline crime block in `Server/Main.cpp`;
  the spawn director subscribes to dispatch police.
- `EntitySpawned{ id, type }` — lifecycle, complements `EntityDespawn`.
- `KeyPressed` / `ActionTriggered` — client-local input events (see §8.1) that the input
  mapper turns into `InputCommand` / `StationRequest`, decoupling key polling from intent.

---

## 8. Worked examples

### 8.1 A key press
1. The platform layer polls the keyboard (today `kbd_poll_keyboard` sets `kbd_*` flags).
2. An **input mapper** publishes local intent messages instead of code reading globals:
   `bus.Publish(KeyPressed{ .key = Key::Fire })` (or directly the higher-level
   `FireWeapon`). Key-state → semantic action lives in one subscriber, not scattered `if`s.
3. The **command builder** (subscriber) accumulates the frame's intents into one
   `InputCommand` and publishes it for **unreliable egress** to the server.
4. Server ingress decodes `InputCommand` → `bus.Publish` → `ServerSessions` handler applies
   it to the player's `FlightIntent` (today's `OnInput` body becomes a handler).

> Net effect: the chain from physical key → wire intent is a series of messages, so
> rebinding, recording/replaying input, or a headless bot driving the same `InputCommand`
> are all the same path (the `BotClient` already wants this).

### 8.2 One ship shoots, another registers a hit
Today this is inlined in `Server/Main.cpp` (`applyShot` lambda) and `StepCombat()` returns
`Kill` vectors. Reworked through messages, server-side:

1. Input/AI publishes `FireWeapon{ shooter, weapon, target }`.
2. The **combat system** subscribes, runs the existing geometry (`ResolvePlayerFire` /
   `StepCombat` math is unchanged — only its *outputs* become messages), and for each
   resolved strike publishes `Hit{ attacker, victim, weapon, damage }`.
3. Subscribers react independently — the decoupling the user asked for:
   - **Damage handler** applies energy loss; if it reaches zero it publishes
     `EntityDeath{ victim, killer }`.
   - **Crime handler** sees a `Hit` on a `Station`/`Police` victim and publishes `Crime`,
     which the spawn director turns into dispatched police (replacing the inline block).
   - **Death handler** destroys the entity (or respawns a player) and the outbound router
     sends `EntityDeath` reliably to every client in AOI.
4. On the **client**, ingress publishes the inbound `EntityDeath`; the VFX subscriber spawns
   an explosion, the audio subscriber plays the boom, and `ReplicationClient::Forget`
   drops the ghost — each an independent subscriber to the same message.

The combat *math* is untouched; what changes is that its results travel as messages, so
crime, death, VFX, audio, and despawn are no longer one tangled function.

---

## 9. Threading & ordering

- **Single-threaded today:** server tick and client frame each call `bus.Dispatch()` once;
  ordering is FIFO within a drain. Deterministic and unit-testable.
- **Future worker threads (`TasksCore`):** `Publish` is the only cross-thread entry; it can
  be made lock-free/MPSC later without changing call sites. Handlers always run on the
  owning loop thread during `Dispatch()`, so subscribers need no locking. The design
  *reserves* this; it does not build it now (YAGNI, but no dead-end).
- **Re-entrancy:** queued dispatch means a handler that publishes does not recurse; the new
  message is drained in the same `Dispatch()` pass (bounded by a max-iterations guard to
  avoid a publish loop wedging a tick).

---

## 10. Testing strategy

Mirror the existing `Tests/NeuronCore` GoogleTest suites:
- **Codec golden tests** — for every catalog entry: `Encode` → `Decode` → field-equality;
  plus a fixed byte-vector golden so wire drift is caught (like `StationProtocolTests`).
- **Bus tests** — subscribe/publish/dispatch ordering, multiple subscribers, queued vs
  immediate, re-entrant publish, queue-depth guard.
- **Round-trip over `ReliableChannel`** — publish reliable message → `WritePacket` →
  `ReadPacket` → ingress → handler fired, under simulated loss/reorder (reuse the
  `ReliableChannel` test harness).
- **Direction/lane `static_assert`s** — compile-time tests that a server-only message can't
  be sent from the client, and an unreliable message can't be queued reliably.
- **Parity tests during migration** — assert the new codec produces byte-identical output
  to the legacy `Encode*` for each folded message *before* deleting the legacy path.

---

## 11. Phased rollout (incremental, always-building)

> Each phase compiles, passes tests, and leaves the game runnable. We fold legacy schemas
> in one at a time and delete the old encode/decode only after a byte-parity test passes.

**Phase 0 — Foundations (NeuronCore, no behaviour change)**
- Add `NeuronCore/Messages/`: `MessageId.h` (catalog enum), `Serialize.h` (concept + leaf
  codecs over `DataWriter`/`DataReader`), `Message` concept, `MessageBus.h`.
- Unit tests for codec + bus. Nothing wired into the game yet.

**Phase 1 — In-process bus on the server**
- Introduce `FireWeapon` / `Hit` / `EntityDeath` / `Crime` as in-process messages.
- Refactor `Server/Main.cpp`'s `applyShot` and the kill loop to publish/subscribe, with
  the combat math (`ResolvePlayerFire`, `StepCombat`) unchanged. Behaviour identical;
  structure decoupled. Validate with existing combat tests + manual run.

**Phase 2 — Fold the unreliable lane (`InputCommand`)**
- Define `InputCommand` as a message; add the `'NMSG'` unreliable framing alongside the
  existing `'NCMD'` path. Parity test vs `WriteInput`/`ReadInput`. Switch client send and
  server ingress to the message; delete `ClientInput.h`'s encode once green.

**Phase 3 — Fold the reliable lane (`GameEvents`, `StationProtocol`, manifest)**
- Move `AssignPlayer`, `EntityDespawn`, `EntityDeath`, `Chat`, `GalaxyManifest`,
  `StationRequest/Response` into the catalog with generated codecs. Generalise
  `ReliableChannel` content to `(MessageId, bytes)` (it already is `type` + payload — this
  is mostly a rename + codec swap). Replace the `PollEvent`/switch in `main.cpp` and the
  request loop in `Server/Main.cpp` with bus subscriptions. Delete `GameEvents.h` /
  `StationProtocol.h` encode/decode after parity.

**Phase 4 — Client-side bus & presentation handlers**
- Stand up the client `MessageBus`; route `ReplicationClient` ingress through it. Convert
  input polling to the mapper/command-builder chain (§8.1). Subscribe VFX/audio/camera/UI
  to inbound messages (enables the roadmap's Death-driven explosion VFX with no server
  entity).

**Phase 5 — Cleanup & docs**
- Remove the now-unused magics/versions, collapse `PeekMagic` to one branch, update
  `AGENTS.md`/roadmap references, and document the catalog as the single source of truth
  for the wire protocol.

---

## 12. Proposed file layout (NeuronCore — mechanism only)

```
NeuronCore/Messages/
  MessageId.h      // enum class MessageId : uint16_t  (the catalog ids)
  MessageTraits.h  // Reliability, Direction enums; per-type static traits
  Serialize.h      // Message concept + WriteField/ReadField leaves + Encode/Decode
  MessageBus.h     // Subscribe / Publish / Dispatch / Emit
  Catalog.h        // includes every message struct; the decode-dispatch table
  Messages/        // one header per message struct (InputCommand.h, Death.h, …)
Tests/NeuronCore/
  MessageCodecTests.cpp
  MessageBusTests.cpp
  MessageWireTests.cpp   // golden bytes + ReliableChannel round-trip
```

Handlers (behaviour) live **outside** NeuronCore: server handlers in `GameLogic` /
`Server`, presentation handlers in `NeuronClient` / `DeepspaceOutpost`.

---

## 13. Risks & open questions

- **Codec for variable-length/nested fields.** `std::string`, `std::vector<T>`, and the
  `GalaxyManifest` chunking need careful leaf overloads (length-prefixed) and a max-size
  guard so a hostile length can't allocate unbounded. Mitigation: bounds-checked reads
  (already the `DataReader` contract) + explicit caps mirroring `GALAXY_NAME_MAX`.
- **`std::tie`-based reflection ergonomics.** Requires every message to keep `Fields()` in
  sync with its members. Mitigation: a `static_assert(sizeof)` / field-count check and the
  golden wire tests catch drift immediately. (If C++26 reflection lands, `Fields()` can be
  generated and removed — the codec API stays.)
- **Per-message vs batched reliable sends.** `ReliableChannel::WritePacket` already packs
  multiple messages per datagram; we keep that and just change the record content.
- **Hot-path overhead of the bus.** `std::function` + type-erased queue has cost. For the
  highest-frequency in-process events we can specialise, but at 30 Hz with ≤100 players the
  simple version is fine; measure before optimising.
- **Ordering between snapshot and reliable messages.** Unchanged from today (death event
  vs snapshot that still shows the entity); the client already handles this via `Forget`.
  Worth restating in the catalog docs so new messages respect it.

---

## 14. Summary

The system makes a **message** the single currency of interaction: a typed C++23 struct
that describes its own fields once, is serialized automatically, is published onto an
in-process bus *or* serialized to a peer with no change to producer/consumer code, and
carries compile-time delivery traits so it can only travel the right lane in the right
direction. The existing hand-rolled protocols fold in as the first catalog entries and are
deleted as parity is proven, leaving **one** mechanism that drives input, combat, lifecycle,
trade, and chat — and makes *all* client ↔ server communication nothing more than the
message catalog on the wire.
