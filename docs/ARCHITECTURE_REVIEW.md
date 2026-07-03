# ARCHITECTURE.md — Review & Recommendations

**Reviewed:** [`ARCHITECTURE.md`](ARCHITECTURE.md) (as-built, Phase G complete),
cross-checked against [`MIGRATION_ROADMAP.md`](MIGRATION_ROADMAP.md) and
[`gameplay.md`](gameplay.md), 2026-07-03.

**Scope of this review.** Three questions, per the review request:

1. Where does the design need **simplification** — places carrying more
   mechanism or special-casing than the rules require?
2. Where does the design need **additional functionality** to be sound as an
   MMO — hardening the concept that already exists?
3. What **features are missing** for the game's own declared trajectory — a
   Darwinia-style (wireframe aesthetic, indirect unit control) **4X space MMO**
   — per the locked decisions in `MIGRATION_ROADMAP.md` §0/§2.4?

Nothing here proposes changing the structural concept: server-authoritative
simulation, thin presentation client, data-only sharing through `NeuronCore`,
intent-based input, deterministic headless `GameLogic`. Those are the right
bones and every recommendation below builds *on* them.

---

## 0. Verdict in one paragraph

The as-built architecture is unusually disciplined: the single load-bearing
rule (server simulates, client renders, only schemas are shared) is stated
once and actually enforced everywhere — intent clamping, catalog messages with
compile-time trait governance, bounded decoding, deterministic seeded RNG,
headless tests. The weaknesses are not in the core design but at its edges:
(a) a handful of protocol conveniences that accumulated special cases worth
simplifying before they calcify into permanent ABI; (b) MMO table-stakes that
are acknowledged but under-weighted (persistence, session security, reconnect,
time sync); and (c) a quiet drift back toward "player = one ship" in the
as-built identity/messages, which is precisely the retrofit the roadmap's §2.4
warned would be the most expensive to undo. The 4X feature gaps (fog of war,
ownership/territory, construction, multi-unit orders, factions, a living
economy) all have natural homes in the existing seams — none requires breaking
the concept.

---

## 1. Simplifications

Ordered by value ÷ effort. Message ids are permanent ABI (§4.3), so protocol
simplifications mean *introducing a successor id and retiring the old one*,
never mutating in place.

### S1. Fold the hand-encoded galaxy manifest into the catalog codec

`0x0210` is the only wire message outside the generic `Fields()` codec
(§4.4 "Bulk"): a hand-rolled fixed layout with a NUL-padded `char[12]` name
array. That is a second serialization path to maintain, fuzz, and
golden-test — for one message whose contents (`u32`, `i64`, short string,
small ints) the generic codec already expresses. Introduce a catalog-encoded
`GalaxyManifestChunk` (new id, e.g. `0x0211`) using `std::string` +
`std::vector<Entry>`, keep the same Bulk-lane chunking, retire `0x0210`.
One codec, one governance regime, one fewer "except for…" in the document.
(If S8/X1 land, the manifest also stops being a single cold-start
dump — see below — which makes this the right moment to re-cut the message.)

### S2. Make `ClientHello` the actual front door

Today the connect sequence (§4.6) is: *any* first `InputCommand` datagram from
an unknown UDP endpoint spawns a player entity, provisions a session, and
triggers the manifest Bulk send — and only *later* does `ClientHello` arrive
with the protocol version and name. This is backwards in three ways:

- **Protocol version is checked after the entity exists.** An incompatible
  client is already in the world before the server learns its version.
- **It is an amplification/DoS primitive.** One spoofed ~40-byte datagram
  makes the server allocate a session, spawn an entity, and stream a
  multi-kilobyte manifest to an arbitrary address. (§9 defers rate limiting,
  but this is cheaper to fix by ordering than by throttling.)
- **The session state machine must tolerate hello-before-or-after-input**
  (`gameplay.md` §2.1) — permanent complexity purchased for a transient
  bootstrapping convenience.

