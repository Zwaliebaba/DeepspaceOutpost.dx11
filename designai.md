# DeepspaceOutpost — Homeworld-Style Ship AI Design

**Status:** design document, written 2026-07-08 against the as-built code
(`docs/ARCHITECTURE.md` §5.1, §6.1, §6.7, §6.13; `GameLogic/AiSystem.h`,
`OrderSystem.h`, `LaunchSystem.h`, `SpawnDirector.h`, `EscortSpawn.h` verified
in source). This document designs the evolution of the server's ship AI from
the current *per-ship Elite tactics + single-unit orders* into a
**Homeworld-style fleet AI**: units that fly themselves safely, adapt their
behaviour to the situation, operate in groups, cycle through a mother
station, and fight in authored PvE encounters — all inside the seamless PvP
MMO world.

**Owner decisions recorded here (2026-07-08):**

| Question | Decision |
|---|---|
| How do PvE "scenes" exist in a seamless MMO? | **Open-world encounters** — data-driven scripted encounters spawned at world locations, extending the spawn-director seam. Other players can stumble into them. The encounter *definition format* is designed so it could later also drive instanced scenes, but no instancing is planned. |
| What does "a ship can never crash into another ship" mean? | **Hard AI avoidance, damage stays.** Collision avoidance becomes a mandatory final pass on *every* AI/order-driven ship. Ship↔ship contact damage (§6.9) is unchanged — the guarantee is behavioural ("an AI-driven ship never *steers into* a hull"), not physical, so ramming stays honest physics and legal gameplay for anything not under AI control. |
| Scope beyond the basics | **All four pillars**: tactics stances, formations, patrol & waypoint routes, and the mother-station dock/undock cycle. |

Nothing in this design touches the anti-cheat boundary: the client still
cannot move a hull an inch. Every new behaviour is server-side `GameLogic`,
driven by validated orders and deterministic seeded RNG, exactly like the
systems it extends. This is a **server game-logic design** with a handful of
small reliable-lane wire additions (§3.9).

**Naming.** All new identifiers follow
[`.github/coding-standards.md`](.github/coding-standards.md): types/functions
`PascalCase`, constants `UPPER_SNAKE_CASE`, `enum class` with explicit
underlying type. Existing symbols are referenced by their real names so this
document stays an accurate map of the code.

---

## 0. The target model (Homeworld reference, adapted)

What "Homeworld-like AI" means for *this* game — an MMO with PvP, not a
single-player RTS campaign — completed with the conventions Homeworld players
expect where the brief leaves gaps:

1. **Ships fly themselves; players command.** Already true (the pointer-first
   order model, `docs/interaction.md`). The AI layer's job is to make ordered
   flight *trustworthy*: a unit sent across a battle never rams a hull, never
   clips a station, and arrives looking like a pilot flew it.
2. **The no-crash rule.** In Homeworld, ships brake, weave and berth around
   each other; collisions are not how fights are lost. Here: every hull whose
   `FlightIntent` is authored by an order or an AI brain gets a mandatory
   avoidance pass — friend *and* foe — strong enough to guarantee no
   AI-steered contact (§3.2).
3. **Tactics stances.** Every unit carries **Aggressive / Neutral / Evasive**,
   changing how it engages, pursues, holds formation and flees — the core
   "adapt to the situation" knob (§3.3).
4. **Formations.** A band-selected group ordered somewhere travels as a
   formation (delta / line / sphere), speed-matched, and re-forms after
   combat per stance (§3.4).
5. **Waypoints, patrol, routes.** Shift-queued orders form waypoint chains;
   Patrol loops them; Route runs a station chain — implementing the reserved
   `OrderKind::Patrol`/`Route` values (§3.5).
6. **The mother station is the carrier.** Undocking is a flown launch (built:
   `LaunchSystem.h`); ordering a *docked* unit auto-launches it first;
   launches are queued so a fleet leaves the bay staggered; a Dock order ends
   in repair/rearm. When mobile carriers/outposts arrive (§13.2-2 of
   ARCHITECTURE.md), they reuse the same dock-port seam (§3.6).
7. **PvE scenes.** Authored encounters — "battle a predefined enemy" — exist
   as data-defined, deterministically-spawned open-world events with waves,
   roles, objectives and rewards, run by an `EncounterDirector` sibling of
   the existing `SpawnDirector` (§3.7).
8. **PvP safety by construction.** The same server-side guardrails serve PvP:
   ordered units cannot be used as ram-griefing missiles, Evasive escorts
   never commit accidental crimes, and launch immunity keeps undocking fair
   (§3.8).

The design principle throughout: **behaviour = parameters, not new code
paths**. One pilot pipeline, tuned per stance × situation, so "flexible to
adapt based on situations" means adding a row to a table, not a branch to a
brain.

---

## 1. Current state evaluation

The Homeworld layer is *half built*, and the half that exists is good. The
canonical facts (all verified in source):

### 1.1 One flight model, three intent authors

