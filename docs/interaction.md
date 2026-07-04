# DeepspaceOutpost — Interaction Design: the pointer-first command interface

**Status:** design accepted 2026-07-04 (owner decisions locked, see below);
implementation planned as **Track I** in `docs/IMPLEMENTATION.md`. This is the
canonical design document for how the player *interacts* with the game —
selection, orders, camera, abilities, screens — across **mouse and touch as
the primary devices**, with the keyboard reduced to optional accelerators.
ARCHITECTURE.md §13.2.4 summarizes this document; when they disagree, this
document wins for interaction, ARCHITECTURE.md wins for everything else.

**Owner decisions locked 2026-07-04:**

1. **Pure order-based control.** The player's ship is their first *unit*:
   right-click/tap issues validated orders (move, approach, dock, attack,
   collect); the server's existing autopilot flies the hull. Direct piloting
   does not return.
2. **Context orders + move gizmo.** Clicking an object issues its contextual
   default order; clicking free space places a Homeworld-style move marker on
   a camera-aligned plane through the ship, with a drag to adjust the third
   axis.
3. **Attack orders, server engages.** An Attack order sets the server-side
   target focus and the ship engages autonomously (the same fire discipline
   NPCs use); crime rules are validated against the *owner*. Missiles / ECM /
   energy bomb / escape pod become tappable HUD actions. There is no manual
   per-frame fire button.

---

## 1. Why this redesign

The 2026-07-04 free-camera refactor decoupled the camera from the ship: the
player now flies the **camera** (first-person free / orbit, toggled on F12)
and the ship renders as an ordinary third-person entity. In the same change,
hull piloting was retired — `send_player_input` (`main.cpp`) sends
`InputCommand` with **all flight axes hard-coded to zero**. Consequence: the
ship launches at rest and idles forever. **There is currently no way to move
the ship at all.** Movement must come back, and going back to keyboard
piloting would fight both the third-person view and the locked trajectory.

The locked trajectory (ARCHITECTURE.md §12) is a **4X/RTS-style MMO**:
*"Command/intent protocol — today flight axes, tomorrow unit orders"* and
§13.2.3's Darwinia thesis — **indirect control: the commander issues orders;
units pilot themselves**. The engine is unusually pre-adapted: NPCs already
fly by writing `FlightIntent` through the same pipeline as players (§6.1),
and roadmap item **#12 (`UnitOrder`)** was always the next feature. This
redesign simply makes the player's own ship the **first ordered unit** — the
right-click-to-move ask and the roadmap converge on one mechanism.

Modern-input goals, in priority order:

1. **Mouse-first**: every gameplay verb reachable with the pointer alone.
2. **Touch-equal**: the same verbs via tap/drag/pinch/long-press — no
   hover-dependent or keyboard-dependent path. (Windows touch via
   `WM_POINTER`; the platform stays Windows per §12.)
3. **Keyboard-optional**: keys remain only as accelerators; nothing is
   keyboard-exclusive.

---

## 2. Analysis — the interaction model as-built (2026-07-04)

Full inventory verified against source. The short version: **every gameplay
verb is a hard-coded key; the mouse only drives the camera and the docked
widget windows; touch is a single-pointer mouse emulation.**

### 2.1 Input capture

- One Win32 backend: `NeuronClient/platform/input_win.cpp`. `WM_KEYDOWN/UP`
  fill a VK-indexed held-state table; `kbd_poll_keyboard()` snapshots it into
  legacy `kbd_*_pressed` globals once per frame; `WM_CHAR` feeds a small ring
  queue used only by the chart "find planet" text entry.
- Mouse: absolute position, LMB/RMB with capture, wheel accumulation
  (`input_mouse_state()` / `input_take_mouse_wheel()`).
- **Touch today:** `WM_POINTERDOWN/UPDATE/UP` map the *primary* pointer to
  mouse position + LMB. No multi-touch, no gestures, no pinch.
- No gamepad. No rebinding (bindings are hard-coded in `kbd_poll_keyboard`).
- Modal GUI input: `GuiOverlay` suppresses the whole game-key snapshot
  (`input_suppress_game_keys`) while a window is open.

