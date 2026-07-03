# Phase G — Multiplayer Gameplay Plan

This plan supersedes the "suggested immediate next step" in
`docs/MIGRATION_ROADMAP.md` §6: **Phase F (Persistence) is deferred by owner
decision**, and Phase G — multiplayer gameplay — is next. It merges the two
remaining bodies of gameplay work into one sequenced plan:

1. The unfinished **Phase G bullets** — player identity/names, chat,
   PvP rules, legal status across players.
2. The **§7 server-gameplay-parity backlog** — bounty, shields, loot/scooping,
   explosions, NPC AI, collisions, hyperspace fuel/witchspace, ECM, equipment.

Everything here is server-authoritative: all rules land in `GameLogic`/`Server`,
the client stays a thin presentation layer (events + snapshots + UI), per the
locked architecture in the roadmap §0/§2.

---

## 0. Decisions locked for this phase (project owner, 2026-07-02)

| Topic | Decision |
|---|---|
| Scope | **Both** the Phase G bullets and the §7 parity backlog, in one plan. |
| PvP model | **Elite-style consequences.** PvP allowed everywhere; attacking a *clean* player raises the aggressor's `Wanted` level, police respond, and stations refuse docking to fugitives. No artificial safe zones — the existing police/wanted system is the deterrent. |
| Death rules | **Respawn docked, drop cargo.** On death the victim's cargo drops as scoopable canisters at the death position; credits and equipment are kept; the player respawns *docked* at the nearest station. (Session-scoped anyway while F is deferred.) |
| Player names | **Client-sent at connect.** The client sends a commander name in a hello message (sourced from config/command line for now); the server sanitizes and de-duplicates it and replicates it to other players. Trivially upgradeable to accounts when F lands. |

### Consequences of deferring Phase F

- **All progress is session-scoped** — a server restart wipes wallets, cargo,
  kills, wanted levels. That is accepted for this phase.
- **Persistence-readiness is a design rule, not a feature**: every new piece of
  durable-in-spirit state introduced here must live in a plain component
  (`Wallet`, `CargoHold`, `Fuel`, `Wanted`, `PlayerRecord`, …) so Phase F can
  later serialize components without refactoring gameplay code.
- **Missions (§7 item 9) are deferred to after F.** Session-scoped mission
  progression that evaporates on restart isn't worth the (high) porting effort;
  see §8 Non-goals.

---

## 1. Where the code is today (grounding)

Verified against the tree on 2026-07-02. The server tick (~30 Hz) lives in
`Server/Main.cpp`; game rules are header-inline in `GameLogic/`.

**Already live end-to-end:** server-authoritative flight, player forward-laser
fire with aim cone (`CombatSystem.h` `ResolvePlayerFire`), homing missiles as
real entities (`MissileSystem.h`), police spawns on first offence
(`Server/Main.cpp:215`), docking/trading/equipping against per-station markets
(`StationServices.h`), procedural 256-system galaxy + charts + docked-only
teleport, death → game-over → respawn-in-place (`Server/Main.cpp:230–276`).

**Written and unit-tested but NOT wired into the live server** (the two
cheapest wins in the whole plan):

- `Combat.h:116` `ApplyKill(credits, score, bounty, inWitchspace)` — the
  legacy bounty/score payout. Only caller is `CombatTests.cpp`.
- `Combat.h:36` `ApplyDamageToShields(state, damage, hitFront)` — the legacy
  front/aft shield + energy-bank damage model, with `ShieldState` at
  `Combat.h:18`. Only caller is `CombatTests.cpp`.

**Confirmed gaps** (each maps to a work package below):