Everything flies by writing normalized axes into `FlightIntent`, clamped by
the hull's `FlightCaps` (`GameLogic/FlightInput.h`) and integrated by
`StepFlight` (`GameLogic/FlightSystem.h`) — the arcade
basis-rotation model (side/roof/nose, no drift). The three authors:

| Author | System | Runs |
|---|---|---|
| Player orders | `StepOrders` (`OrderSystem.h:232`) | every tick, before AI |
| NPC brains | `StepAi` (`AiSystem.h:402`) | each ship thinks every 8th tick (`(index ^ tick) & 7`) |
| Launch fly-out | `StepLaunchCruise` (`LaunchSystem.h:26`) | first inside `GameLogic::Tick`, overrides intent while `LaunchCruise` is present |

Server tick order (ARCHITECTURE.md §5.1): `StepOrders → StepAi →
GameLogic::Tick(StepLaunchCruise → StepFlightInput → StepFlight → StepMotion)
→ CompleteDockOrders → SpawnDirector::Step → StepTraders → … → StepMissiles →
StepCombat → StepCollisions`.

### 1.2 The NPC brain (`AiSystem.h`)

A faithful port of the legacy `tactics()`/`track_object()`: pursue with
deadzone steering (`Detail::SteerToward`, `AiSystem.h:173`), attack runs
gated by a bravery roll, the **close-box break-off** (768×512, wider than the
600-unit contact range — the existing anti-ram rule), jink, panic missile,
flee-below-⅛-energy. Per-ship state in `AiPilot` (`AiSystem.h:145`).
Targeting with `Combatant.focus` memory (`Detail::FindTarget`,
`AiSystem.h:333`). Tunables are `constexpr` constants (`AiSystem.h:51–127`).

**Collision avoidance exists — for NPCs only.** `Detail::BuildAvoidGrid` /
`Detail::AvoidObstacles` (`AiSystem.h:230/255`): a 6000-unit forward
lookahead against a per-tick obstacle grid; berth radii `AVOID_SHIP_R` 1200 /
`AVOID_STATION_R` 2500 / `AVOID_PLANET_R` 6000; the current combat `focus` is
exempt so attack runs press home. Deterministic (no RNG, integer positions,
fixed tie-break).

### 1.3 The order layer (`OrderSystem.h`)

`PlanUnitOrder` (`OrderSystem.h:117`) is the pure anti-cheat validation
matrix; `StepOrders` (`OrderSystem.h:232`) executes
Stop/Move/Approach/Dock/Attack/Collect/Escort through the *same*
`SteerToward` steering, with arrival-aware throttle (`MoveThrottle`,
`OrderSystem.h:209`) and an Attack standoff (`ORDER_ATTACK_STANDOFF` 900,
`OrderSystem.h:82`). `OrderKind::Patrol = 8` and `Route = 9` are **reserved
and rejected** (`UnitOrder.h`, `OrderSystem.h:184`). `Escort` is "reserved
formation semantics, treated as a never-completing Approach for now"
(`OrderSystem.h:28`).

### 1.4 Units, spawning, stations

- **Escorts** (`EscortSpawn.h:47`): player-owned, Team-Player, auto-engaging
  fighters driven by `ActiveOrder` — the template for "a player commands AI
  ships". Cap 4, hardcoded Viper loadout.
- **SpawnDirector** (`SpawnDirector.h`): ambient pirates near players (cap
  12), police-on-crime with warrants (`focus` fixed on the offender), lane
  traders. Deterministic LCG. NPC loadouts are **hardcoded component
  assembly** — `GameData/Models/*.json` ship stats feed only the client.
- **Witchspace ambush** (`HyperspaceSystem.h`): a ~0.8 % misjump strands the
  ship 20M units out with 1–4 Thargoids — the one "scripted encounter" in the
  game, and the seed of the PvE system (§3.7).
- **Undock is a fly-out, not a teleport** (`LaunchSystem.h`): the Undock
  handler attaches `LaunchCruise{origin, distance, throttle}`
  (`SimComponents.h:104`); `StepLaunchCruise` forces a straight 0.35-throttle
  cruise until `LAUNCH_OFFSET` (2000, `StationServices.h`) clears, under
  launch immunity (`invulnTicks`). **This is exactly the "ship automatically
  leaves the mother station on undock" behaviour — built and tested.** What's
  missing is wiring it into orders (§1.5-4) and staggering multiple launches.

### 1.5 What's missing (gap analysis)

1. **Ordered units do not avoid anything.** `StepOrders` steers straight at
   the destination; only the Attack standoff and dock/arrival radii prevent
   the worst. A Move order through a furball, a station, or another player's
   ship flies *through* it — with §6.9 contact damage (100/tick ship↔ship)
   as the result. The avoidance pass exists ten lines away in the same
   header and is simply never called for `ActiveOrder` hulls.
2. **No stances.** Engagement posture is hardcoded per archetype (pirate
   bravery, trader cowardice, escort auto-engage). The player has no
   aggression knob; "adapt to the situation" is not expressible.