Simplify: an unknown endpoint's datagrams are ignored until a valid
`ClientHello` (version-checked) arrives on the Control lane; *that* spawns the
session and entity and starts the manifest. `InputCommand` from unknown
endpoints is dropped. This deletes the "name arrives late / `Commander-<n>`
placeholder" path, closes the amplification hole, and gives F/security work
(F2) a single choke point to attach tokens to.

### S3. Split travel out of the station protocol

`Teleport` and `JumpDrive` ride `StationRequest` but are intercepted before
`ProcessStationRequest` and routed to `HyperspaceSystem` (§4.4) — the message
name lies about its handler. The overloading shows in the fields and enums:
`stationId` means *system id* for Teleport only; `StationStatus` carries
travel-only outcomes (`Arrived`, `Witchspace`, `MassLocked`, `OutOfRange`,
`NotEnoughFuel`) alongside commerce outcomes (`NoStock`, `HoldFull`);
`StationResponse.credits/cargo` are meaningless for travel. Introduce a
`TravelRequest{kind: Hyperspace|InSystemJump, systemId}` /
`TravelResponse{status}` pair (Gameplay lane, ids in the `0x04xx` band or a
new travel band), and let `StationRequest`/`StationStatus` shrink back to
docking + commerce. The station protocol and the travel rules then evolve
independently — which they already do in the code (`ProcessStationRequest` vs
`HyperspaceSystem`); the wire should match.

### S4. Delete the client's single-player shield-regen fallback