### 2.2 The key map (complete)

Flight/combat (`handle_flight_keys`, `main.cpp`): **A** fire laser, **T**
lock missile target (center-of-view auto-pick), **M** launch missile, **U**
unlock, **E** ECM, **Tab** energy bomb, **Esc** escape pod, **C** dock
request, **J** in-system jump, **H** hyperspace (on a chart). Screens:
**F1** launch/front view, **F4** equip, **F5/F6** charts, **F7** planet
data, **F8** market, **F9** commander, **F10** inventory, **F11** options,
**F12** camera FPV↔orbit toggle. Charts: **arrow keys** move the crosshair,
**D** distance, **F** find-by-name (text entry), **O** origin. Camera
(flight view): **arrows** move, **PgUp/PgDn** vertical, **Shift** boost.
The legacy speed/roll/climb piloting keys are dead (piloting retired).

### 2.3 Mouse & camera

- **RMB-drag** = mouse-look (FPV) / orbit rotate; **wheel** = dolly / orbit
  distance. Camera input is gated off whenever a GUI overlay is open or a
  non-flight screen is active (`CameraRig.cpp`).
- Two controllers (`NeuronClient/CameraController.cpp`): `FirstPerson`
  (free-fly) and `Orbit` (around the missile-lock target if any, else the own
  ship), toggled **only** by F12; the rig anchors behind the ship on
  spawn/teleport and publishes the floating origin.
- **LMB** is exclusively the GUI click button (the `Canvas` widget stack).

### 2.4 Screens & GUI

- Two state machines: `GameState` (Intro1 → Intro2 → Flight → GameOver;
  intros advance on **Space**) and `current_screen` (`SCR_*`): the flight
  view plus retro-canvas screens (charts, planet data, commander status)
  navigated **only by F-keys**.
- A Darwinia-derived widget stack (`NeuronClient/gui/`: `Canvas`,
  `GuiWindow`, `GuiButton`, `GuiOverlay`) already handles the Market, Equip,
  Options, Commander and Inventory windows with **mouse-clickable rows** —
  a real, working foundation for pointer UI.
- **The charts have no mouse support at all**: the hyperspace crosshair
  (`cross_x/cross_y`) moves only with arrow keys, despite the widget system
  sitting right there.

### 2.5 Targeting

`find_lock_target()` (`space.cpp`) picks the nearest ship inside the central
view cone — a cockpit-era mechanism (aim the *view*, press T). The lock
doubles as the orbit camera's subject and the HUD reticle. There is no
click-to-select of any kind.

### 2.6 Raw input → wire

Discrete combat intents publish `ActionTriggered` (id `0x8200`, LocalOnly)
onto the client bus; a command-builder subscriber accumulates per-frame flags;
`send_player_input()` assembles `InputCommand` (`0x0100`, unreliable, 30 Hz):
zeroed axes + `fire/fireMissile/missileTarget/ecm/energyBomb/escapePod` +
`ackSnapshotTick` (the delta-stream ack — the cadence must never stop).
Station screens send `StationRequest`; charts send
`TravelRequest{Hyperspace}`; **J** sends `TravelRequest{InSystemJump}`.

### 2.7 Assessment

| Problem | Evidence | Consequence |
|---|---|---|
| **The ship cannot move** | axes hard-coded 0 in `send_player_input` | The game's core verb is missing since the camera migration |
| Every verb is a bare key | `kbd_poll_keyboard` + `handle_flight_keys` | Untouchable on touch; undiscoverable; 20+ bindings to memorize |
| One-shot actions ride an unreliable lane | missile/ECM/bomb/pod flags on `InputCommand` | A lost datagram silently eats a button press — unacceptable for tap UI |
| Targeting assumes a cockpit | center-of-view cone + T | Meaningless in third person, where the camera is not the gun |
| Charts are keyboard-only | `move_cross` arrow-key crosshair | The most map-like screen in the game rejects the pointer |
| Touch = 1 emulated mouse | `WM_POINTER*` → LMB only | No pinch zoom, no two-finger camera, no long-press |
| Docking is implicit | proximity + nose-on-station heuristic sends Dock | Opaque; fails silently; needs piloting precision the player no longer has |