| Gap | Evidence |
|---|---|
| Kills pay nothing | `EntityKilled` handler (`Server/Main.cpp:230–276`) destroys the wreck without touching the killer's `Wallet`. |
| No shields; flat energy | Players spawn with `Combatant.energy=255` only (`ServerSessions.h:139–162`); nothing reads `ShieldState`; no regen tick. |
| No identity | `Session` (`ServerSessions.h:39–46`) has no name field; the whole handshake is `AssignPlayer{entityId}` on first input datagram. |
| Chat is a dead schema | `Chat{sender, text}` exists (`NeuronCore/Messages/Defs/CoreEvents.h:68`, id `0x0300`, Gameplay lane, `Direction::Both`) but is never sent, never decoded (`DeepspaceOutpost/main.cpp` `process_server_events` handles only `StationResponse`/`EntityDeath`/`EntityDespawn`), and the GUI has **no text-input widget** (`NeuronClient/gui/` has only `GuiButton` over a bare `Widget`; `TextRenderer` is output-only). |
| NPCs are stationary turrets | `SpawnDirector.h` adds `WorldTransform,Flight,Combatant` but **no `FlightIntent`/`Velocity`** — NPCs never move; `StepCombat` only fires at the nearest in-range enemy. |
| Spawned NPCs render as the wrong ship | `SpawnDirector::Step`/`SpawnPolice` set **no `NetType`**, so dynamic pirates/police replicate as `type=0` (default mesh), unlike the hand-placed `NetType{Viper}` pirate. |
| No loot/scooping | Nothing ever spawns cargo entities; `Equipment.fuelScoop` is purchasable but inert (grep: only `StationServices.h` assigns it). |
| Killed ships just vanish | Client plays an explosion *sound* on `EntityDeath` (`main.cpp:1187`) then `Forget()`s the entity — no debris/flash VFX. |
| No collisions | Only ranged Chebyshev distance checks exist; planets/stations/ships are pass-through. |
| Travel is free/instant | `Teleport` (`StationServices.h:421–446`) is docked-only, no fuel, no witchspace, no mass-lock; no fuel resource exists server-side. |
| ECM/energy bomb/escape pod inert | Purchasable (`EquipPlayer`, prices at `StationServices.h:171`) with zero runtime effect. |

**Legacy reference code** for every port is still readable in
`DeepspaceOutpost/`: `space.cpp` (`regenerate_shields:378`, `damage_ship:415`,
`enter_witchspace:1259`, `complete_hyperspace:1284`, `update_cabin_temp:322`),
`swat.cpp` (`launch_loot:254`, `check_target:337`, `explode_object:321`,
`activate_ecm:392`, `abandon_ship:1229`), `trade.cpp` (`scoop_item:137`),
`pilot.cpp` (NPC steering). These are the parity oracles — port behavior, not
code (they mutate single-player globals).

---

## 2. Cross-cutting design

Three pieces of shared plumbing that several work packages need. Build them
once, first (they are work package **G0**).

### 2.1 Player roster & identity (`PlayerInfo`)

- **New wire message `ClientHello`** (Control lane, reliable,
  ClientToServer): `{ protocolVersion, commanderName }`. The client sends it
  immediately after `Open()`. Name source for now: `newkind.cfg` /
  command line, defaulting to `"Commander"`.
- The implicit first-input spawn (`ServerSessions::OnInput`) stays — sessions
  must tolerate `ClientHello` arriving before *or* after the first input.
  Until a hello arrives, the server assigns `Commander-<n>`.
- Server sanitizes (length ≤ 20, printable ASCII, strip control chars) and
  de-duplicates (`Name`, `Name-2`, …). Stored on the `Session` **and** in a
  new `PlayerRecord` component `{ name, score }` on the player entity
  (persistence-ready).
- **New wire message `PlayerInfo`** (Gameplay lane, reliable,
  ServerToClient): `{ entityId, name, wantedLevel }`. Broadcast on join, on
  name change, and on wanted-level change; the full roster is sent to a new
  session after the manifest. Clients keep an `entityId → PlayerInfo` map.
- Names/legal status deliberately go over **reliable events, not snapshots** —
  they change rarely; snapshots stay hot-path-lean (roadmap §0 wire-format
  decision).