3. **No groups.** Orders are strictly per-unit; a band-selected fleet
   ordered to a point converges on the *same* point (and today, into each
   other). No formations, no group arrival, no speed matching.
4. **No order↔dock integration.** A flight order on a docked unit is
   rejected (`OrderStatus::Docked`); the player must manually undock first.
   No launch queue, no rearm-on-dock.
5. **No waypoints.** One order replaces the last ("latest order wins");
   Patrol/Route are rejected as `Illegal`.
6. **No mission/encounter system.** Roadmap item #5 is open; enemies exist
   only as ambient pressure (pirate spawner) or the witchspace ambush.
7. **NPC "situations" are rigid.** The brain's adaptations (flee threshold,
   panic missile, jink odds) are fixed constants — right for Elite parity,
   too rigid for authored encounters that want, e.g., a guard that holds
   position or a wave that presses regardless of losses.

---

## 2. Design overview: the pilot stack

All seven gaps close with **one architecture**: a layered pilot pipeline
that every AI-driven hull flows through, per tick.

```
┌──────────────────────────────────────────────────────────────┐
│ 1. OBJECTIVE   what should I be doing?                       │
│    ActiveOrder / OrderQueue (player)  ·  AiPilot (NPC)       │
│    EncounterRole (PvE script)         ·  LaunchCruise        │
├──────────────────────────────────────────────────────────────┤
│ 2. CONDUCT     how should I do it right now?                 │
│    TacticsStance × Situation → parameter set                 │
│    (engage? pursue how far? flee when? hold formation?)      │
├──────────────────────────────────────────────────────────────┤
│ 3. STEERING    turn the goal into axes                       │
│    Detail::SteerToward + throttle shaping (existing, shared) │
├──────────────────────────────────────────────────────────────┤
│ 4. AVOIDANCE   the mandatory final veto                      │
│    StepAvoidance: deflect ANY intent that would cross a berth│
│    — runs for every AI/order-driven hull, no exceptions      │
├──────────────────────────────────────────────────────────────┤
│ 5. INTENT      FlightIntent → ResolveIntent → FlightCaps     │
│    (existing; nothing can out-fly its hull)                  │
└──────────────────────────────────────────────────────────────┘
```

Layers 3 and 5 exist and are shared already. Layer 1 exists in three
disconnected pieces. The new work is layer 2 (stances), layer 4 (promoting
avoidance from an NPC-think detail to a universal final pass), and richer
layer-1 objective sources (queues, formations, encounter roles, launch
integration).

**Evolve, don't rewrite** (§12 trajectory decision): each layer lands as an
extension of the existing header it belongs to. No new library, no new
threading, no change to the tick's phase structure beyond one added step.

---

## 3. The design

### 3.1 Shared goal resolution

Today `StepOrders` and `StepAi` each contain their own goal→steering code.
Rather than merge the systems (they have different cadences and concerns),
extract the small amount of *truly shared* logic into `GameLogic/Pilot.h`:

```cpp
// What a pilot wants this tick, independent of who decided it.
struct PilotGoal
{
  enum class Kind : uint8_t { Hold, FlyTo, Pursue, FollowSlot, Flee };
  Kind kind = Kind::Hold;
  Math::Vector3i64 point{};      // FlyTo / FollowSlot resolved position
  uint32_t target = ECS::INVALID_INDEX;  // Pursue / Flee-from
  double throttleCap = 1.0;      // conduct layer may lower it (formation match)
  bool arriveStop = false;       // ease to a stop at the point
};
```

`StepOrders` resolves an `ActiveOrder` into a `PilotGoal`; `StepAi` resolves
its tactics decision into one; an encounter role or formation slot resolves
into one. A single `SteerGoal(world, self, goal, stanceParams, intent)`
turns it into axes + throttle using the existing `SteerToward` /
`MoveThrottle` / `ThrottleByAlignment` primitives. This is a refactor with
zero behaviour change, unit-tested by the existing suites — and it is the
seam every following section plugs into.

### 3.2 Hard collision avoidance — the no-crash rule

**Guarantee:** *a hull whose `FlightIntent` was authored by an order, an AI
brain, a formation slot, or an encounter role never steers itself within
ship-contact range (600) of another hull, nor within station-contact /
planet-kill range of a structure.* Contact damage rules (§6.9) are
unchanged; the guarantee is that AI flight never triggers them. A hull can
still *be* rammed — physics stays honest.

**Mechanism — promote avoidance to a tick phase.** Add `StepAvoidance`
(in `AiSystem.h`, where the grid code lives), running **after** `StepOrders`
and `StepAi` and **before** `GameLogic::Tick`:

```
StepOrders → StepAi → StepAvoidance → GameLogic::Tick(StepLaunchCruise → …)
```

Per tick:

1. `BuildAvoidGrid` once (already built per tick today; it moves here).
2. For every hull with an `ActiveOrder`, `AiPilot`, `FormationMember` or
   `EncounterRole` (i.e. every AI-authored intent — a plain `Each` over the
   union, cheap at current entity counts): run the forward-path check and
   **deflect the already-written intent** if the path crosses a berth.