What is **worth keeping**: the `ActionTriggered` bus decoupling (device →
intent seam), the widget stack (already pointer-driven), the two camera
controllers (they become modes of one pointer camera), the entity-picking
math (`camera_view_point` + the shared projection), and above all the wire
discipline — orders will be validated Commands like everything else.

---

## 3. The interaction model (the design)

### 3.1 Principles

1. **The ship is a unit, not an avatar.** You command; it pilots. Everything
   the player does is *select → order* or *tap an ability*.
2. **One pointer grammar for mouse and touch.** Every interaction is defined
   in pointer verbs (primary-select, command, context, drag, zoom) with a
   mouse binding and a touch binding. No verb exists in only one device.
3. **Orders are wire Commands.** Every order is validated server-side
   (ownership, legality, range) exactly like station requests — the
   anti-cheat boundary is unchanged.
4. **The keyboard is a convenience layer.** Every key is an accelerator for
   something the pointer can already do.
5. **Feedback is mandatory.** Every accepted order shows a marker/route line
   and the unit's current order on its status ring; every refusal says why
   (toast from the ack). Touch has no hover — feedback must not rely on it.

### 3.2 The pointer grammar

| Verb | Mouse | Touch |
|---|---|---|
| **Select** | LMB click | Tap |
| **Command (contextual default)** | RMB click | Tap a target while an own unit is selected |
| **Context menu (all orders + info)** | RMB hold ≥ 0.35 s | Long-press ≥ 0.35 s |
| **Camera orbit** | LMB-drag on empty space | One-finger drag on empty space |
| **Camera pan** | MMB-drag (or Shift+LMB-drag) | Two-finger drag |
| **Camera zoom** | Wheel | Pinch |
| **Focus camera on unit** | Double-click it | Double-tap it |
| **Cancel / stop** | RMB on own selected ship → Stop (also on the action bar) | Action-bar Stop button |

Selection notes:

- Selecting is free of consequence: any entity can be selected (own ship,
  enemy, station, planet, canister) — selection shows an **info card**
  (name/type/legal status/distance) and becomes the orbit camera's subject.