- Client display: name tag on the targeted ship + roster in a tab overlay;
  fugitive players show their legal status (this is the Phase G "legal status
  across players" bullet).

### 2.2 Private player status channel (`PlayerStatus`)

The HUD currently has no authoritative source for the local player's vitals
(credits arrive only piggybacked on `StationResponse`). Several packages
(shields, fuel, scooping, bounty) need one:

- **New wire message `PlayerStatus`** (Gameplay lane, reliable,
  ServerToClient, sent **only to the owning session**): `{ energy,
  frontShield, aftShield, fuel, credits, missiles, cargoUsed, wantedLevel,
  score }`.
- Sent on-change with a floor of ~4 Hz equivalent (dirty-flag checked each
  tick; no periodic resend when unchanged). This is cold-path state — tiny and
  rare compared to snapshots.
- Client: one handler updates the legacy HUD globals (`cmdr.credits`, shield
  bars, fuel gauge) that the retained cockpit HUD already draws.

### 2.3 Crime & PvP rules (extends the existing wanted system)

Elite-style, building on the existing `Crime` message and
`Wanted{level}` component:

- **Aggression rule:** a player laser/missile hit on a *clean* player
  (victim `Wanted.level == 0`) publishes `Crime` against the attacker —
  exactly like firing on police/stations today. Hitting a *fugitive* player
  (level ≥ FUGITIVE_THRESHOLD) is legal (bounty hunting).
- **Police response:** unchanged path — first offence triggers
  `SpawnDirector::SpawnPolice` near the offender; police `autoEngage` the
  fugitive (requires G5's team-targeting tweak: police must pick the
  *offender*, which the current nearest-enemy-by-team loop approximates and
  G5 makes explicit).
- **Docking refusal:** `ProcessStationRequest` `Dock` branch rejects players
  with `Wanted.level ≥ FUGITIVE_THRESHOLD` with a new
  `StationStatus::DockingRefused` — fugitives must cool off. **Wanted decay:**
  a slow server-side tick-down (legacy legal-status decay analogue) so refusal
  isn't permanent in a persistence-free world.
- **Kill payout:** killing a fugitive player pays that player's bounty
  (derived from their wanted level) via the same `ApplyKill` path as NPC
  bounties (G1).
- **No damage nulling anywhere** — no safe zones. The respawn grace
  (`invulnTicks`, already implemented) is the only protection window.

---

## 3. Work packages

Ordered by impact ÷ effort, honoring dependencies. Effort: **S** ≤ 1 day-ish,
**M** = a few days, **L** = a week+. Every package lands with unit tests in
`Tests/GameLogic/` using the in-house `TEST(Suite, Name)` /
`EXPECT_TRUE` macros (style reference: `CombatTests.cpp`,
`StationServicesTests.cpp`) and must be CI-green on the Windows runner.

### G0 — Shared plumbing (identity, status channel, crime rules) — **M**

The three designs in §2, minus UI:

1. `ClientHello`, `PlayerInfo`, `PlayerStatus` message defs in
   `NeuronCore/Messages/Defs/` (+ `REGISTER_MESSAGE`), serialization tests.
2. `PlayerRecord{name, score}` component; sanitize/de-dupe in
   `ServerSessions` (hello-before-or-after-first-input tolerated); roster
   broadcast on join/leave/change; roster replay to new sessions.
3. `PlayerStatus` dirty-flag sender in the server loop (after step 6,
   before snapshots).
4. Crime-rule extension in `ResolveFireWeapon`/missile detonation: clean-victim
   check → `Crime` against attacker; wanted decay tick; docking refusal +
   `StationStatus::DockingRefused`.
5. Client: decode `PlayerInfo`/`PlayerStatus` in `process_server_events`,
   populate roster map + HUD globals. (Chat UI is G2; no text input needed yet.)

*Acceptance:* two clients connect with names; each sees the other's name in
the roster; shooting a clean player makes the attacker wanted, police spawn,
and the attacker is refused docking until decay; HUD credits update live.

### G1 — Bounty & kill rewards + NPC `NetType` + explosion VFX — **S** (the quick wins)

§7 items 1 and 4, mostly wiring existing tested code:

1. **Bounty:** in the `EntityKilled` handler (`Server/Main.cpp:230–276`),
   resolve the killer's session/entity and apply
   `ApplyKill(wallet.credits, record.score, bounty, /*witchspace*/ false)`.
   Bounty source: a new `Bounty{tenthsCr}` component set at spawn by
   `SpawnDirector` (pirates > police(0) > station(0)), values from the legacy
   ship-data table. Fugitive players get a wanted-derived bounty (§2.3).
   Missile kills credit `Missile.owner` (already tracked).
2. **`NetType` for dynamic NPCs:** `SpawnDirector::Step`/`SpawnPolice` assign
   proper `NetType` so pirates/police render as the right mesh (one-line-each
   fix; also a prerequisite for per-type bounty sanity).
3. **Explosion VFX (client-only):** on `EntityDeath`, spawn a local debris/
   particle burst at the victim's last interpolated transform (port the look of
   `swat.cpp` `explode_object`/`exp_seed` debris); no server entity needed —
   the event already broadcasts.