3. `LaunchCruise` hulls are exempt (they are born clear by construction —
   `LAUNCH_OFFSET` launches start outside every contact range — and fly a
   protected straight line); the moment the cruise releases, avoidance owns
   them.

**Why the guarantee actually holds** (the math the implementation must keep
true): worst-case closing speed is `2 × NPC_MAX_SPEED = 120` u/tick
(head-on). With the check running **every tick** (not every 8th think — this
is the key change), reaction latency is 1 tick. The deflection must begin
while `distance > contact + closing × turnTicks`, where `turnTicks` is the
ticks to swing the nose past the berth at the hull's turn cap
(`NPC_MAX_TURN_RATE = 7/152` rad/tick ⇒ a full 90° evasion ≈ 34 ticks ≈
4100 units of closure). The current `AVOID_LOOKAHEAD = 6000` covers this
with margin; `AVOID_SHIP_R = 1200` stays as the berth. A **golden test**
(§5) enforces the invariant empirically, so the constants can be tuned
without re-deriving the proof by hand.

**Attack runs still press home — to a standoff, never to contact.** Today
the combat `focus` is fully exempt from avoidance. Under the hard rule the
exemption narrows: the focus target uses a *reduced* berth
`AVOID_FOCUS_R = 800` (just above the 600 contact range, just inside the
768 close-box) instead of no berth. Attack passes keep their aggression —
the close-box break-off (`AI_BOX_NOSE/SIDE`) and the order-layer standoff
(`ORDER_ATTACK_STANDOFF = 900`) already turn ships away — but a mis-tuned
pass can no longer graze into contact. Three overlapping protections, and
the outermost one is now unconditional.

**Mutual avoidance is deterministic — right-of-way, no RNG.** When two
AI hulls threaten each other head-on, both deflect, but along **opposite
deterministic sides**: the hull with the lower entity index climbs (+roof
deflection), the higher dives. No coin flips (determinism is a locked
asset), no mirror-dance oscillation. The existing grid tie-break rules
stay.

**Cost.** The per-tick sweep is a forward ray against a spatial grid the
server already builds, over the AI-driven subset of hulls, all cell-local
(§13.3 discipline: no whole-world scans). At the ~12-NPC + escorts scale
this is noise; at fleet scale it is exactly the kind of embarrassingly
parallel phase §13.3-E7 plans for. The 8-tick *think* stagger stays for the
expensive tactics brain; only the cheap safety ray runs every tick.

### 3.3 Tactics stances

One new persistence-ready component (the §12 standing invariant — plain
serializable data), on every commandable unit and every NPC:

```cpp
enum class TacticsStance : uint8_t { Evasive = 0, Neutral = 1, Aggressive = 2 };

struct UnitStance
{
  TacticsStance stance = TacticsStance::Neutral;
};
```

Stances select a **parameter row**, consulted by the conduct layer — never a
separate code path:

| Parameter | Evasive | Neutral | Aggressive |
|---|---|---|---|
| Auto-engage (own-team hostiles in range) | never | when fired upon, or by order | on sight |
| Return fire while executing a Move/Patrol | no — evade and continue | yes, without leaving the path leash | yes — breaks off to pursue |
| Pursuit leash (return to order/formation beyond this) | 0 (never pursues) | `AI_ENGAGE_RANGE / 2` = 8192 | `AI_ENGAGE_RANGE` = 16384 |
| Flee threshold (fraction of `maxEnergy`) | ½ (trader-grade cowardice) | ⅛ (the ported default) | 1/16 (fights nearly to the hull) |
| Attack-run commitment (effective bravery) | −32 | ported archetype value | +32 (clamped 0..127) |
| Jink odds under fire | 1-in-12 per think | 1-in-25 (ported) | 1-in-25 |
| Avoidance berth margin | ×1.5 | ×1.0 | ×1.0 (the *berth* never shrinks — §3.2 is stance-independent) |
| Formation keeping in combat | holds slot | holds slot | breaks formation to chase |

The rows live in one `constexpr StanceParams STANCE_TABLE[3]` next to the
existing tunables in `AiSystem.h`. Existing archetypes map onto rows instead
of bespoke constants: traders *are* Evasive, pirates Neutral-with-high-
bravery, police Aggressive-with-discipline (`PoliceMayEngage` remains an
independent legality filter on top — stance never overrides the law), and
the encounter system (§3.7) assigns stances per wave.

**Wire:** a new small reliable C→S message `SetStance{unitId, stance}`
(validated: ownership, known enum), acked like an order. Client UI: three
toggle buttons on the ability bar when a selection exists (the exact
Homeworld affordance), applying to the whole selection. Crime interaction:
stance changes are never crimes; an Aggressive unit's auto-engagement obeys
the same protected-victim rules as any NPC fire (§6.3) — Aggressive is not a
license, it is eagerness *within* legality.