§7 notes local shield regen runs "only when disconnected (single-player
fallback path)". Single-player was retired (roadmap status, 2026-06-28); this
is the one place a *game rule* still lives in the client, in direct tension
with the load-bearing rule the same document leads with. Delete it. A
disconnected client should show a "connection lost" state, not simulate
vitals. (Cheap, and it makes the architecture's central claim literally true.)

### S5. Replace `Sleep(33)` with an accumulator-based fixed timestep

§3.3/§10 pin the tick at "~30 Hz (`Sleep(33)`)". `Sleep` guarantees *at
least* the requested delay; actual tick duration drifts with scheduler load,
so "600 ticks ≈ 20 s" is only approximately true and worsens under load.
Since every rate in the game is expressed in ticks (good!), the fix is
confined to the host loop: fixed-timestep accumulator (run N catch-up ticks
when behind, sleep the remainder when ahead). This also gives Phase H a
meaningful "tick overrun" metric for free. No gameplay code changes.

### S6. Re-band the misfiled event ids before the bands ossify

`EcmPulse` (`0x0202`) and `EscapePodUsed` (`0x0203`) sit in the
"replication control / lifecycle" band (`0x0200–0x02FF`) but are gameplay
events, not lifecycle. Ids are permanent, so this cannot be *fixed*, only
stopped: either amend the §4.3 band table to describe the band as
"lifecycle **and combat events**", or (better) declare the next combat/VFX
event ids will be allocated from the game-specific band (`0x1000+`) and note
the two legacy placements as grandfathered. Cheap now, impossible later.

### S7. One name for the input message

The document calls it `InputCommand` "(alias `Net::ClientInput`)" and the
gameplay plan calls it `ClientInput`. Aliases in a protocol document are pure
reader tax. Pick one (the catalog name, `InputCommand`), rename the code-side
alias, and strike the parenthetical.

### S8. Trim the always-on Bulk manifest (see also X1)

Shipping all 256 systems' names, positions and attributes to every client at
connect is simple, but it is also the single largest cold-start payload and
it forecloses exploration gameplay (X1). Even absent 4X goals, sending the
manifest *incrementally* (home system + jump-range neighbours first, the rest
on demand or in trickle) simplifies nothing today but bounds the connect
burst; combined with X1 it becomes a feature. Flagged here so S1's message
re-cut anticipates a `baseIndex/count` request-driven flow rather than a
fire-hose.

---

## 2. Additional functionality the current concept requires

These are not new gameplay — they are what "MMO" already promises. Ordered by
how much of the rest of the design is blocked on them.

### F1. Persistence (Phase F) — everything else is provisional until this

Already the document's own top structural gap (§12), so only two additions:

- **Promote the "plain components" rule from `gameplay.md` §0 into
  `ARCHITECTURE.md` itself** as a design invariant: *durable-in-spirit state
  lives in plain serializable components* (`Wallet`, `CargoHold`, `Fuel`,
  `Wanted`, `PlayerRecord`, `Equipment`). It is currently only stated in the
  phase plan, where it will be forgotten once G is history.
- **Persist the world, not just the players.** The roadmap (§2.4, Phase F)
  already says empire/market/territory state must survive restarts for the 4X
  drift. Note in the architecture that station markets are currently pure
  functions of seed — the moment X5 (dynamic economy) lands, markets become
  state and need rows. Design the schema with that in mind now.

### F2. Session security: the UDP endpoint must stop being the identity

§5.2: sessions are keyed by `addr<<16 | port`. Consequences:

- **Hijack/spoof:** anyone who can observe or guess a player's endpoint can
  send `InputCommand`s (or a `StationRequest{Sell...}`) as them. UDP source
  addresses are trivially forged on many paths.
- **NAT rebinding kills the session:** a router changing the source port
  mid-flight looks like a brand-new player (see F3).

Minimum viable fix, staying on plain UDP: `ClientHello` (now the front door,
S2) returns a per-session random 64-bit token; every subsequent datagram
carries it (4.1's framing has room — a per-packet field after the lane byte);
mismatches are dropped. That defeats blind spoofing and decouples identity
from endpoint. Real authentication (accounts) arrives with F1; transport
encryption can follow later — but the token is a weekend, not a phase, and
closes the worst hole. Add per-endpoint rate limiting at the same choke
point (§9 already lists it as future).

### F3. Reconnect & resume

Idle reaping after 300 ticks (~10 s) plus endpoint-keyed sessions means a
Wi-Fi blip or NAT rebind is character death (pre-F1: total loss). With F2's
token, resume is nearly free: a `ClientHello` carrying an existing token
re-binds the session to the new endpoint instead of spawning fresh. Add a
grace window (e.g. 30–60 s: ship flies straight/despawns from AOI but the
entity and record survive) before the reap is final. This is the single
biggest perceived-quality feature per line of code in this list.

### F4. Time synchronization & snapshot pacing contract

Snapshots carry a `tick` (§4.4) but the protocol has no notion of shared
time: the client cannot know how far behind "now" it should render, so the
interpolation delay is a guess, and future lag compensation (roadmap Phase
E/G note) has no clock to compensate against. Add a trivial ping/time
message pair (Control lane): client learns RTT + server-tick offset,
schedules interpolation at `serverTick − k`, and the server gains the RTT
estimate it will need for lag-compensated hit tests (F5). Also document the
*snapshot send rate* as a first-class constant — today it is implicitly
"every tick per session", which is exactly the knob Phase D bandwidth work
will need to turn.

### F5. Server-side lag compensation for aiming

`ResolvePlayerFire` tests the cos ≥ 0.9 cone against *current* server
positions (§6.2). At 100+ ms RTT the client aims at a ghost: what they see is
the target's position ~(RTT + interpolation delay) ago. For a game whose core
loop is dogfighting, this punishes exactly the players an MMO wants to keep.
Standard fix, already anticipated by the roadmap: keep a short ring buffer of
recent per-entity transforms (~0.5 s at 30 Hz ≈ 15 frames), and resolve fire
against the world at `now − RTT − interpDelay` (clamped). Determinism is
unaffected (the buffer is derived state). Depends on F4 for the RTT estimate.

### F6. Broadcast player kills — the killer currently sees nothing

§4.4/§6.4: a player victim's `EntityDeath` goes *only to the dying session*,
so the ship "never flickers out" for others. Side effect: the killer of a
player gets **no explosion, no kill feedback at all** — the target simply
vanishes from AOI as it respawns docked elsewhere. That is the wrong trade
for a PvP game. Keep the respawn-invisibility goal but add a broadcast VFX
event (e.g. `ExplosionAt{x,y,z, scale}` or an `EntityDeath` variant flagged
`cosmetic`) sent to everyone *except* the victim, anchored at the death
position rather than the (respawned) entity. Cheap, and it also gives G-era
loot canisters a visual cause.

### F7. Validate the missile lock server-side

`InputCommand.missileTarget` is a client-supplied index resolved through
`LiveEntity()` (good — no stale-handle forgery), but nothing in §6.2 says the
server checks the lock is *plausible*: in sensor range, roughly forward, not
a teammate at lock time. A modified client can lock any entity id it has ever
seen in a snapshot, including one behind it or 90 km away, and fire on the
turn. Add the same style of gate the laser already has (range + cone at
launch time). One function, one test.

### F8. Wire the spatial grid into the O(n²) loops before Phase H, not during

§12 already names this; the review only re-prioritizes it: combat target
scans, collisions, scooping, ECM radius, energy-bomb radius and AOI are all
pairwise sweeps today. The roadmap says `Spatial::Grid` exists since A3. The
100-player milestone *with* NPC caps lifted (X4 wants many more entities)
turns every one of these into a hot spot simultaneously. Doing the grid
integration while the entity counts are still small means each system can be
converted and golden-tested one at a time instead of under load-test fire.

### F9. Finish chat with abuse controls designed in

Already planned (`gameplay.md` G2). Two architecture-level notes: rate
limiting and length caps belong in the *server relay* (never trust the
sender), and the roster (`PlayerInfo`) should gain a mute/ignore affordance
client-side from day one — retrofit cost is near zero now and painful after.

---

## 3. Missing features for the 4X / MMO trajectory

The roadmap locked these directions (§0 "Game trajectory", §2.4) — this
section maps them onto the as-built architecture. The Darwinia comparison is
apt in two specific ways: the wireframe aesthetic (already native to this
codebase) and **indirect control** — the player as commander issuing orders
to units that pilot themselves, which this engine is unusually well prepared
for because *NPCs already fly by writing `FlightIntent`* (§6.1's
"architectural rhyme"). The recommendations below exploit that.

Grouped by the four X's, each with its architectural seam.

### X1. Explore — fog of war over the galaxy

**Gap:** the full 256-system manifest ships to every client at connect
(§4.4 Bulk); the galaxy holds no secrets, so "explore" is a chart screen.

**Recommendation:** per-player *known-systems* state (a component:
`KnownSystems{bitset/ids}` — persistence-ready per F1's rule). A fresh
commander knows the home cluster; systems become known by visiting, by
docking (buy charts at high-tech stations — an economy sink), or later by
scout units (X4). The manifest becomes request/driven and incremental
(S8's re-cut anticipates this). AOI already prevents seeing *entities* you
shouldn't; this extends the idea to *map knowledge*. Strategic-tier
replication (X6) then has a natural filter: you see summaries only of known
space.

### X2. Expand — claimable outposts and territory

**Gap:** the game is named *Deepspace Outpost*, but stations are immutable
world furniture; nothing can be owned, built, or claimed. 4X "expand" has no
verb.

**Recommendation:** two steps that reuse existing seams.

1. **Ownership before construction.** Add `Owner{playerId}` (see X4's
   identity prerequisite) to stations and future structures, plus a claim
   mechanic (e.g. purchase a *neutral* station's charter, or deploy a beacon
   in an unclaimed system). Owned stations confer concrete, small privileges
   first: docking priority, a market fee share trickling into the owner's
   wallet, docking refusal lists. All of it is server-validated station
   logic — the `ProcessStationRequest` pattern already fits.
2. **Construction as a validated order.** A deployable outpost kit: bought as
   cargo (it already has tonnage semantics), deployed via a command message
   (the `0x1000+` game-specific band exists for exactly this), spawning a
   structure entity that upgrades over time/resources. No new architecture:
   it is an entity with components, replicated by the existing snapshot path
   (one new `NetType`), persisted by F1.

Territory then *emerges* from station/beacon ownership (influence radius per
owned structure) rather than needing a separate map-painting system —
appropriate for the "extension, not rewrite" rule.

### X3. Exploit — a living economy instead of a seeded one

**Gap:** each station's market is a pure function of its seed (§6.6) —
faithful to Elite, but static: player trade changes stock only until the next
regeneration, prices never respond to war or supply, and no goods actually
move between systems except in player holds.

**Recommendation (staged, keeping the legacy generator as the *baseline*):**

1. Make market stock/prices *state* that drifts back toward the seeded
   baseline (so the legacy feel survives), with player buys/sells pushing
   against it. This alone creates trade-route gameplay (arbitrage decays as
   routes are exploited).
2. Let the already-existing **traders** (§6.7) be the visible economy: a
   trader that docks delivers goods (stock up, price down) — suddenly the
   ambient NPCs *are* the supply chain, and piracy has macro consequences
   (killed traders = scarcity). This is a beautiful fit for the sim: the
   mechanism (lane traffic) exists; only the delivery side-effect is missing.
3. Later: production per economy type (agri produces food, industrial
   consumes it), giving X2's owned stations something to tax and X4's
   haulers something to haul.

Persistence (F1) is a prerequisite for any of this being real.

### X4. Exterminate at scale — indirect control of owned units (the Darwinia hook)

**Gap:** the identity model. Roadmap §2.4's first and loudest warning is
*player ≠ avatar* ("avoids the deepest retrofit") — yet the as-built protocol
re-concretized the single-avatar assumption: `AssignPlayer{entityId}` is
singular, `PlayerStatus` is one ship's vitals, `PlayerRecord` lives *on the
ship entity*, sessions map 1:1 to a hull. Every month of building on this
makes the promised pivot more expensive.

**Recommendation — introduce the identity layer now, minimally:**

1. A `PlayerId` distinct from any entity id; the session owns a `PlayerId`;
   `PlayerRecord` (name, score, credits-as-account-money later) keys off it.
   `AssignPlayer` grows into `AssignControl{playerId, primaryEntityId}` (new
   message id per ABI rules). Wallet/score move player-side; hull-bound state
   (shields, fuel, cargo) stays entity-side. Short-term behaviour identical.
2. `Owner{playerId}` component (also required by X2) — the relational index
   the roadmap promised ("all my units" as a cheap query).
3. **Then the Darwinia move is almost free:** player-owned escort/drone
   ships are NPC hulls with `Owner{you}` whose `AiSystem` targets/waypoints
   are set by a validated order message (`UnitOrder{unitId, order:
   Escort|Attack|Patrol|Dock, target}` in the `0x1000+` band) instead of by
   the spawn director. The AI already flies by intent through the same
   pipeline as players (§6.1/§6.7) — indirect control is *literally* the
   existing architecture with a different order source. Start with one
   buyable escort fighter as the vertical slice.

This is the highest-leverage item in section 3: it unblocks X2 (ownership),
the fleet dimension of X3 (owned haulers), factions (X5), and the strategic
tier (X6), and it is the one the roadmap itself says gets more expensive
every day.

### X5. Diplomacy & factions — outgrow the hard-coded team enum

**Gap:** allegiance is a fixed five-value enum (Player=0, Pirate=1, Police=2,
Station=3, Trader=4, §6.2). All players are one "team"; PvP legality is
mediated solely by the wanted system. A 4X MMO needs player groupings —
alliances, standings, wars — and none of that fits in an enum.

**Recommendation:** split *archetype* from *allegiance*. Keep the enum for
NPC archetypes (police discipline, pirate targeting are archetype rules), and
add a `FactionId` (dynamic, server-issued) with a small standings table
(ally/neutral/hostile). Crime/PvP rules generalize cleanly: "protected
victim" (§6.3) becomes "clean player *not at war with you*"; declared wars
suspend wanted-level consequences between the belligerents (giving 4X players
consensual mass PvP without touching the police system for everyone else).
Faction chat rides F9's chat with a channel field. Persist with F1.

### X6. The strategic view — the second AOI tier (already designed, now needed)

**Gap:** roadmap Phase D's strategic tier remains unbuilt; there is exactly
one interest radius (±1 cell + landmarks). A commander with holdings in three
systems (X2) or a hauler fleet (X4) is blind to all of it.

**Recommendation:** a low-rate (~0.5–1 Hz), coarse, reliable-lane summary
stream: per known system (X1 filter), aggregate counts/ownership/alerts —
"your station in Lave is under attack", "hauler 2 docked". Not entity
snapshots: a purpose-built summary message keyed by system id. This is also
the bandwidth-correct answer to "how does a 4X player watch an empire" —
never by widening the tactical AOI. Depends on X4's ownership index; feeds
the chart screen the client already has.

### X7. Deliberately *not* recommended

To honor "do not structurally change the concept": no landing/planetary
layer, no crafting trees, no player-built capital ships, no sharded
mega-galaxy, no real-time voice — none are required by the 4X loop, and each
would strain the thin-client / 1200-byte / 30 Hz envelope that makes this
codebase testable and honest. The 4X pivot should keep riding the existing
primitives (entities, intents, validated orders, reliable events, AOI) — the
sections above show it can.

---

## 4. Document fixes (small, do anytime)

- **Test-count inconsistency:** §1 says "218 GameLogic tests"; §8 says
  "205 GameLogic tests". One of them is stale — pick whichever `ctest`
  reports and consider wording that doesn't rot ("200+" or a CI badge).
- **§4.3 band table vs. reality:** note the `0x0202/0x0203` grandfathering
  (S6) so the next contributor doesn't copy the precedent.
- **State the snapshot send rate explicitly** in §4.4/§10 (currently implied
  as once per tick per session) — it is a load-bearing number for Phase D/H.
- **Promote two rules from `gameplay.md` into §12/§9 as invariants:**
  persistence-ready plain components (F1) and "no game rules in the client,
  including fallbacks" (S4 closes the one exception).
- §12's future list should absorb the priorities argued here; suggested
  order: **F1 persistence → F2/F3 session security & resume → X4 identity
  layer (player ≠ avatar) → F4/F5 time sync & lag compensation → Phase D
  delta/strategic tier (X6) → 4X feature slices (X1, X2, X3, X5)** — with
  S1–S7 slotted in as small standalone wins whenever convenient, and F8
  (spatial grid) before any entity-cap increase.

---

## 5. Priority matrix (summary)

| # | Recommendation | Type | Effort | Unblocks |
|---|---|---|---|---|
| F1 | Persistence + world-state rows | Add | L | everything durable |
| S2 | `ClientHello`-first handshake | Simplify | S | F2, F3 |
| F2 | Session token, drop endpoint-as-identity | Add | S | F3, security |
| F3 | Reconnect grace + resume | Add | S | player retention |
| X4 | `PlayerId`/`Owner` identity layer + first ordered unit | Add | M | X2, X3, X5, X6 |
| F6 | Broadcast player-kill VFX event | Add | S | PvP feel |
| F4 | Time sync (ping/offset) | Add | S | F5, interpolation |
| F5 | Lag-compensated fire resolution | Add | M | PvP fairness |
| F8 | Spatial grid in combat/collision/AOI loops | Add | M | scale (H), X4 unit counts |
| S1 | Manifest onto the generic codec | Simplify | S | S8, X1 |
| S3 | `TravelRequest` out of station protocol | Simplify | S | protocol hygiene |
| S4 | Delete client shield-regen fallback | Simplify | S | dogma integrity |
| S5 | Accumulator fixed timestep | Simplify | S | tick stability, H metrics |
| F7 | Server-side missile-lock validation | Add | S | anti-cheat |
| F9 | Chat with relay-side abuse controls | Add | M | social baseline |
| X1 | Fog of war / known systems | Feature | M | explore |
| X2 | Ownership + deployable outposts | Feature | L | expand |
| X3 | Drifting markets + traders-as-supply | Feature | M–L | exploit |
| X5 | FactionId + standings | Feature | M | diplomacy, mass PvP |
| X6 | Strategic AOI summary tier | Feature | M | empire visibility |
| S6/S7 | Id-band note, single input-message name | Docs | XS | — |

*Effort: XS/S ≤ a day-ish, M = days, L = week(s), matching `gameplay.md`'s scale.*