*Acceptance:* killing a pirate pays its bounty (visible via `PlayerStatus`
credits + score bump), the wreck explodes visibly, and dynamic NPCs render as
Vipers/pirate hulls, not the default ship.

### G2 — Chat — **M**

1. **Server:** drain `Chat` from each session's reliable endpoint (same place
   station requests are drained, `Server/Main.cpp:348–361`); validate (length
   cap ~120, printable, ≥ 500 ms per-session rate limit); stamp
   `sender = session entity id`; broadcast to all sessions (global channel
   only, this phase).
2. **Client input widget:** build the missing piece — a minimal
   `GuiTextInput` (subclass of `Widget`, drawing via `TextRenderer`,
   caret + backspace + enter/escape), plus a chat log pane (last ~8 lines,
   fading, `TextRenderer` output). Enter opens the input and captures the
   keyboard (flight input suppressed while typing); enter sends
   `Chat` via the reliable Gameplay lane; sender ids render as names via the
   G0 roster.
3. Decode `Chat` in `process_server_events` → client bus → chat log.

*Acceptance:* two clients exchange messages by name; a flooding client is
rate-limited; typing never fires lasers.

*Dependencies:* G0 (names).

### G3 — Shields, energy regen & death rules — **M**

§7 item 2 plus the locked death-rule change. Restores combat survivability:

1. **`ShieldState` component** on players (`{front, aft, energy}` — reuse the
   struct from `Combat.h:18`), initialized to legacy caps at spawn/respawn.
2. **Wire `ApplyDamageToShields`** into every player-damage path:
   `ResolvePlayerFire` / `StepCombat` / missile detonation. Directional
   front/aft from the attacker's position vs the victim's `Flight.nose`
   (dot ≥ 0 → front). NPCs keep the flat `Combatant.energy` model — shields
   are a player feature, as in legacy.
3. **Regen tick** in `GameLogic::Tick`: port `regenerate_shields`
   (`space.cpp:378`) — energy bank recharges, surplus tops up shields
   front/aft. Replicated to the owner via `PlayerStatus`; client HUD shield
   bars go live.
4. **Death rules (locked decision):** rework the player branch of the
   `EntityKilled` handler:
   - Drop the victim's `CargoHold` contents as canister entities at the death
     position (needs the canister entity from G4 — land the entity def there,
     or stub with "cargo evaporates" until G4 merges; see sequencing).
   - Keep credits/equipment/score. Clear wanted (as today).
   - Respawn **docked at the nearest station** (`NearestStation` /
     `FindStationBySystem` + `DockState`, as `Teleport` arrival does) instead
     of in place. Client already handles the game-over → resume flow; verify
     the docked-screen entry path.

*Acceptance:* a player survives several pirate hits with visible shield/energy
depletion and regen; death relocates them docked at the nearest station minus
cargo; unit tests cover directional damage, regen caps, and respawn placement.

*Dependencies:* G0 (`PlayerStatus`); canister entity from G4 for the cargo
drop (the two packages can land in either order with the stub noted above).

### G4 — Loot drops & scooping — **M**

§7 item 3 — restores the kill → loot → sell loop, and feeds G3's death drops:

1. **Cargo canister entity:** components `WorldTransform`, `Velocity` (slow
   drift inherited from the wreck), new `LootItem{commodityIndex, units}`,
   `NetType{Cargo|Alloy|Rock}` (new ship-type ids matching legacy meshes), and
   a despawn timer (~2 min) so the world doesn't silt up.