### 3.4 Formations

**Scope:** movement formations for band-selected groups — the Homeworld
travel feel — not combat-maneuver choreography (attack passes remain
individual, governed by stance).

**Model.** A formation is a transient server-side grouping:

```cpp
enum class FormationShape : uint8_t { Delta = 0, Line = 1, Sphere = 2 };

struct FormationMember          // component on each member
{
  uint32_t groupId = 0;         // server-issued, unique per formation order
  uint16_t slot = 0;            // index into the shape's offset table
  FormationShape shape = FormationShape::Delta;
};
```

- **Creation:** when the client issues a group order (§3.9 wire), the server
  validates each unit exactly as today (`PlanUnitOrder` per unit — the
  anti-cheat matrix is unchanged), then assigns `groupId` + slots. Slot
  assignment is deterministic: sort members by entity index, fill the shape's
  offset table in order.
- **The guide point, not a leader ship.** The formation steers a virtual
  guide: the group centroid advanced along the ordered path at the **speed of
  the slowest member × 0.9** (speed matching — the Homeworld rule that the
  fleet arrives together). Each member's `PilotGoal` is `FollowSlot`: guide
  position + the slot offset rotated into the guide's travel frame. No
  leader means no leader-death edge case and no accordion oscillation.
- **Shapes** are `constexpr` offset tables scaled by group size: `Delta`
  (staggered wedge behind the guide — the default), `Line` (abreast),
  `Sphere` (escort shell — the natural Escort-order formation, replacing the
  "never-completing Approach" placeholder at `OrderSystem.h:28`).
- **Combat and dissolution:** per stance (§3.3 table) — Aggressive members
  break slot to fight and re-form on the leash rule; Neutral/Evasive fight
  from/hold slot. The formation dissolves when the order completes or a new
  non-group order arrives for a member (that member leaves; slots do not
  reshuffle mid-flight — gaps are aesthetic and cheap).
- **Avoidance beats formation.** `StepAvoidance` (§3.2) runs after slot
  steering; a member deflected off its slot simply re-converges. Slot offsets
  are spaced ≥ 2 × `AVOID_SHIP_R` so a formed group never triggers its own
  avoidance.

Escort ships (`EscortSpawn.h`) adopt `Sphere` around the owner by default —
the first visible payoff, using only components that already replicate.

### 3.5 Waypoints, Patrol, Route

**Order queues — the Homeworld shift-queue.** One new component:

```cpp
struct OrderQueue                      // capped, plain, serializable
{
  static constexpr size_t MAX = 8;
  ActiveOrder entries[MAX];
  uint8_t count = 0;
  bool loop = false;                   // Patrol: wrap instead of complete
};
```

`Msg::UnitOrder` gains one flag byte: `queue` (append instead of replace —
Shift-click in the client grammar, `docs/interaction.md`). `StepOrders`
changes minimally: when the head `ActiveOrder` completes and a queue exists,
pop the next (or wrap if `loop`). "Latest order wins" survives for
non-queued orders: an unqueued order clears the queue. Validation reuses
`PlanUnitOrder` per entry; a full queue acks `Rejected`.

- **`OrderKind::Patrol` (8) — now implemented:** the client sends the
  waypoints as queued Move orders + a final `Patrol` marker that sets
  `loop = true`. A patrolling unit obeys its stance at every waypoint leg:
  Neutral engages what fires on it and returns to the leg inside the leash;
  Aggressive sweeps the leash radius; Evasive just flies the loop. This is
  the standing-guard verb the 4X tier's territory feature (§13.2-2) will
  lean on.
- **`OrderKind::Route` (9) — now implemented:** a queue of Dock orders over
  a station chain. Dock → (dock services resolve: §3.6 rearm, later §13.2-3
  cargo exchange) → auto-undock (§3.6) → next station → loop. The hauler
  order ARCHITECTURE.md §13.2-3 reserved the value for — this design
  provides its flight layer; the economy layer plugs in later without
  touching it.

### 3.6 The mother-station cycle (carrier AI)

The station already *is* a Homeworld mothership in miniature: hangar
(`DockState`), flown launches (`LaunchCruise`), dock completion
(`CompleteDockOrders`). The design completes the cycle:

1. **Auto-undock on order** — the requested "ship automatically leaves the
   mother station" generalized: a flight order for a **docked** unit is no
   longer rejected with `OrderStatus::Docked`. Instead `PlanUnitOrder`
   accepts it and the server runs the existing Undock path (launch
   placement, `LaunchCruise`, launch immunity) with the order recorded but
   **held** until `StepLaunchCruise` releases the hull — at which point the
   order goes active and the unit proceeds. One new `OrderStatus::Launching`
   ack value tells the client what is happening (order toast: "launching…").
   Manual player undock (the station menu) is untouched.
2. **Launch queue.** Stations gain a tiny launch scheduler: at most one hull
   released per `LAUNCH_STAGGER` (≈ 30 ticks) per station, FIFO by request
   order, deterministic. A five-ship formation ordered out of the bay leaves
   like a carrier launching a strike group — and never overlaps inside the
   fly-out corridor. (The corridor itself needs no avoidance: launches are
   sequential, straight, and immune.)