- Orders apply only when the selection is an **own unit** (today: exactly the
  primary ship; F1's escorts join for free).
- On touch, "tap = select" and "tap = command" collide; resolution: a tap
  always *selects* unless an own unit is already selected and the tap lands
  on a **different** entity or empty space — then it is a command. Tapping
  another *own* unit always re-selects. A visible **selection chip** at the
  screen edge shows the currently commanded unit at all times, with an ✕ to
  deselect (returning taps to pure selection).

### 3.3 Contextual default orders

The command verb (RMB / tap-with-selection) issues the target's default:

| Target | Default order | Server behaviour |
|---|---|---|
| Empty space | **Move** (via the move gizmo, §3.4) | Autopilot flies to the point, eases in, stops |
| Enemy ship (NPC or wanted player) | **Attack** | Sets `focus`, engages with NPC fire discipline |
| Clean player ship | **Approach** (Attack only via context menu — deliberate friction; the crime is validated and attributed to the owner) | Formate at a standoff distance |
| Own ship (selected) | **Stop** | Zero intent, clear order |
| Station | **Dock** | Autopilot to the dock approach, then the existing Dock request path |
| Planet | **Approach** | Fly to a safe standoff (outside the 4000 kill radius) |
| Cargo canister | **Collect** | Autopilot within scoop range (600); the existing `ScoopSystem` does the rest |
| Own escort (post-F1) | **Escort me** | The F1 escort order |

The long-press/RMB-hold **radial context menu** lists *all* legal orders for
the target (e.g. station: Dock / Approach; enemy: Attack / Approach) plus
**Info**. Radial, large hit targets, works identically for mouse and touch.

### 3.4 The move gizmo (free-space destination)

Turning a 2D click into a 3D point, Homeworld-style, adapted for touch:

1. Command verb on empty space → a **command plane** appears: the plane
   through the selected ship's position whose normal is the camera's up
   vector (so it always faces the view sensibly; no gravity plane exists in
   space). A subtle depth-faded grid renders on it — this doubles as the
   tactical-grid element of the retro-vector art direction (§13.2.1).
2. The click ray ∩ plane = the **in-plane point**; a marker and a line from
   the ship preview the path, with the distance printed.
3. **Drag before release** (mouse: keep RMB held; touch: keep the finger
   down) switches to **elevation**: vertical pointer motion slides the marker
   along the plane normal, drawing the classic vertical stem from plane to
   marker.
4. Release confirms → `UnitOrder{Move, targetPos}`. A tap/click without drag
   is simply a move on the plane (the common case stays one gesture).
5. While an order is active, the marker + route line persist (dimmed) until
   arrival or countermand; the info card shows "Moving — 12.4 km".

Edge cases: a ray nearly parallel to the plane (camera looking along its own
up vector is impossible; near-grazing angles clamp the point to a max
command range, e.g. 500 k units); orders are also range-clamped server-side.

### 3.5 Combat

- **Attack order** (command verb on an enemy): the server sets
  `Combatant.focus` and enables engagement for the *ordered* target only —
  the ship closes with the existing attack-run steering and fires with the
  NPC fire discipline (`fireInterval`, laser temperature still applies).
  "Players fire only on command" (§6.2) is preserved: the command is now the
  order, one level up. Attacking a protected victim is validated **at order
  time and at fire time** and publishes `Crime` against the **owner**
  (consistent with F1's escort rule).
- **Disengage**: Stop or any new order clears the engagement.
- **Missiles, ECM, energy bomb, escape pod** move to the **ability bar**
  (§3.7) and a new reliable message: one tap = one attempt, acknowledged.
  A missile launch requires a selected enemy (the selection replaces the old
  T-lock; the server's G2 validation gates it). The center-of-view
  `find_lock_target` cone-pick retires.
- PvP pacing note: laser DPS, shields and regen are untouched; what changes
  is *who aims* — the server, symmetric to how it already aims for NPCs. Lag
  compensation (E1) continues to apply to the shooter's view of the target.

### 3.6 Camera

- **Orbit becomes the default and primary camera**, subject = current
  selection (own ship when nothing else is selected). This is the natural
  third-person RTS camera and already exists.
- **Free camera** (the current first-person controller) remains as an
  *observer* mode behind a HUD toggle (and F12 as its accelerator) — useful
  for screenshots and, later, fleet overview; it needs no keys (drag-look +
  wheel/pinch dolly; on-screen joystick nub for translation on touch —
  low priority).
- Camera never consumes the command verbs: orbit is LMB-drag / one-finger
  drag (a *drag*, distinguished from click/tap by a slop threshold ~6 px /
  ~8 mm), command is RMB click / tap. No modes, no toggles for the common
  loop.
- The arrow/PgUp/PgDn camera-fly keys remain as free-camera accelerators
  only.

### 3.7 The ability bar (HUD)

A persistent row of large touch targets (bottom-right), state-driven from
`PlayerStatus` / `CargoManifest` mirrors — greyed when unavailable, badge
counts where relevant:

| Slot | Shown when | Sends |
|---|---|---|
| **Stop** | own unit selected & moving | `UnitOrder{Stop}` |
| **Missile** | rack > 0 & enemy selected | `AbilityRequest{FireMissile, target}` |
| **ECM** | owned | `AbilityRequest{Ecm}` |
| **Energy bomb** | owned | `AbilityRequest{EnergyBomb}` (confirm-press: hold 0.5 s) |
| **Escape pod** | owned | `AbilityRequest{EscapePod}` (confirm-press) |
| **Jump** | in flight, not mass-locked | `TravelRequest{InSystemJump}` |
| **Launch** | docked | the existing launch flow |

Destructive/irreversible abilities (bomb, pod) use **hold-to-confirm**, not a
modal dialog.

### 3.8 Charts, station screens, HUD

- **Charts become pointer surfaces**: tap/click a system to select it (info
  card: name, economy, government, distance, fuel cost, in/out of range);
  a **Hyperspace** button on the card sends `TravelRequest{Hyperspace}`.
  Pinch/wheel zooms between galactic and short-range scales (eventually
  merging the two chart screens into one zoomable map — F5/F6 stay as zoom
  presets). Drag pans. The arrow-key crosshair and **H** retire; find-by-name
  becomes a search field on the chart (the text-entry path already exists).
- **Station screens** already run on the widget stack; the pass here is
  ergonomic: minimum 40 px row heights / touch spacing, wheel + drag-scroll
  in lists, and Buy/Sell quantity steppers with press-and-hold repeat.
- **HUD**: the scanner stays (camera-relative); selected-entity info card
  (top-left), selection chip + ability bar (bottom), order feedback markers
  in-world. Screen navigation (charts/market/status) moves to a compact
  icon strip (top-right) — the F-keys remain as accelerators.
- **Intro/game-over**: "press Space" becomes "tap/click anywhere" (Space
  stays as accelerator).

### 3.9 Keyboard policy (the residue)

Everything below is optional; nothing is exclusive:

| Keys | Accelerate |
|---|---|
| F1/F4–F11 | screens & windows (unchanged) |
| F12 | camera mode toggle |
| Esc | close window / (hold) escape pod via ability-bar focus |
| Space | advance intro; Stop (in flight) |
| Arrows/PgUp/PgDn/Shift | free-camera fly |
| Delete/Backspace | deselect |

The chart crosshair keys, T/M/U/A/E/Tab/C/J/H gameplay keys retire with
their mechanisms (their verbs live in the pointer grammar and ability bar).

---

## 4. Protocol & server design

### 4.1 `UnitOrder` / `UnitOrderAck` (extends roadmap #12, shared with F1)

This is F1's planned message, generalized with positional orders and adopted
*now* for the primary ship — the escort later reuses it unchanged.

**`UnitOrder`** — `0x1010` · Wire · Command · Gameplay · C→S:

| Field | Type | Meaning |
|---|---|---|
| `unitId` | u32 | the ordered unit (today: your primary ship) |
| `order` | u8 | `Stop=1, Move=2, Approach=3, Dock=4, Attack=5, Collect=6, Escort=7` (Patrol/Route reserved for F-track) |
| `target` | u32 | target entity index or `0xFFFFFFFF` (Move/Stop) |
| `targetX/Y/Z` | i64 ×3 | destination (Move; ignored otherwise) |

Validation: `LiveEntity(unitId)`, `Owner == sender's PlayerId`, order-legal
target (Attack: live combatant, not self, crime rules evaluated with the
**owner** as offender; Dock: a station; Collect: a canister), destination
range-clamped (Chebyshev, e.g. ≤ 1 M units from the unit), docked units
accept only Undock-flavored flows (launch stays a station flow). One active
order per unit; a new order replaces the old (latest wins). Reliable lane —
an order must never be silently lost.

**`UnitOrderAck`** — `0x1011` · Wire · Event · Gameplay · S→C (owner):

| Field | Type | Meaning |
|---|---|---|
| `unitId` | u32 | echo |
| `order` | u8 | echo |
| `status` | u8 | `Accepted=0, NotYours=1, BadTarget=2, Illegal=3, OutOfRange=4, Docked=5, Rejected=6` |

The client shows refusals as a toast on the selection chip and clears the
optimistic marker. (Order *completion* needs no message — arrival is visible
in the snapshot stream; the client clears the marker by proximity.)

### 4.2 `AbilityRequest` — one-shot actions off the unreliable lane

**`AbilityRequest`** — `0x1014` · Wire · Command · Gameplay · C→S:
`kind u8 {FireMissile=1, Ecm=2, EnergyBomb=3, EscapePod=4}`, `target u32`
(missile lock target; `0xFFFFFFFF` otherwise). Server routes to the same
handlers `InputCommand`'s flags reach today (`FireWeapon` bus / equipment
system), with G2's missile-target validation. Outcomes already ride existing
events (`EcmPulse`, `EntityDeath`, `EscapePodUsed`, `PlayerStatus` missile
count) — no ack message; a rejected ability simply does nothing visible
beyond the status mirrors, which is today's behaviour but *reliable*.

### 4.3 `InputCommand` re-cut → the heartbeat/ack

With axes and buttons gone, `InputCommand`'s one remaining duty is the
delta-stream ack cadence. Pre-launch (no back-compat, same rule used by
B1/C1), re-cut `0x0100` in place with a `PROTOCOL_VERSION` bump:
`{sequence u32, ackSnapshotTick u32}` — conceptually a **ClientHeartbeat**;
keep ~10–30 Hz cadence (it also feeds the B3 safe-park silence detection).
The `fire/fireMissile/missileTarget/ecm/energyBomb/escapePod/rollAxis/
pitchAxis/throttle` fields are deleted; the `ActionTriggered` client bus
message shrinks to the ability set. Post-launch this would have been a
successor id — note it at the definition per §4.3's ABI rule.

### 4.4 The server `OrderSystem`

A new GameLogic system, before `StepAi` in the tick:

- **Components:** `ActiveOrder{order, target, targetPos}` on the ordered
  unit (plain serializable — persistence-ready per §12's standing
  invariant).
- **Execution = the existing autopilot.** Each tick, the order is translated
  into a `FlightIntent` using the steering that already exists: the
  trader-lane autopilot for Move/Approach/Dock/Collect (deadzone steer,
  full-throttle when aligned, ease-off on approach, stop inside the arrival
  radius) and the attack-run/track steering for Attack (`focus` +
  engagement). *No new steering math* — exactly the §13.2.3-2 promise, and
  NPCs and ordered players remain the same kind of thing.
- **Arrival:** Move completes inside an arrival radius (e.g. 200 units),
  order cleared, intent zeroed. Dock completes by handing over to the
  existing dock-range request path (server-initiated `Dock` when in range,
  nose-aligned — the reliable, explicit replacement for the client-side
  proximity heuristic). Collect completes when the canister despawns
  (scooped/smashed) or expires.
- **Safety rails**: ordered flight is still clamped by `FlightCaps`
  (`ResolveIntent` unchanged — an order can never out-fly the hull);
  collision, mass-lock, crime, laser-temperature rules all apply
  unmodified. Planet Approach standoff > the 4000 kill radius; station
  approach uses the existing 2000-unit launch offset as its waypoint.
- **Interaction with damage**: being attacked does not auto-change orders
  (the commander decides); the HUD makes incoming fire loud instead.

### 4.5 Anti-cheat & determinism posture

Unchanged in kind: orders are Commands validated against ownership, ranges,
legality; positional destinations are clamped; the client still cannot move
itself an inch — it asks, the server flies. `OrderSystem` is headless,
deterministic (no RNG needed; any future formation jitter takes a seeded
stream), golden-testable like every other system.

---

## 5. Client architecture changes

| Piece | What | Builds on |
|---|---|---|
| **Gesture layer** | Full `WM_POINTER` multi-pointer tracking + a small recognizer (tap, double-tap, long-press, drag, two-finger drag, pinch) emitting device-neutral `PointerEvent`s; mouse synthesizes the same events (wheel → pinch-equivalent zoom). Lives in `NeuronClient/platform/input_win.cpp`'s successor (`PointerInput`). | existing `WM_POINTER` stub |
| **Picking** | Screen-ray entity pick: project replicated entities through the one shared projection, nearest within a distance-scaled screen radius (generous on touch). | `camera_view_point`, `find_lock_target`'s math |
| **Selection state** | Client-local `Selection{entityId}` + info card + selection chip; feeds the orbit camera target (replacing the missile-lock coupling). | `CameraRig` orbit target seam |
| **Order UX** | Contextual default resolution, radial menu, move gizmo (plane math + grid render), in-world order markers/route lines, ack toasts. | widget stack for the menu; `Scene3D` for markers |
| **Ability bar** | GuiOverlay-hosted persistent bar (non-modal — must not suppress the world the way modal windows do), availability from `PlayerStatus` mirrors, hold-to-confirm. | `GuiOverlay`, `GameWindows` patterns |
| **Chart interaction** | Pickable systems, info card + Hyperspace button, pan/zoom; crosshair keys retired. | chart render + `GalaxyChunk` data already client-side |
| **Touch ergonomics pass** | 40 px minimum targets in Market/Equip rows, drag-scroll, steppers. | `GameWindows.cpp` |

The `ActionTriggered` bus stays as the device→intent seam: pointer verbs
publish intents (`OrderIssued`, `AbilityTriggered`) the same way keys do
today, so keyboard accelerators and pointer UI converge on one path.

---

## 6. Implementation plan — Track I

Efforts use the repo scale (XS ≤ hours, S ≤ a day-ish, M = days, L = weeks).
Sequenced so the game is playable again as early as possible. Full specs and
acceptance criteria live in `docs/IMPLEMENTATION.md` §Track I (this table is
the summary).

| # | Item | Contents | Effort | Depends on |
|---|---|---|---|---|
| **I1** | Wire protocol + `OrderSystem` | `UnitOrder`/`UnitOrderAck`/`AbilityRequest`, `ActiveOrder` component, autopilot-backed execution, crime-validated Attack, `InputCommand` re-cut to heartbeat. Headless tests. **This alone restores ship movement.** | M | — |
| **I2** | Selection & picking | Pointer events (mouse first), entity picking, selection state + info card, orbit-follows-selection, T-lock retired (missile target = selection). | S–M | — (parallel with I1) |
| **I3** | Command UX | RMB contextual orders, move gizmo, radial menu, order markers/ack toasts, Stop. | M | I1 + I2 |
| **I4** | Ability bar & HUD | The bar, hold-to-confirm, availability states; Dock/Jump/Launch buttons; icon strip for screens. | S | I1 |
| **I5** | Touch layer | `WM_POINTER` multi-touch + gesture recognizer; camera drag/pinch; touch mappings of §3.2; ergonomics pass on widget rows. | M | I2–I4 |
| **I6** | Charts pointer pass | Pickable systems, hyperspace from the info card, pan/zoom, search; crosshair keys retired. | S–M | I2 |
| **I7** | Keyboard reduction & cleanup | Retire dead keys/globals, accelerator table (§3.9), doc pass. | XS–S | I3–I6 |

Milestone framing: **I1 is urgent** (movement is broken); I1+I2+I3 form the
minimum "command your ship" experience; I4–I7 complete the model. F1 (escort)
afterwards reuses `UnitOrder` and the whole selection/command UX for free —
ordering an escort is selecting a different unit.

### Testing

- **Headless (CI):** order validation matrix (ownership, legality, ranges,
  crime attribution), autopilot arrival/stop convergence golden tests (same
  seed → bit-identical), Dock/Collect handover, ability routing + missile
  validation, heartbeat/ack continuity across the `InputCommand` re-cut,
  message round-trips + governance for the three new/changed ids.
- **Client (manual + BotClient):** BotClient gains an "order bot" that moves
  by `UnitOrder` (replacing its zero-axis idle), which doubles as the load
  harness for ordered fleets. Pointer/gesture layer gets a small
  event-injection unit test target; the rest is the manual Windows pass
  (mouse + a touch device).

---

## 7. Deliberately out of scope (for now)

- **Multi-select / drag-band selection and group orders** — designed-for
  (the grammar and `UnitOrder` carry one unit; group = N messages or a
  batched successor) but pointless before F1 gives a second unit.
- **Formation flying, patrol routes, rally points** — F-track (`Patrol`,
  `Route` order kinds reserved).
- **On-screen virtual joystick for the free camera on touch** — observer
  mode is mouse-comfortable already; revisit with real device feedback.
- **Rebindable keys** — the keyboard is accelerators-only now; revisit if
  accessibility feedback demands it.
- **Gamepad** — no demand yet; the pointer grammar maps naturally to a
  cursor-driven pad scheme later if wanted.