2. **Drop on kill:** in the NPC branch of `EntityKilled`, roll drops per the
   legacy `check_target`/`launch_loot` tables (`swat.cpp:337, 254`): pirates
   drop cargo/alloys, asteroids (when added) drop rock. Player deaths drop
   their actual cargo (G3).
3. **`ScoopSystem`** (server, per tick): for each player with
   `Equipment.fuelScoop`, not docked, within legacy proximity
   (`distance < 170` scaled to the server's units — calibrate against the
   docking/collision ranges) of a `LootItem` → add to `CargoHold` (respect
   capacity), destroy the canister, notify via `PlayerStatus`. Without a
   scoop, contact destroys the canister (legacy behavior: you smash it).
4. Client: canisters render via the existing replicated-entity path once the
   `NetType` ids map to meshes; scoop success can reuse the pickup sound.

*Acceptance:* kill a pirate → canisters drift out → scoop → cargo appears in
hold → sell at station for profit. Tests: drop tables, capacity-clamped
scooping, no-scoop destruction, despawn timer.

*Dependencies:* G1 (kill handler refactor), G0 (`PlayerStatus`).

### G5 — NPC AI movement — **L** (the big one)

§7 item 5 — NPCs stop being stationary turrets. Port `pilot.cpp`/`swat.cpp`
tactics into a proper `GameLogic` system, in stages:

1. **Give NPCs the player's flight model:** spawned NPCs get
   `FlightIntent` + `FlightCaps`; `StepFlight` then integrates them like
   players. AI only writes intents — same authority boundary as clients (a
   deliberate architectural rhyme: *everything* flies by intent).
2. **`AiSystem` stage 1 — pursue & break off:** steer nose toward target
   (roll-then-pitch, legacy steering feel), throttle by range, break-away
   arcs when too close (legacy anti-ramming behavior).
3. **Stage 2 — flee & self-preservation:** flee vector at low energy,
   legacy retreat thresholds.
4. **Stage 3 — NPC missiles & ECM chance:** NPCs launch via the existing
   `MissileSystem` on legacy conditions; NPC ECM comes with G7.
5. **Stage 4 — explicit target selection:** replace nearest-enemy-by-team
   with legacy-style target memory (police fix on the *offender* from the
   `Crime` event; pirates prefer players/traders).
6. **Stage 5 (stretch):** ambient traders flying planet ↔ station lanes and
   station-launched Vipers (station spawns police instead of the current
   spawn-near-offender shortcut).

*Acceptance:* pirates visibly pursue, orbit, and flee; police chase the actual
offender; NPC missiles force ECM/evasion gameplay. Golden-run style
determinism tests on the steering math (integer/fixed-point, per roadmap §4).

*Dependencies:* G1 (`NetType` fix). Stages 1–2 unblock G6 (collisions matter
once things move).

### G6 — Collisions & environment — **M**

§7 item 6:

1. **Ship↔ship contact damage:** legacy `distance < 170`-style contact check
   (same calibrated range as scooping) in a server `CollisionSystem`; damage
   both parties (`ApplyDamageToShields` for players, energy for NPCs).
   Ship↔canister is already handled by G4.