3. **Rearm & repair on dock.** While docked, a unit regenerates energy/
   shields at an accelerated fixed rate and restocks missiles against the
   owner's wallet (validated station transaction, same as Refuel). A `Dock`
   order thus becomes Homeworld's "retire for repair"; `Route` (§3.5) gets
   its service stop for free.
4. **The future carrier.** When deployable outposts / mobile structures land
   (§13.2-2), dock capability is expressed by giving the structure the same
   dock-port surface the station exposes (`CanDock` range check + the launch
   scheduler), keyed off a component rather than `ServerStation`
   specifically. This design requires no changes when that happens — the
   cycle above is written against "a dockable entity", the station is merely
   the only one today.

### 3.7 PvE — open-world authored encounters

**The model:** an *encounter* is a data-defined, server-spawned battle scene
at a world location — waves of predefined enemies with roles, an objective,
and a reward — living in the one shared world (other players can join,
flee, or third-party it; that is a feature, and it is what the seamless-world
locked decision implies).

**Definition format** (initially `constexpr` tables in
`GameLogic/EncounterDefs.h`, deliberately shaped for a later cold-path JSON
loader under `GameData/Encounters/` — same trajectory as the ship-stats
JSONs; the format is the contract, the storage medium is an implementation
detail):

```cpp
struct EnemyArchetype   // extracted from today's hardcoded SpawnDirector loadouts
{
  ShipType hull; int energy; int laser; int64_t range;
  int bravery; int missiles; double maxSpeed; int bounty; bool ecm;
};

enum class EncounterRoleKind : uint8_t { Guard, Assault, Ambush, Courier };

struct WaveDef
{
  uint8_t archetype;          // index into the archetype table
  uint8_t count;              // 1..8
  EncounterRoleKind role;
  TacticsStance stance;
  FormationShape formation;
  uint16_t delayTicks;        // after the previous wave (0 = simultaneous)
};

enum class ObjectiveKind : uint8_t { DestroyAll, Survive, ProtectTarget, ReachPoint };

struct EncounterDef
{
  uint16_t id;
  ObjectiveKind objective;
  uint16_t paramTicks;        // Survive duration, etc.
  WaveDef waves[4]; uint8_t waveCount;
  int rewardCredits; int rewardScore;
};
```

**Roles are just initial orders + stances** — no second brain. `Guard`
= Patrol loop around the anchor with a leash; `Assault` = Attack the
triggering player (a warrant-style fixed `focus`, exactly the police
mechanism); `Ambush` = hold dark (Evasive, zero throttle) until the trigger
range, then Assault; `Courier` = Route to an exit point (a protect/intercept
objective). Every role rides the pilot stack of §2–§3.5 — encounter enemies
obey the same avoidance, stances and formations as everything else, which is
what makes them *feel* like Homeworld opponents rather than turrets.

**Runtime — `EncounterDirector`** (a sibling of `SpawnDirector`, stepped at
the same cadence, seeded LCG stream of its own):

- **Placement & trigger:** instances are armed at deterministic world
  anchors (per-system, from the galaxy seed — e.g. "the derelict 40k above
  the gate") or by explicit hooks (a mission acceptance, a misjump). A
  player entering the trigger radius activates the instance; waves spawn on
  their delays via the existing component-assembly path, each member tagged
  `EncounterMember{encounterId, waveIndex}`.
- **Lifecycle:** `Armed → Active → Resolved(success|failure) → Cooldown →
  Armed`. Objectives are evaluated in the director's step from world state
  (members alive, protect-target alive, timer). Resolution pays the reward
  to *participants* (players whose fire damaged encounter members — the
  bounty attribution path already tracks killers; participation extends it),
  broadcasts a result line, and despawns/releases survivors to ambient AI.
- **The first three encounters** prove the format: (1) **Witchspace ambush
  ported** — the existing 1–4 Thargoid misjump re-expressed as
  `EncounterDef{DestroyAll, waves:{Thargoid×rand(1..4), Assault,
  Aggressive}}`, deleting the bespoke spawn code; (2) **Pirate nest** — a
  Guard patrol around a loot cache, one Ambush wave; (3) **Convoy raid** —
  a Courier trader + Guard escorts, the player may attack (crime rules
  apply!) or defend against a scripted pirate Assault wave. All three reuse
  every mechanism above; none adds a system.
- **MMO discipline:** encounters respect AOI (they are ordinary entities),
  a per-region active cap (the pirate-spawner cap pattern), and the
  determinism rules (armed anchors and wave rolls from seeded streams —
  reproducible in headless tests). *Instancing is explicitly out of scope*;
  if it is ever wanted, an `EncounterDef` is world-agnostic and could be
  spawned in a private region, which is why the definition carries no
  absolute coordinates — anchors are supplied by the director.

**Missions vs encounters:** roadmap #5 ("Missions") is the *offer/accept/
journal* layer — economy and narrative. This design deliberately builds the
layer *below* it: missions will point at `EncounterDef`s ("clear the pirate
nest at X"), so the encounter system ships value on its own (ambient world
events) and de-risks missions later.

### 3.8 PvP — the same AI, the same guardrails

PvP needs no separate AI; it needs the AI to be **safe, legal, and
server-owned**. The design delivers each by construction:

- **No ram-griefing by proxy:** an ordered unit cannot be steered into
  another player's hull — `StepAvoidance` is unconditional and server-side;
  no client toggle exists to disable it. Deliberate manual ramming remains
  what it is today (§6.9 — dangerous, legal, and now *only* achievable by a
  hull that isn't under AI flight, i.e. never by your fleet "accidentally").
- **No accidental crimes:** auto-engagement (stances, escorts, encounter
  enemies) passes the same protected-victim legality as NPC fire today
  (§6.3); ordering an attack on a clean player keeps its menu-only friction
  and crime-at-order-time attribution. Evasive is the "never start
  anything" stance for haulers on Route through pirate space.
- **Fair launches:** auto-undock (§3.6) inherits launch immunity and the
  born-clear placement — a player cannot be spawn-camped into contact
  damage during a queued fleet launch; conversely the launch scheduler's
  determinism means no launch-spam to shield-stack a bay area.
- **Fugitive rules hold:** auto-undock and Route docking go through
  `CanDock` — a fugitive's hauler is refused doors like its owner (§6.3),
  no bypass via order automation.
- **Escalation stays consensual:** stances shape *how hard* units fight,
  never *whom* they may fight — that remains the Team/Wanted (and future
  FactionId, §13.2-4) legality layer. When factions land, the stance table
  is unchanged; only `PoliceMayEngage`-style legality predicates widen.

### 3.9 Wire & protocol additions (all reliable Gameplay lane)

| Message | Dir | Content | Notes |
|---|---|---|---|
| `UnitOrder` v2 | C→S | + `uint8_t queue` flag; + optional `groupId/groupSize/slot` triple on group orders | one flag byte + three small fields; validator unchanged per unit |
| `SetStance` | C→S | `unitId`, `TacticsStance` | acked; ownership-validated |
| `UnitOrderAck` | S→C | + `OrderStatus::Launching` | existing message, one enum value |
| `EncounterEvent` | S→C | encounterId, phase, objective state | HUD line + objective ticker; AOI-scoped |

Nothing rides the unreliable lane; the `InputCommand` heartbeat is
untouched. Snapshot replication needs one addition: `FormationMember`
and `UnitStance` replicate for **owned** units only (selection UI), via the
existing owned-fields path.

---

## 4. Flexibility: how the concept adapts

The brief's core demand — *"flexible to adapt based on situations"* — is
answered structurally, not by any single feature:

1. **Stance × situation = parameters.** The conduct layer reads a tiny
   per-tick `Situation` snapshot (under fire? energy fraction? target class?
   outnumbered in the cell?) and the stance row. New situational behaviour
   = a new column in `STANCE_TABLE` or a new situation predicate — never a
   new brain. (Example future row: "formation under fire → Sphere
   tightens", one column, zero new systems.)
2. **Roles compose from verbs.** Encounter roles, police warrants, trader
   lanes and player orders are all `PilotGoal` producers over one pipeline.
   A new PvE behaviour ("strafing run then extend") is a new goal producer,
   ~50 lines, inheriting avoidance/stances/formations for free.
3. **Archetypes are rows.** Enemy definitions move from hardcoded assembly
   into the archetype table (§3.7) — an authored encounter can field any
   hull with any loadout without touching spawn code, and the table is the
   future JSON schema.
4. **The safety layer is invariant.** Avoidance sits *below* all of it —
   whatever new behaviours are added above, the no-crash guarantee cannot
   be regressed by them, only by editing `StepAvoidance` itself (guarded by
   the golden test).
5. **Tuning is centralized and deterministic.** Every constant this design
   adds lives beside the existing `AiSystem.h:51–127` block; every random
   draw comes from a caller-owned seeded LCG stream. Behaviour changes are
   diffable, testable, and replayable — the project's standing determinism
   asset extends to the whole fleet layer.

---

## 5. Testing strategy

All of it headless GoogleTest under `Tests/GameLogic/`, real integrator, no
Windows dependency — the house method (`AiSystemTests.cpp` et al. are the
templates):

- **The no-crash golden test (the crown jewel):** seed a 24-ship furball
  (mixed orders, stances, formations, one encounter Assault wave) in a box
  with a station; run 20 000 ticks; assert **zero** ship↔ship and
  ship↔station contacts among AI-driven hulls, for a spread of seeds. This
  single test *is* the §3.2 guarantee, and it gates every future AI change.
- **Right-of-way determinism:** two head-on hulls, assert the
  lower-index-climbs rule and byte-identical replays across runs.
- **Stance matrix:** for each stance × (fired-upon / enemy-in-range /
  half-energy / leash-exceeded), assert engage/ignore/flee/return per the
  §3.3 table.
- **Formation convergence:** band of 5, Move 50k units; assert slot error
  under a bound after N ticks, group arrival within a window, slowest-member
  speed matching, avoidance-deflection recovery.
- **Queue/Patrol/Route:** waypoint progression, loop wrap, unqueued-order
  clears queue, Route dock→rearm→undock cycle including the fugitive
  refusal.
- **Launch scheduler:** 5 queued launches, assert stagger spacing and no
  corridor overlap; order-held-until-released semantics
  (`OrderStatus::Launching`).
- **Encounter lifecycle:** trigger→waves→objective→reward→cooldown for the
  three seed encounters; witchspace-port parity test (same distribution of
  ambush sizes as the legacy code it replaces).
- **BotClient soak:** a scripted bot scenario ordering formations through a
  contested region for the 100-player milestone — the AI layer must not
  move the tick budget materially (§13.3: measure before parallelizing).

---

## 6. Phasing

Each phase is independently shippable and leaves the game strictly better;
order chosen so the *safety substrate lands first* and everything after
builds on it. Effort scale as ARCHITECTURE.md §14 (S ≤ a day-ish, M = days,
L = week(s)).

| # | Phase | Contents | Effort | Depends on |
|---|---|---|---|---|
| A1 | **Avoidance unification** | `Pilot.h` goal extraction (§3.1); `StepAvoidance` tick phase; ordered units avoid; focus-berth narrowing; right-of-way; the golden test | M | — |
| A2 | **Stances** | `UnitStance` + `STANCE_TABLE`; `SetStance` wire + ability-bar UI; archetype mapping (traders=Evasive…) | M | A1 |
| A3 | **Queues, Patrol, Route** | `OrderQueue` + queue flag; Patrol loop; Route dock chain (flight layer) | M | A1 |
| A4 | **Formations** | `FormationMember`, guide-point steering, Delta/Line/Sphere, group wire fields, escort Sphere default | M–L | A1, A2 |
| A5 | **Mother-station cycle** | Auto-undock on order (`Launching` ack), launch scheduler, dock rearm/repair | M | A3 (Route uses it) |
| A6 | **Encounters** | Archetype table, `EncounterDef`/roles, `EncounterDirector`, witchspace port + pirate nest + convoy raid, `EncounterEvent` | L | A1–A5 |
| A7 | **Data-driven archetypes/encounters** *(optional, later)* | Cold-path JSON loader for the §3.7 tables under `GameData/Encounters/` | S–M | A6 |

Fits the existing roadmap: A1–A5 slot before/alongside roadmap items #1–4
(they share no files with fog-of-war/territory/economy work); A6 lands the
substrate that roadmap **#5 Missions** was waiting on; A3's Route delivers
the flight half of **#3 hauler routes** early.

**Risks & mitigations:**

- *Avoidance vs. feel* — over-eager deflection makes fleets look drunk.
  Mitigate: the berth constants are the only knobs, the golden test bounds
  safety from below while replay review bounds feel from above; tune in A1
  before anything stacks on top.
- *Tick budget at fleet scale* — every-tick avoidance over many hulls.
  Mitigate: cell-local queries only, profile with BotClient (roadmap #15)
  before entity caps rise; the phase is embarrassingly parallel by design
  if §13.3-E7 ever needs it.
- *Wire creep* — four small additions is the ceiling; anything more
  (formation editing, encounter browsers) is client-side over existing
  state.
- *Scope creep in A6* — encounters intentionally reuse everything and add
  only the director + tables; any "just one more role" that needs a new
  system is deferred to missions.

---

## 7. Locked-decision compliance (ARCHITECTURE.md §12)

| Decision | This design |
|---|---|
| Server-authoritative; clients send intent | All AI server-side in `GameLogic`; new wire = validated commands + acks only |
| Unit orders as the anti-cheat boundary | `PlanUnitOrder` matrix extended, never bypassed; group/queue/stance all ownership-validated |
| Pointer-first indirect control | Stance buttons, shift-queue, group orders extend the existing grammar (`docs/interaction.md`); no keyboard-exclusive verb |
| Seamless world, no visible segments | Encounters are open-world entities; no instancing |
| GameLogic server-only; schemas shared | New components are plain serializable data in NeuronCore schemas; behaviour stays in GameLogic |
| Determinism kept for replays/tests | Seeded LCG streams, deterministic right-of-way/slots/scheduler, golden tests |
| Replication, not lockstep | New components ride the existing owned-fields snapshot path |
| Evolve, never rewrite | Every phase extends an existing header/system; zero rewrites, one added tick phase |
| Persistence invariant | `UnitStance`, `OrderQueue`, `FormationMember`, encounter state: plain components |
| 4X/RTS trajectory | Stances/formations/patrol/routes *are* the RTS toolkit; encounters feed missions (#5); Route feeds the living economy (#3) |