2. **Planet/sun collision:** altitude check against planet radius → death;
   sun proximity → `update_cabin_temp` port (heat damage ramp; also enables
   legacy sun-skimming fuel scooping with `Equipment.fuelScoop`, which
   dovetails with G7's fuel resource).
3. **Station collision:** hitting the station outside a legal docking
   approach damages/kills (legacy `check_docking` crash rule); keeps the
   existing low-speed dock-request path as the safe way in.

*Acceptance:* ramming costs shields, flying into the planet kills, botched
station approaches hurt. Tests for range math and damage routing.

*Dependencies:* G3 (shield damage routing), ideally after G5 stage 1 (moving
NPCs make collisions meaningful).

### G7 — Hyperspace fuel, witchspace & in-system jump — **M**

§7 item 7 — travel gets a cost and a risk:

1. **`Fuel` component** on players (legacy 0–70 = 7.0 LY scale), full at
   spawn; shown via `PlayerStatus`; refuel priced at stations (extend
   `StationRequestKind` with `Refuel`, legacy pricing).
2. **In-flight hyperspace:** replace the docked-only `Teleport` with a real
   jump: target selected on the chart (existing crosshair flow), server
   validates range ≤ fuel, runs the legacy countdown, deducts fuel by
   distance (`complete_hyperspace` parity), relocates the ship near the
   destination planet (not docked). Docked teleport is removed (it was a
   stopgap; the owner-visible feature is the same key, now with rules).
3. **Witchspace mishap:** legacy small probability → arrive in deep space
   with Thargoid ambush (spawned via `SpawnDirector`); `ApplyKill`'s
   `inWitchspace` flag (already implemented!) finally gets its caller —
   no bounty in witchspace.
4. **In-system jump (`jump_warp`) + mass-lock:** fast-forward travel toward
   the planet, blocked when massive objects or hostiles are near (legacy
   mass-lock rules) — server-side speed multiplier, not teleport.

*Acceptance:* jumping costs fuel, out-of-fuel strands you, refuel works,
witchspace ambushes occur at legacy probability (seeded test), mass-lock
blocks in-system jump near the station.

*Dependencies:* G0 (`PlayerStatus` for the fuel gauge), G5 stage 1 for
Thargoids that fight back (soft).

### G8 — ECM & remaining equipment — **S/M**

§7 items 8 and 10 — make the purchased items real:

1. **ECM:** player-activated (new bit in `ClientInput`); server destroys all
   in-flight missiles within range (walk `Missile` entities), energy cost +
   cooldown, broadcast an `EcmPulse` event for the client's classic ECM
   sound/flash. NPC ECM chance hooks into G5 stage 3.
2. **Energy bomb:** one-shot; kills every non-station NPC within a large
   radius (legacy `detonate_bomb`); consumed on use.
3. **Escape pod:** on use, ship is lost — respawn docked (G3 path), cargo
   lost, credits kept (legacy insurance flavor without persistence).
4. **Laser temperature:** per-shot heat, overheat blocks firing, cools over
   time (`fire_laser` parity) — server-side field surfaced via `PlayerStatus`.

*Acceptance:* each item observably works and is consumed/cools per legacy
rules; tests per item.

*Dependencies:* G0, G3 (respawn path), G5 stage 3 (NPC missiles make ECM
worth buying — ship G8 after or alongside).

---

## 4. Sequencing

```
G0 plumbing ──► G1 bounty/VFX ──► G4 loot/scoop ──► G6 collisions
   │                │                                    ▲
   │                └────────► G3 shields/death ─────────┤
   ├──► G2 chat                                          │
   └───────────────► G5 NPC AI (stages 1–5) ─────────────┘
                          │
                          └──► G7 fuel/witchspace ──► G8 ECM/equipment
```

Suggested landing order (each row independently shippable and CI-green):

| Step | Packages | Rationale |
|---|---|---|
| 1 | **G0** | Everything else reports through identity/status/crime plumbing. |
| 2 | **G1** | Cheapest visible wins; wires already-tested code. |
| 3 | **G3 + G4** | Restores the survivability + reward loop (the core game). Land G4's canister entity first or stub G3's cargo drop. |
| 4 | **G2** | Chat — first *social* feature; needs only G0. Can run in parallel with 3 by a second contributor since it's mostly client UI. |
| 5 | **G5** stages 1–3 | The single biggest gameplay-feel jump. |
| 6 | **G6, G7** | World consequences: collisions, fuel, witchspace. |
| 7 | **G8, G5** stages 4–5 | Equipment payoff + AI polish. |

---

## 5. Protocol changes (summary)

All new messages follow the existing per-message `static constexpr MessageId`
pattern in `NeuronCore/Messages/Defs/` with `REGISTER_MESSAGE`, hand-rolled
binary via `DataWriter`/`DataReader`, and round-trip serialization tests.

| Message | Id block | Lane | Direction | New/changed |
|---|---|---|---|---|
| `ClientHello{version, name}` | Control | Control | C→S | New |
| `PlayerInfo{entityId, name, wantedLevel}` | Gameplay | Gameplay | S→C | New |
| `PlayerStatus{energy, shields, fuel, credits, missiles, cargoUsed, wanted, score}` | Gameplay | Gameplay | S→C (owner only) | New |
| `Chat{sender, text}` | `0x0300` | Gameplay | Both | **Exists** — gets senders/handlers/UI |
| `EcmPulse{source}` | Gameplay | Gameplay | S→C | New (G8) |
| `ClientInput` | — | Unreliable | C→S | +`ecm`, +`energyBomb`, +`escapePod`, +`jump` bits (versioned) |
| `StationRequestKind` | — | Gameplay | C→S | +`Refuel`; `Teleport` retired in G7 |
| `StationStatus` | — | — | — | +`DockingRefused` |
| `EntitySnapshot` | `NSNP` v1 | Unreliable | S→C | Unchanged this phase (names/status ride reliable events, keeping the hot path lean per roadmap §0) |

New `NetType`/ship-type ids: `Cargo`, `Alloy`, `Rock` (G4), plus correct
assignment of existing ids in `SpawnDirector` (G1).

---

## 6. Testing strategy

- **Unit tests per package** in `Tests/GameLogic/` (in-house
  `TEST`/`EXPECT_TRUE` macros, one `.cpp` per system, mirroring
  `CombatTests.cpp` / `StationServicesTests.cpp` / `SessionTests.cpp`).
  Determinism-sensitive math (AI steering, drop tables, witchspace rolls) uses
  seeded generators and golden expectations, per roadmap §4.
- **Multi-session tests** at the `ServerSessions`/bus level (two fake
  endpoints) for: roster replay, chat broadcast + rate limit, PvP crime
  attribution, kill-credit routing, death-respawn placement — the
  `SessionTests.cpp` pattern already supports this.
- **Serialization round-trip tests** for every new/changed message.
- **CI:** Windows runner (`ci.yml`) stays the merge gate — this container
  cannot compile the MSVC/DX11 tree (roadmap §5).
- **Manual multi-client validation** by the project owner per landing step
  (two clients + server), as done for prior phases. The **BotClient (Phase B)
  remains unbuilt** and is *not* a dependency of this plan, but G5's
  intent-driven NPCs deliberately exercise the same input path a future
  BotClient will use.

---

## 7. What "done" means for Phase G

Two strangers can meet in the same system and have the full core-game loop,
all server-authoritative:

- See each other's **named** ships; check the roster; **chat**.
- Fight NPC pirates that **maneuver**, take **shielded** damage, **explode**,
  and **drop loot** that can be **scooped** and **sold**.
- Earn **bounties** and score for kills.
- Attack each other anywhere — but ganking a clean player makes you a
  **fugitive**: police hunt you and stations **refuse you docking** until your
  status decays; killing a fugitive pays their bounty.
- Die and **respawn docked** at the nearest station, cargo scattered at the
  death site for others to claim.
- **Jump between systems on fuel**, risk **witchspace**, refuel at stations;
  ram things at your peril.
- ECM, energy bomb, escape pod, and fuel scoop **actually work**.

---

## 8. Non-goals (explicitly out of this phase)

- **Persistence (Phase F)** — deferred by owner decision. Design rule: new
  state lives in plain components so F can serialize it later without touching
  gameplay code.
- **Missions (§7 item 9)** — deferred until after F (session-scoped missions
  aren't worth the high port cost).
- **Strategic AOI tier, delta compression, quantization (Phase D)** and
  **client prediction (Phase E)** — unchanged.
- **BotClient / 100-player load (Phases B/H)** — unchanged; G5's
  intent-driven NPCs are a stepping stone, not a substitute.
- **Factions/empires, private chat channels, friends lists** — the 4X-ready
  identity model (Account → Empire → entities, roadmap §2.4) is respected by
  keeping `PlayerRecord` per-entity and the roster keyed by entity id, but no
  empire features ship in G.
