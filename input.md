# DeepspaceOutpost — Homeworld-Style Input Migration Plan

**Status:** technical migration plan, written 2026-07-06 against the as-built
code (`docs/ARCHITECTURE.md` §7, `docs/interaction.md` Track I, all I1–I7
residues verified in source). This document plans the transition of the
client's interaction model from the current *pointer-first order grammar* to a
**Homeworld-style interaction model**.

**Naming.** All *new* identifiers proposed here follow the canonical project
standard in [`.github/coding-standards.md`](.github/coding-standards.md):
types and functions in `PascalCase`, locals in `camelCase`, private members
`m_` + `camelCase`, constants and macros in `UPPER_SNAKE_CASE`, and enums as
`enum class` with an explicit underlying type and `PascalCase` enumerators.
File-scope statics/globals keep the project-universal `g_` / `s_` prefix (the
standard is silent on globals; this prefix is the one convention both source
tiers already share) with a `camelCase` body. New client-engine code lands in
`Neuron::Client` with `_camelCase` parameters. The legacy `DeepspaceOutpost/`
presentation tier still uses older snake_case patterns; per AGENTS.md
("move legacy code toward the target style when you touch it") the new code
this migration introduces adopts the target style. **Existing symbols are
referenced by their real names** (e.g. `handle_pointer_commands`,
`g_missile_lock_target`, `pick_entity_at_screen`) so the plan stays an
accurate map of the current code.

Nothing in this plan touches the server or the wire protocol's semantics: the
anti-cheat boundary (`UnitOrder` validated server-side, the client cannot move
an inch) is unchanged. This is a **client presentation-layer migration** with
one small optional wire addition (ownership replication for band-select,
§2.4).

---

## 0. The target model (Homeworld reference, completed)

The reference model, completed with the conventions Homeworld players expect
where the brief leaves gaps:

**Camera** — one persistent **focus point** the camera always orbits:

| Verb | Binding | Behaviour |
|---|---|---|
| Rotate | **RMB + drag** | Pitch/yaw orbit around the focus point |
| Zoom | **Wheel** (and **MMB-drag** vertical) | Exponential dolly toward/away from the focus point; never through it |
| Pan | **LMB + RMB together + drag** (chord), or **arrow keys / WASD** | Translate the focus point in the current screen plane (camera right/up) |
| Focus | **F** | Animate the focus point onto the current selection (smooth fly, ~0.5 s ease); with nothing selected, refocus the own ship |
| Rotate inertia *(polish)* | — | Small velocity decay on release so the camera settles rather than stops dead |

**Selection & commands:**

| Verb | Binding | Behaviour |
|---|---|---|
| Select | **LMB click** | Single-select the entity under the cursor; empty space deselects |
| Band select | **LMB drag** | Rubber-band box; selects all **own units** inside (pre-F1 that is exactly the primary ship — the mechanism ships multi-select-ready) |
| Add to selection | **Shift + LMB click/drag** | Additive select *(multi-unit era; degenerate today)* |
| Command (contextual) | **RMB click** (no drag) | The target's default order — the existing `OrderMenu::DefaultContextOrder` table (Attack/Dock/Collect/Approach; empty space → movement grid) |
| All orders | **RMB hold ≥ 0.35 s** | The existing radial menu (unchanged) |
| Movement grid | **M key**, or RMB click on empty space | The tactical plane (§0.1) |
| Stop | Ability-bar Stop (unchanged); **S** *(accelerator, optional)* | `UnitOrder{Stop}` |

**§0.1 The movement grid (the two-step 3D move):**

1. Activating the grid (M, or the RMB-on-empty command verb) spawns a
   **horizontal tactical plane** — normal **world +Y** — through the selected
   unit's position, rendered as the existing depth-faded grid.
2. **Pointer position** (hover on mouse, drag on touch) tracks the cursor-ray
   ∩ plane intersection: the **X/Z coordinate**. A line from the ship and the
   distance label preview the path (all existing gizmo furniture).
3. **Holding Shift** (or, in the drag-gesture form, continuing the drag that
   is already held) switches to **elevation**: vertical pointer motion slides
   the marker along **+Y**, drawing the vertical stem from plane to marker.
   Releasing Shift returns to X/Z placement with the elevation kept.
4. **LMB click** (M-mode) or **release** (drag mode) confirms →
   `UnitOrder{Move, targetX/Y/Z}`. **Esc / RMB / M again** cancels.

This is a superset of the shipped move gizmo: the gizmo's one-gesture form
(press-drag-release on RMB) remains as the fast path; the M-key form adds the
persistent, hover-driven grid mode Homeworld players expect.

**Touch (co-primary, per the locked interaction decision):** §5.

---

## 1. Current State Evaluation

### 1.1 The input architecture, layer by layer

Input flows through five layers, bottom to top:

1. **Win32 capture** — `NeuronClient/platform/input_win.cpp`. One
   `InputWndProc` on the `EventManager` chain: `WM_KEYDOWN/UP` → a VK-indexed
   held table (`g_key[256]`), `WM_CHAR` → a ring queue, mouse messages →
   absolute `g_mouseX/Y` + `g_lmb`/`g_rmb` with capture, `WM_MOUSEWHEEL` → a
   `g_wheelSteps` accumulator. `WM_POINTERDOWN/UPDATE/UP` (touch) feeds two
   parallel paths: an ad-hoc two-slot tracker that synthesizes mouse state
   (one finger → `g_mouseX/Y` + `g_lmb`; two fingers → pinch into
   `g_wheelSteps`) **and** the tested `Neuron::Input::GestureRecognizer`, from
   which only LongPress, DoubleTap and PanMove are consumed. **There is no MMB
   state** — `WM_MBUTTONDOWN/UP` is not handled anywhere.
2. **The device-neutral gesture core** — `NeuronClient/input/
   GestureRecognizer.h`. A pure, headless-tested pointer state machine
   (tap / double-tap / long-press / drag / two-finger pan / pinch) with
   explicit time. Today it is fed **only by touch**; the mouse path re-derives
   click-vs-drag slop by hand in two separate places (below).
3. **Camera input & selection** — `DeepspaceOutpost/CameraRig.cpp`
   (`camera_rig_update`, called once per frame from the flight update). It
   polls `input_mouse_state` directly and owns: LMB-drag = look/orbit
   (with its own 6-px slop copy at `CameraRig.cpp:43`), LMB click = select
   (`pick_entity_at_screen` → `g_missile_lock_target`), wheel consumption,
   the arrow/PgUp/PgDn/Shift key axes, the F12 FPV↔orbit toggle glue, the
   docked-mode forced orbit + RMB exception, and the floating-origin publish.
4. **Command input** — `DeepspaceOutpost/main.cpp`
   (`handle_pointer_commands`, called from the flight update *after* the
   camera). It polls `input_mouse_state` **again** and owns the whole RMB
   grammar with a second hand-rolled slop/hold state machine
   (`s_prevRmb`/`s_downX`/`s_moved`/`s_downFrames`, `LONGPRESS_FRAMES = 11` — a
   frame-counted 0.35 s that silently assumes 30 fps): RMB click → contextual
   order, RMB hold → radial menu, RMB press on empty → move gizmo with
   vertical-drag elevation. Touch long-press/double-tap one-shots are consumed
   here too.
5. **Pure decision/geometry cores** — `NeuronClient/input/OrderMenu.h`
   (target classification, default orders, menu contents, clean-player
   friction) and `NeuronClient/input/MoveGizmo.h` (ray-plane math, grazing
   clamp, elevation, server-reach Chebyshev clamp). Both headless-tested.
   Orders leave through `ReplicationClient::SendUnitOrder` (reliable);
   abilities publish `ActionTriggered` on the client bus and ride the
   `InputCommand` flags (the §13.1 unification residue, untouched by this
   plan).

Keyboard: `kbd_poll_keyboard()` snapshots the held table into legacy
`kbd_*_pressed` globals once per frame; the combat keys are already retired
(I7), leaving F-keys, chart accelerators (D/F/O + arrows), Space/Enter/Esc,
and the camera-fly arrows. `GuiOverlay::IsShown()` suppresses the game key
snapshot and gates both pointer layers; the ability bar and nav strip are
non-modal and are excluded from camera/selection clicks by explicit hit-test
calls (`ability_bar_button_at` / `nav_strip_button_at`) inside `CameraRig`.

### 1.2 The camera, as built

- Two controllers in `NeuronClient/CameraController.{h,cpp}`:
  `FirstPersonCameraController` (default: yaw/pitch look, key-axis fly, wheel
  dolly) and `OrbitCameraController` (yaw/pitch/distance around a target fed
  every frame; wheel = multiplicative distance, `ORBIT_MIN_DISTANCE` /
  `ORBIT_MAX_DISTANCE` 150–60 000). Both keep the **absolute eye in doubles**
  and render through `ApplyView(camera, originWorld)` against the int64
  floating origin — this contract is exactly right for the target model and is
  kept unchanged.
- The orbit **target is not a camera focus point**: it is slaved every frame
  to `g_missile_lock_target` (the selection) or the own ship
  (`CameraRig.cpp:179-192`). There is no free retargeting, no pan, and no
  focus animation — `SetTarget` snaps.
- **The orbit controller has no translate axis.** `input_win.cpp:150`
  documents this as the reason two-finger pan is accumulated but never
  consumed (`input_take_pan` has no caller — verified). This is roadmap
  item #8 ("two-finger camera pan"), which this migration subsumes.
- FPV is the default; orbit is opt-in via F12 and forced while docked.

### 1.3 Selection, as built

One global does four jobs: `g_missile_lock_target` (`main.cpp:155`) is
simultaneously (a) the selection, (b) the HUD reticle subject, (c) the orbit
camera subject, and (d) the missile-launch target. It is written from five
places (LMB click in `CameraRig`, RMB command-as-you-select, radial-menu
open, touch double-tap, death/despawn clears). It holds exactly **one**
entity, can never hold an *own* unit (`pick_entity_at_screen` excludes the
own hull), and there is no notion of a selected **set**.

### 1.4 Screen↔world math, as built (inventory for §4)

- **Unproject (screen → ray):** `cursor_ray(mx, my, ro, rd)`
  (`main.cpp:257`) — inverse of `View()*Projection()` through DirectXMath,
  producing a ray in the **render frame** (origin-relative doubles). Correct
  and reusable as-is.
- **Ray → plane:** `Neuron::Input::RayPlanePoint` (`MoveGizmo.h`) — generic
  plane (origin + normal), grazing fallback (`GIZMO_GRAZE_EPS`), max-range
  clamp. **Already normal-agnostic** — switching from the camera-up plane to
  the world-Y tactical plane is a one-argument change at the call site
  (`gizmo_begin`, `main.cpp:316` passes `camera_up_vec()`).
- **Elevation & clamps:** `ApplyElevation` (slide along the normal) +
  `ClampReach` (server ±1e6 Chebyshev) + `ComputeMoveTarget` (full pipeline).
  All reusable verbatim with the normal `{0, 1, 0}`.
- **Project (world → screen):** `camera_view_point` +
  `CameraSpaceToPixels` — used by `pick_entity_at_screen` (nearest entity
  within a viewport-scaled hit radius) and by the gizmo's
  world-units-per-pixel scale (`g_gizmo_scale = depth / focal`,
  `main.cpp:326`) that makes the elevation drag track the pointer 1:1.
  The same projection loop is exactly what band-select needs (§4.3).

### 1.5 Tight couplings & bottlenecks

| # | Coupling | Evidence | Why it blocks the target model |
|---|---|---|---|
| C1 | **Selection ≡ orbit subject ≡ missile target** via one global | `g_missile_lock_target`, fed to `s_orbit.SetTarget` every frame | The target model needs a camera focus point that is *sticky* (pan moves it, F re-centers it) and a selection that can be a set. One u32 cannot be both. |
| C2 | **Camera input split across two frame-ordered pollers** with duplicated slop machines | `CameraRig.cpp` (LMB) + `main.cpp` `handle_pointer_commands` (RMB), each with private prev/slop state; recognizer unused for mouse | Rebinding rotate to RMB-drag while RMB-click stays the command verb requires click-vs-drag disambiguation in **one** place, or the two machines will fight over the same button. |
| C3 | **No MMB, no chord detection** | `input_win.cpp` handles L/R only; `input_mouse_state(x, y, lmb, rmb)` has no mmb out-param | MMB zoom-drag and the LMB+RMB pan chord need capture-correct platform state. |
| C4 | **Pan input exists, pan camera doesn't** | `input_take_pan` uncalled; `OrbitCameraController` has no translate | Blocks both mouse pan and the touch two-finger pan (roadmap #8). |
| C5 | **Frame-counted hold threshold** | `LONGPRESS_FRAMES = 11` "~0.35 s at the 30 Hz command tick" — but the flight update runs at display rate | RMB hold length varies with fps; the recognizer already solves this with real timestamps. |
| C6 | **Command plane is camera-relative** | `gizmo_begin` uses `camera_up_vec()` per the interaction.md §3.4 design | Homeworld's grid is a *stable horizontal* plane; a camera-up plane re-orients when the view does, so a destination reads differently after every orbit. |
| C7 | **Own-hull unselectable, ownership invisible** | `pick_entity_at_screen` skips `rc.LocalPlayer()`; `EntitySnapshot` carries no owner | Band-select must resolve "own units". Today only the primary is knowable (`HelloAck`/`PlayerInfo` entityId); escorts are not identifiable client-side. |

What is **worth keeping unchanged**: the double-precision controller/
floating-origin contract, `MoveGizmo.h` (normal-agnostic already),
`OrderMenu.h` (the entire order grammar survives), `GestureRecognizer`
(becomes the single disambiguator), `pick_entity_at_screen`'s optics,
`send_order`/toast/ack feedback, the radial menu, the ability bar, and the
whole wire layer.

---

## 2. Gap Analysis

### 2.1 Verb-by-verb mapping (current → target)

| Verb | Today | Target | Class of change |
|---|---|---|---|
| Camera rotate | LMB-drag (FPV look / orbit) | **RMB-drag** | **Rebind** + click-vs-drag disambiguation (C2) |
| Camera zoom | Wheel (FPV dolly / orbit distance) | Wheel + **MMB-drag**, toward focus | Keep orbit wheel; **add** MMB (C3) |
| Camera pan | — (absent; touch pan discarded) | **LMB+RMB chord / arrows / WASD**, screen plane | **New** controller axis (C4) + chord detection (C3) |
| Camera focus | Implicit: orbit slaved to selection each frame | **F key**: animated re-center onto selection | **Replace** slaving with a sticky, animated focus point (C1) |
| Camera default mode | FPV default, orbit on F12 | **Focus-orbit default**; FPV demoted to observer toggle | Flip the default (interaction.md §3.6 already wants this) |
| Select | LMB click | LMB click | Keep |
| Band select | — | **LMB-drag** | **New** (needs C7 for >1 unit; ships degenerate) |
| Command | RMB click | RMB click (no drag) | Keep (survives the rebind because rotate needs drag) |
| Radial menu | RMB hold | RMB hold, stationary | Keep |
| Move order | RMB press on empty + drag elevation | Same fast path **+ M-key grid mode**; plane → **world-Y**; elevation modifier → **Shift** | **Refactor** plane + **new** state machine (C6) |
| Focus (touch) | Double-tap selects | Double-tap = select + F | Keep, add the focus animation |
| Abilities / screens / charts / chat | Ability bar, nav strip, windows | unchanged | — |

### 2.2 What gets refactored (kept, changed in place)

- `OrbitCameraController` → gains a `Pan(right, up)` translation of
  `m_targetWorld`, an animated `FocusOn(target)` (ease-out lerp, absolute
  doubles), and key-axis pan replacing its key-axis yaw/pitch. Rename to
  `FocusCameraController` only if desired; the class survives.
- `CameraRig.cpp` → all pointer *classification* leaves; it consumes
  device-neutral gestures and camera intents only. Keeps: anchoring,
  re-anchor on teleport, floating origin, dust cue, docked forcing.
- `handle_pointer_commands` (`main.cpp`) → drops its private RMB state
  machine; consumes recognizer gestures; hosts the movement-grid state
  machine (§4.2).
- `gizmo_begin` / `gizmo_apply` → plane normal parameter becomes world-Y; the
  render globals (`g_gizmo_*`) and `draw_move_gizmo` survive with the grid
  drawn horizontal.
- `input_win.cpp` → routes **mouse** buttons through the same
  `GestureRecognizer` instance path as touch (per-button recognizers or a
  button field on `PointerSample` — §3 step H1), adds MMB + chord state; the
  new accessors land on the `Neuron::Client::PointerInput` successor
  interaction.md §5 already names.

### 2.3 What gets replaced outright

- The **frame-counted RMB hold** (`LONGPRESS_FRAMES`) and both hand-rolled
  slop copies (`CameraRig.cpp:43`, `main.cpp:558`) — replaced by recognizer
  timestamps. One clock, one slop constant, mouse and touch identical.
- The **per-frame orbit-target slaving** block (`CameraRig.cpp:179-192`) —
  replaced by the sticky focus point + F/double-tap re-centering.
- The **camera-up command plane** — replaced by the world-Y tactical plane.
- `g_missile_lock_target` as *selection* — replaced by a `Selection` module;
  the global survives (renamed in spirit) as the derived **target of
  interest** for the reticle/missile until those consumers migrate (§3 H2).

### 2.4 New state that must be introduced

Named per `.github/coding-standards.md` (see the Naming note above): types and
functions `PascalCase`; `m_`-prefixed members; `g_` / `s_` file-scope state
with a `camelCase` body; `enum class` with `PascalCase` enumerators.

| State | Owner | Meaning |
|---|---|---|
| `m_focusFrom[3]` / `m_focusTo[3]` / `m_focusT` | `OrbitCameraController` (extended) | The focus-point **ease** (absolute doubles). `m_targetWorld` (existing) becomes the live **camera focus point**, replacing per-frame `SetTarget` slaving. |
| `s_panChordActive` | `CameraRig.cpp` | LMB+RMB chord latched: both buttons' other meanings suppressed until release. |
| `g_mmb` (via `PointerInput::MouseState`) | `input_win.cpp` | Middle button held, capture-correct. |
| `s_selection` (`Selection` core: fixed array ≤ `MAX_SELECTED`, count) | new `NeuronClient/input/Selection.h` + glue in `main.cpp` | The selected **set** of own units; primary-only today. |
| `g_bandActive`, `g_bandX0` / `g_bandY0` / `g_bandX1` / `g_bandY1` | `main.cpp` (drawn by `space.cpp`) | Live rubber-band rectangle. |
| `g_gridMode` (`enum class GridMode : uint8_t { Off, PlacingXZ, AdjustingY }`) | `main.cpp` | The movement-grid state machine (the spec's `movement_plane_active`). |
| `s_gridShift` | `main.cpp` | **Shift held during grid** = the vertical modifier (the spec's `vertical_modifier_active`). |
| `g_gridPlaneY` | `main.cpp` | The tactical plane's world-Y (the selected unit's Y at activation), so the grid doesn't swim if the ship drifts while placing. |
| *(optional, wire)* owned-units knowledge | `PlayerInfo`-adjacent | Band-select beyond the primary needs the client to know which entities it owns. Options: (a) new S→C Gameplay event `OwnedUnits{vector<u32>}` on change, or (b) piggyback on the F-track escort work. **Not required to ship this migration** — pre-F1 the selection set is `{primary}` and the band mechanism is still exercised. |

### 2.5 Explicitly out of scope

The `InputCommand` re-cut / `AbilityRequest` unification (roadmap #6), chart
pointer polish (#8's non-camera parts), rebindable keys, gamepad. The server
`OrderSystem` is already exactly what the target model commands into.

---

## 3. Architectural Modification Plan

Sequenced so the game stays playable after every step. Efforts on the repo
scale (XS ≤ hours, S ≤ a day-ish, M = days).

### H1 — One pointer front door (S–M)

*Goal: kill C2/C3/C5 before any rebinding, or the rebind becomes whack-a-mole.*

1. `input_win.cpp`: handle `WM_MBUTTONDOWN/UP` (capture like L/R); the new
   pointer capability lands on the `Neuron::Client::PointerInput` successor
   (interaction.md §5), e.g.
   `void PointerInput::MouseState(int& _x, int& _y, bool& _lmb, bool& _rmb, bool& _mmb)`
   (keep the existing `input_mouse_state` as a forwarding shim until H6).
2. Extend `PointerSample` with a `button` field (`Primary`, `Left`, `Right`,
   `Middle` — an `enum class PointerButton : uint8_t`) **or** instantiate one
   recognizer per mouse button — recommended: per-button instances, zero
   change to the tested core. Feed synthetic samples from the mouse messages
   using the same `GetTickCount()` clock.
3. New consumable accessors mirroring the touch one-shots:
   `PointerInput::TakeClick(button, x, y)` (tap),
   `PointerInput::TakeHold(button, x, y)` (long-press),
   `PointerInput::DragState(button, dx, dy)` (active drag + frame delta).
   `CameraRig` and `main.cpp` switch from raw `lmb`/`rmb` edges to these; the
   two private slop machines and `LONGPRESS_FRAMES` are deleted.
4. Chord: in `input_win.cpp`, when LMB and RMB are down together, emit a
   Cancel into both button recognizers and latch `s_panChordActive` until
   both release — a chord is never a click, a drag, or a hold.

*Acceptance: behaviour identical to today (LMB-drag still orbits, RMB grammar
unchanged) — this step only re-plumbs.*

### H2 — Selection decoupled from the lock target (S)

1. New pure core `NeuronClient/input/Selection.h` (headless-tested):
   fixed-capacity set of entity ids + `Primary()`, `Set()` / `Add()` /
   `Clear()`, and a `ContainsOwn()` predicate taking an `IsOwnUnit` callback.
2. `main.cpp` glue: `s_selection`; LMB click routes here.
   `g_missile_lock_target` becomes **derived**: the selected *enemy* for
   reticle/missile, written by the selection glue (single writer). The five
   scattered writers collapse to selection ops + the death/despawn clears.
3. `pick_entity_at_screen` gains an `allowOwn` flag (the own hull must be
   selectable now — it is the thing you command and focus).

### H3 — The focus camera (M) — *the largest single step*

1. `OrbitCameraController` (`NeuronClient/CameraController.{h,cpp}`):
   - `void Pan(float _dxPx, float _dyPx)` — translate `m_targetWorld` along
     the camera **right** and **up** basis vectors (derived from yaw/pitch),
     scaled by `m_distance` so pan speed is screen-constant
     (`PAN_PER_PIXEL ≈ distance * 2·tan(fovY/2) / viewportH`).
   - `void FocusOn(const double _targetWorld[3])` — begin an ease-out lerp
     (`FOCUS_ANIM_SECONDS ≈ 0.5`) from the current focus; `Update` advances
     it. `SetTarget` (snap) survives for anchor/teleport.
   - `void CancelFocusAnim()` — settle the ease immediately (used by the
     reset seam, §6.2).
   - Key axes become pan (right/up in the screen plane), not yaw/pitch.
2. `CameraRig.cpp` rebind (consuming H1 gestures):
   - **RMB drag** → `in.looking` (rotate). **LMB drag** → no longer camera
     (freed for H5's band). **Wheel** → unchanged. **MMB drag** vertical →
     `in.wheelSteps += dy * MMB_ZOOM_PER_PIXEL`. **Chord / two-finger pan /
     arrows+WASD** → `in.panDX` / `in.panDY` (new `CameraInput` fields; finally
     consumes `input_take_pan`).
   - Delete the per-frame orbit-slaving block; instead: **F key edge** (and
     selection double-click/tap) → `s_orbit.FocusOn(selected-or-own-ship)`.
     Re-anchor/teleport keeps using snap.
   - Orbit becomes the **default** controller; FPV stays behind F12 as the
     observer mode (interaction.md §3.6 already mandates this flip).
   - Note: `F` collides with the charts' find-by-name accelerator only by
     letter — `kbd_find_pressed` is read on chart screens, the focus key is
     read on `SCR_FRONT_VIEW`. No conflict; document it in §3.9 of
     interaction.md.
3. The dust-cue code survives unchanged (it reads eye motion, which
   pan/focus naturally feed). The docked exception is **folded into** the
   unified rotate path rather than kept as a parallel copy (§6.3), and
   `camera_rig_reset` gains the new-state teardown (§6.2) so no focus
   animation or latched chord survives a death→intro→flight transition.

### H4 — RMB grammar under the rebind (S)

`handle_pointer_commands` re-expressed over H1 gestures — the grammar is
unchanged, only the disambiguation source moves:

- `PointerInput::TakeClick(PointerButton::Right, …)` → contextual order /
  open grid (as today's release-without-move branch).
- `PointerInput::TakeHold(PointerButton::Right, …)` over an entity → radial
  menu (drag-to-slice unchanged).
- `RMB drag` → **camera** (H3); an armed one-gesture gizmo on empty space
  still owns the drag if it began there (press-classification: entity →
  menu candidate, empty → gizmo, and now *drag from entity-or-nothing
  without gizmo* → rotate).
- Suppress all of it while `s_panChordActive`.

### H5 — Band select (S, degenerate today)

- LMB drag (freed by H3) draws the `g_band*` rectangle; on release, project
  every replicated entity (the `pick_entity_at_screen` loop body, factored
  into a shared helper) and `s_selection.Set()` those inside that are **own
  units** (today: `rc.LocalPlayer()` only). A drag that never crossed the slop
  is the H2 click.
- The ownership wire gap (§2.4) is a follow-up, not a blocker.

### H6 — The movement grid (M) + cleanup (S–M)

1. Plane change: `gizmo_begin` passes the normal `{0, 1, 0}` and anchors the
   plane at `g_gridPlaneY` (ship Y at activation). `draw_move_gizmo`'s grid
   renders in the XZ orientation (it already draws in the plane's frame).
2. The `g_gridMode` state machine (§4.2): M-key toggle + hover placement +
   Shift elevation + LMB confirm; the one-gesture RMB fast path routes
   through the same states (press = enter `GridMode::PlacingXZ`, drag =
   `GridMode::AdjustingY`, release = confirm) so there is exactly **one**
   implementation.
3. Introduce `FlightInputActive()` (§6.5) and route the grid, band, and
   contextual-order entries through it, replacing the ad-hoc
   `GuiOverlay::IsShown() || current_screen != SCR_FRONT_VIEW || docked`
   gate at `main.cpp:511` and its scattered siblings.
4. Retire the old `input_mouse_state` shim, the dead slop constants, update
   `docs/interaction.md` §3.2/§3.4/§3.6 tables + `ARCHITECTURE.md` §7 input
   bullet (this file becomes the record of the migration; interaction.md
   remains canonical for the resulting grammar).

### Testing (per the repo's headless-first rule)

- **Recognizer**: per-button mouse feeding, chord-cancel, hold-vs-drag — new
  cases in the existing suite.
- **MoveGizmo**: world-Y plane cases (ray from above/below, grazing when the
  camera is in the plane — the fallback already covers it; elevation ± along
  +Y; reach clamp) — parameter-only additions.
- **Selection core**: set/add/clear, own-unit filtering, band membership.
- **Grid state machine**: extract the transition table into a pure core
  (`NeuronClient/input/MovePlan.h`, mirroring OrderMenu's pattern) and pin the
  `Off → PlacingXZ → AdjustingY → confirm/cancel` paths, Shift press/release
  mid-gesture, and the drag fast path.
- **Manual pass**: mouse + touch device, per interaction.md Track I practice.

---

## 4. Code Implementation Details

Concrete shapes in the canonical style (`.github/coding-standards.md`).
(Illustrative — final signatures land with the steps above.)

### 4.1 The camera state update loop

`CameraInput` gains pan; the controller gains the focus point. In
`NeuronClient/CameraController.h`:

```cpp
struct CameraInput
{
  float lookDX = 0.0f, lookDY = 0.0f;  // rotate drag, pixels (RMB / one-finger)
  bool  looking = false;
  float panDX = 0.0f, panDY = 0.0f;    // pan drag, pixels (chord / two-finger / MMB-h)
  bool  panning = false;
  float wheelSteps = 0.0f;             // wheel + pinch + MMB vertical drag
  float keyPanRight = 0.0f;            // -1..1 (arrows / WASD)
  float keyPanUp = 0.0f;
  bool  boost = false;
  float dt = 1.0f / 60.0f;
};
```

The extended orbit controller (the Homeworld camera):

```cpp
void OrbitCameraController::FocusOn(const double _targetWorld[3])
{
  for (int i = 0; i < 3; ++i) { m_focusFrom[i] = m_targetWorld[i]; m_focusTo[i] = _targetWorld[i]; }
  m_focusT = 0.0f;   // eases toward 1 in Update()
}

void OrbitCameraController::Update(const CameraInput& _input)
{
  // 1) Rotate: pitch/yaw about the focus point (RMB-drag).
  if (_input.looking)
  {
    m_yaw += _input.lookDX * LOOK_SENSITIVITY;
    m_pitch = ClampPitch(m_pitch + _input.lookDY * LOOK_SENSITIVITY);
  }

  // 2) Focus animation: ease the focus point onto its target (F key / select).
  if (m_focusT < 1.0f)
  {
    m_focusT = std::min(1.0f, m_focusT + _input.dt / FOCUS_ANIM_SECONDS);
    const float ease = 1.0f - (1.0f - m_focusT) * (1.0f - m_focusT);   // ease-out
    for (int i = 0; i < 3; ++i)
      m_targetWorld[i] = m_focusFrom[i] + (m_focusTo[i] - m_focusFrom[i]) * ease;
  }

  // 3) Pan: slide the focus point in the screen plane; world-units-per-pixel is
  //    derived from the orbit distance so panning feels the same at any zoom.
  const double perPixel = m_distance * m_tanHalfFovY * 2.0 / m_viewportH;
  const double panRight = -_input.panDX * perPixel
                        + _input.keyPanRight * KEY_PAN_RATE * m_distance * _input.dt;
  const double panUp    =  _input.panDY * perPixel
                        + _input.keyPanUp * KEY_PAN_RATE * m_distance * _input.dt;
  if (panRight != 0.0 || panUp != 0.0)
  {
    float look[3];  YawPitchToLook(m_yaw, m_pitch, look);
    const float right[3] = {cosf(m_yaw), 0.0f, -sinf(m_yaw)};          // flat right
    const float up[3]    = {right[1] * look[2] - right[2] * look[1],   // look x right
                            right[2] * look[0] - right[0] * look[2],
                            right[0] * look[1] - right[1] * look[0]};
    for (int i = 0; i < 3; ++i)
      m_targetWorld[i] += right[i] * panRight + up[i] * panUp;
    m_focusT = 1.0f;   // panning cancels an in-flight focus animation
  }

  // 4) Zoom toward the focus point (unchanged: multiplicative, clamped).
  double dist = m_distance;
  if (_input.wheelSteps != 0.0f)
    dist *= std::pow(ORBIT_WHEEL_FACTOR, static_cast<double>(_input.wheelSteps));
  m_distance = std::max(ORBIT_MIN_DISTANCE, std::min(ORBIT_MAX_DISTANCE, dist));

  RecomputeEye();   // eye = focus - look * distance (absolute doubles, unchanged)
}
```

The rig side (`CameraRig.cpp`, gathering via the H1 accessors). `KeyAxis` and
`KeyEdge` are the rig's existing/added key helpers; `PointerInput` is the
input successor:

```cpp
Client::CameraInput in{};
in.dt = static_cast<float>(dt);
if (!uiOwns)
{
  in.wheelSteps = input_take_mouse_wheel();               // wheel + pinch, as today

  float dx = 0.f, dy = 0.f;
  if (PointerInput::ChordPan(dx, dy))                     // LMB+RMB held together
  { in.panDX = dx; in.panDY = dy; in.panning = true; }
  else if (PointerInput::DragState(PointerButton::Right, dx, dy)
           && !g_gizmo_active && !g_radial_open)
  { in.lookDX = dx; in.lookDY = dy; in.looking = true; }  // RMB-drag = rotate

  input_take_pan(dx, dy);                                 // two-finger pan (touch)
  in.panDX += dx; in.panDY += dy;

  in.keyPanRight = KeyAxis(VK_RIGHT, VK_LEFT) + KeyAxis('D', 'A');
  in.keyPanUp    = KeyAxis(VK_UP, VK_DOWN)    + KeyAxis('W', 'S');
  in.boost = input_key_down(VK_SHIFT) && g_gridMode == GridMode::Off;   // Shift is the
}                                                          // grid's vertical modifier

if (KeyEdge('F'))                                          // Focus: animate onto selection
  s_orbit.FocusOn(SelectionFocusWorld());                 // selection, else own ship
```

### 4.2 The movement-grid state machine (the two-step 3D move)

Pure core (`NeuronClient/input/MovePlan.h`, OrderMenu-style so CI pins the
transitions) plus glue in `main.cpp`. The glue below shows the whole loop;
the enum/transition function is what moves into the tested core.

```cpp
// Movement-grid state (interaction.md §3.4 successor; drawn by draw_move_gizmo).
enum class GridMode : uint8_t { Off, PlacingXZ, AdjustingY };
GridMode g_gridMode  = GridMode::Off;
double   g_gridPlaneY = 0.0;          // tactical plane height (ship Y at activation)
static double s_gridElev = 0.0;       // accumulated Y offset (world units)
static int    s_gridAnchorY = 0;      // pointer y when Shift was pressed (px)

static void GridEnter(void)
{
  Neuron::Input::GVec3 ship;
  if (!ship_relative(ship)) return;
  g_gridPlaneY = ship.y;              // the plane is HORIZONTAL: normal = world +Y
  s_gridElev = 0.0;
  g_gridMode = GridMode::PlacingXZ;
  g_gizmo_active = true;              // existing render path draws plane+marker+stem
}

static void GridCancel(void)
{
  g_gridMode = GridMode::Off;
  g_gizmo_active = false;
}

// Ray ∩ horizontal plane -> the XZ coordinate (MoveGizmo core, world-up normal).
static bool GridPlaceXZ(int _mx, int _my)
{
  using namespace Neuron::Input;
  GVec3 ro, rd, ship;
  if (!cursor_ray(_mx, _my, ro, rd) || !ship_relative(ship)) return false;
  const GVec3 planeOrigin{ship.x, g_gridPlaneY, ship.z};
  const PlaneHit hit = RayPlanePoint(ro, rd, planeOrigin, GVec3{0, 1, 0});
  g_gizmo_base[0] = hit.point.x; g_gizmo_base[1] = hit.point.y; g_gizmo_base[2] = hit.point.z;
  // 1:1 screen tracking for the later elevation drag (same derivation as today).
  struct vector bp{hit.point.x, hit.point.y, hit.point.z};
  camera_view_point(&bp);
  const double focal = Neuron::Client::CameraFocalPixels(Client::MainCamera(),
      static_cast<float>(Neuron::Graphics::Core::GetOutputSize().Height));
  g_gizmo_scale = (bp.z > 1.0 && focal > 1.0) ? bp.z / focal : 50.0;
  return true;
}

static void GridConfirm(void)
{
  long long pt[3];
  gizmo_apply(s_gridElev, pt);        // elevation along +Y, ClampReach, -> absolute
  send_order(Neuron::Msg::OrderKind::Move, 0xFFFFFFFFu, pt);
  GridCancel();
}

// Per-frame, from handle_pointer_commands (after the camera has read input):
void HandleMovementGrid(int _mx, int _my)
{
  const bool shift = input_key_down(VK_SHIFT);   // the VERTICAL MODIFIER

  switch (g_gridMode)
  {
  case GridMode::Off:
    if (KeyEdge('M') && SelectionHasOwnUnit())
      GridEnter();                               // M spawns the grid...
    break;                                       // ...RMB-on-empty enters via H4 too

  case GridMode::PlacingXZ:
    if (KeyEdge('M') || KeyEdge(VK_ESCAPE)
        || PointerInput::TakeClick(PointerButton::Right, _mx, _my))
      { GridCancel(); break; }
    if (shift) { s_gridAnchorY = _my; g_gridMode = GridMode::AdjustingY; break; }
    GridPlaceXZ(_mx, _my);                       // hover tracks the XZ point live
    if (PointerInput::TakeClick(PointerButton::Left, _mx, _my))
      GridConfirm();                             // click = zero-elevation move
    break;

  case GridMode::AdjustingY:
    if (KeyEdge(VK_ESCAPE)) { GridCancel(); break; }
    if (!shift) { g_gridMode = GridMode::PlacingXZ; break; }   // elevation kept
    // Vertical pointer motion slides the marker along +Y; dragging UP raises it.
    s_gridElev = static_cast<double>(s_gridAnchorY - _my) * g_gizmo_scale;
    { long long pt[3]; gizmo_apply(s_gridElev, pt); }         // marker preview only
    if (PointerInput::TakeClick(PointerButton::Left, _mx, _my))
      GridConfirm();
    break;
  }
}
```

The existing one-gesture RMB fast path maps onto the same states: RMB press
on empty space = `GridEnter()` + `GridPlaceXZ` (locked, not hover), drag =
`GridMode::AdjustingY` with the press point as anchor (no Shift needed — the
button being held *is* the modifier there), release = `GridConfirm()`.
`gizmo_apply` needs one change only: it already takes elevation and clamps —
with the base on the world-Y plane, `ApplyElevation(base, {0, 1, 0}, elev)`
*is* the Y-axis modification.

### 4.3 Band-select projection (shared with picking)

Factor the projection loop out of `pick_entity_at_screen` so band and click
share optics:

```cpp
// Project every replicated entity to pixels once; callers filter.
// (space.cpp; the loop body is pick_entity_at_screen's, unchanged.)
struct ScreenEntity { unsigned int id; double sx, sy; int type; };
std::size_t ProjectEntitiesToScreen(ScreenEntity* _out, std::size_t _cap);
```

Band release then selects `IsOwnUnit(id) && InsideBand(sx, sy)` — with
`IsOwnUnit` = `id == rc.LocalPlayer()` until ownership replicates (§2.4).

---

## 5. Touch translation

The gesture layer (recognizer + synthesis) already exists; the migration is
mostly *consuming* what it emits. Mapping strategy, per target verb:

| Target verb | Touch gesture | Status |
|---|---|---|
| Camera rotate (RMB-drag) | **One-finger drag** on empty space | Exists (today it feeds LMB-drag orbit); after H3 the synthesized single-finger drag routes to `in.looking` directly — the *feel* is unchanged, only the internal button label moves. |
| Camera zoom (wheel) | **Pinch** | Exists (`g_wheelSteps` sharing) — keep. |
| Camera pan (chord) | **Two-finger drag** | Recognizer emits PanMove; `input_take_pan` finally gets its consumer (H3). This closes roadmap #8. |
| Focus (F) | **Double-tap** | Exists; now also triggers `FocusOn` (H3). |
| Select | **Tap** | Exists (synthesized LMB click). |
| Band select | **Tap-then-drag is taken** (it must stay camera rotate). Provide band via a small **selection-mode chip** next to the selection chip (tap to arm; next drag bands), mirroring how the ✕-deselect chip already works. | New, low priority pre-F1 (single-unit selection makes band redundant on touch today). |
| Command (RMB click) | **Tap with an own unit selected** (the interaction.md §3.2 collision rule, unchanged). | Exists. |
| Radial menu (RMB hold) | **Long-press** | Exists — keep. |
| Movement grid (M) | **Long-press on empty space** opens the grid in `GridMode::PlacingXZ`; drag = XZ placement; a **second finger down** while placing = the vertical modifier (`GridMode::AdjustingY`, the primary finger's vertical motion drives Y — the recognizer's second-finger pre-emption is suppressed while the grid owns the contact); lift = confirm. | New glue over the same state machine; no Shift key exists on touch, the second finger is its analogue. |
| Cancel | Tap the selection-chip ✕ / a grid "cancel" chip | New (XS). |

Ergonomic guardrails already in place and kept: the long-press consumes the
contact (`g_touchSuppress`), the radial menu owns the frame while open
(`g_radial_open` gates the camera), and `>2` fingers suppress until a clean
slate.

---

## 6. Game-state handling

The plan above is written for the live flight loop; the client is a small
state machine around it and each state constrains input differently. This
section makes those constraints explicit — the first draft treated only
`docked`, which is not enough.

### 6.1 The states, and which input runs

`GameState` (`main.cpp:1125`) is `{Intro1, Intro2, Flight, GameOver}`.
`docked` is **not** a state — it is a sub-mode of `Flight` (`current_screen`
stays `SCR_FRONT_VIEW`, the `StationMenuWindow` floats over the station
view). Only `Flight` runs the input path this migration touches:

| State | Input that runs | Camera | This migration's exposure |
|---|---|---|---|
| `Intro1` / `Intro2` | `kbd_poll_keyboard` only; **Space** advances. Scenes animate in **camera space** against the identity camera (`camera_rig_reset`). `camera_rig_update` / `handle_pointer_commands` **not called**. | Identity (reset) | Reset-on-entry only (§6.2); intro *advance* gesture (§6.4) |
| `Flight` (in space) | `camera_rig_update` → `handle_pointer_commands` → `handle_flight_keys` → `send_player_input` (`main.cpp:1731-1740`) | Focus-orbit (new default) / FPV observer | The full plan (H1–H6) |
| `Flight` (docked) | Same call chain, but every command handler early-returns on `docked`; `StationMenuWindow` (a `GuiOverlay`) owns LMB; the rig force-orbits the station | Forced orbit around own ship | §6.3 — converges with the new model |
| `GameOver` | Frame counter only (100 frames) → `respawn_after_death` (docked). Scene animates in camera space against the identity camera. | Identity (reset) | Reset-on-entry only (§6.2) |

Because the new selection / band / grid glue lives inside
`handle_pointer_commands` and `camera_rig_update`, which run **only in
`Flight`**, the intro and game-over states are automatically inert for the
*active* verbs. The one thing they are **not** automatically safe against is
**stale carried state** — that is §6.2.

### 6.2 Reset-on-transition (the real new requirement)

`camera_rig_reset()` (`CameraRig.cpp:102`) is the seam every non-flight
state enters through (intro1/intro2/game-over). Today it clears `s_ready`,
`s_orbitMode`, the origin, the timer, and the identity view. The new
controller/selection state **must reset here too**, or a focus animation
mid-flight when the player dies will keep easing through the game-over
scene and into the next spawn:

```cpp
void camera_rig_reset(void)
{
  // ... existing resets (s_ready, s_orbitMode, s_origin, s_haveTime, view) ...
  s_orbit.CancelFocusAnim();      // no in-flight ease survives a scene change
  s_panChordActive = false;       // drop any latched LMB+RMB chord
  SelectionClear();               // s_selection + derived g_missile_lock_target
  GridCancel();                   // g_gridMode = GridMode::Off, g_gizmo_active = false
  g_bandActive = false;           // no rubber-band across a transition
}
```

`g_missile_lock_target` is already cleared on the docked entry
(`enter_station`, `main.cpp:1168`) and on death/despawn events
(`main.cpp:1359,1408,1432`); routing those through `SelectionClear()`
keeps the selection set and the derived global consistent (the H2 single-
writer rule). The re-anchor on first ship sight after a reset
(`AnchorBehindShip`) already re-seats the orbit — the focus point should be
initialised to the ship there (`s_orbit.SetTarget(ship)` becomes the focus
seed, snap not animated).

### 6.3 Docked — the special case that converges

Today docked is handled by two exceptions: the rig **forces orbit mode**
for the whole docked stay and binds **RMB-drag to orbit the station**
(`CameraRig.cpp:199-216, 266-274`), because the `StationMenuWindow` owns the
pointer (`uiOwns` is true) yet RMB is otherwise unused at a station. Under
the target model this stops being a special case:

- Orbit is now the **default** camera everywhere, so "force orbit while
  docked" is a no-op relative to the default — keep it only as a guard that
  F12/observer-FPV can't be toggled on at the station.
- RMB-drag is now the **global rotate** binding, so the docked RMB-orbit
  exception is simply the normal rotate verb still being live while the menu
  owns LMB. The dedicated docked RMB block can be deleted in favour of the
  standard path, gated so it reads input even though `uiOwns` is true (the
  one thing the docked branch does that the flight branch does not).
- The **pan chord cannot form** docked (the station menu owns LMB, so
  LMB+RMB never latches) — correct by construction; no extra guard needed.
- **F (focus)** docked has no selection (`g_missile_lock_target` cleared on
  entry), so it re-centres the own ship — the desired behaviour.
- **Movement grid, band-select, contextual orders** stay disabled docked:
  `handle_pointer_commands` already early-returns on `docked`
  (`main.cpp:511`), and the H6 grid entry (M-key) must sit **behind that
  same gate** so M is inert at the station. Ordering a docked ship to move
  is meaningless — launch is the station-menu flow.

Net: the docked path gets *simpler*, not more complex. The migration should
fold the docked RMB exception into the unified rotate path rather than carry
a parallel copy.

### 6.4 Intro / game-over: the advance gesture

interaction.md §3.8 wants "press Space" to become "tap/click anywhere"
(Space kept as accelerator). That is the only pointer verb the intro states
need, and it must **not** be routed through the H1 selection/band
recognizer path (there is nothing to select in an intro). Keep it as a
plain "any tap or Space this frame" check in the `Intro1`/`Intro2` arms of
`game_update` (`main.cpp:1820-1836`), reading a lightweight
`PointerInput::TakeAnyTap()` one-shot from the recognizer (a `Tap` on any
button) rather than the selection glue. The gesture recognizer is fed
regardless of game state (it lives in the window proc), so the one-shot is
available; the selection/command consumers simply don't run outside `Flight`.

### 6.5 Gate summary (what each new verb checks)

Every new verb inherits the existing gates. Rather than re-derive the same
multi-term condition at each call site (five scattered `if (docked)` copies
is how the current code drifts), the migration introduces **one shared
predicate** that names "the flight command grammar should accept pointer
input":

```cpp
// true only in live flight with the world in front and no menu owning input.
// The single gate every new command verb consults (replaces the ad-hoc
// GuiOverlay::IsShown() || current_screen != SCR_FRONT_VIEW || docked copies).
bool FlightInputActive(void)
{
  return s_state == GameState::Flight
      && !docked
      && current_screen == SCR_FRONT_VIEW
      && !GuiOverlay::IsShown();
}
```

Each verb then reads as one call:

| New verb | Runs only when |
|---|---|
| Band-select (LMB drag) | `FlightInputActive()` |
| Movement grid (M / RMB-empty) | `FlightInputActive()` (this replaces the `handle_pointer_commands` gate at `main.cpp:511`) |
| Contextual order / radial menu (RMB) | `FlightInputActive()` |
| Focus (F) | `Flight` && `current_screen == SCR_FRONT_VIEW` (**docked allowed** — re-centres own ship; not `FlightInputActive`) |
| Camera rotate / pan / zoom | `Flight` && !`uiOwns` (the `CameraRig` gate), plus the docked RMB exception (§6.3) |
| Intro advance (tap) | `Intro1` / `Intro2` only |

Focus and camera deliberately do **not** use `FlightInputActive()`: both
stay live while docked (§6.3), so they keep their own, looser conditions.
The predicate is exactly the "live flight, no station, no window" set the
command verbs share.

### 6.6 Why `docked` stays a sub-mode (not a `GameState`)

A natural question: should `docked` be promoted to a fourth-and-a-half
`GameState` alongside `Intro1/Intro2/Flight/GameOver`? **No** — and the
reason is that the two model different axes:

- **`GameState` is client-owned "which top-level scene/loop":** each member
  has its own update *and* render function, and its transitions are
  client-driven (Space advances the intro; the 100-frame counter ends
  game-over). Intro1/Intro2/GameOver each render a wholly different
  camera-space scene.
- **`docked` is server-owned "is my ship parked":** the client flag only
  flips in reaction to server facts — `StationResponse{Dock}`
  (`main.cpp:1352`), `TravelResponse{Hyperspace}` (`main.cpp:1326`), plus
  the local dock/launch request flows (`space.cpp:284,1544`). It mirrors the
  server's `DockState`.

Promoting it would (a) pull a server-authoritative fact into the client's
top-level scene machine, creating a second source of truth that must be kept
synchronized on every undock path (death-respawn, hyperspace, escape pod);
(b) buy nothing structurally, since docked and flying **share** the
`game_*_flight` update/render (the station is simply in view, HUD dashboard
swapped for `StationMenuWindow`) — a `Docked` state would just call back into
the same functions and still branch on the flag internally; and (c) duplicate
what is already modelled by the `StationMenuWindow` overlay
(`OpenStationMenu`/`CloseStationMenu`). The render *forks* for intros/
game-over but only *toggles a HUD* for docked — which is exactly why the
first three are states and docked is a boolean. `FlightInputActive()`
(§6.5) gives the centralization a `Docked` state would promise, without the
authority-duplication cost.

---

## 7. Migration sequencing & risk

| Step | Ships alone? | Risk | Mitigation |
|---|---|---|---|
| H1 front door | Yes (no behaviour change) | Recognizer double-fires vs raw state | The raw `input_mouse_state` shim stays until H6; assert one consumer per gesture in debug |
| H2 selection | Yes | Reticle/missile regressions | `g_missile_lock_target` kept as the single derived output; its readers untouched |
| H3 camera | Yes — **the feel flip** | Muscle-memory break (LMB-drag stops orbiting) | Land H3+H4 in the same change so RMB gains rotate the moment LMB loses it |
| H4 RMB grammar | With H3 | Click-vs-drag misreads eating orders | Recognizer slop is the single arbiter; the order toast makes a misread visible immediately |
| H5 band | Yes (degenerate) | — | Pure addition |
| H6 grid | Yes | Plane-orientation surprise (camera-up → world-Y changes where a click lands) | The grid is *rendered*; the marker preview is the truth the player confirms against |

Open decisions to settle during implementation (none block the start):

1. **WASD**: `S`/`X` are still GUI menu-up/down aliases (`input_win.cpp`
   `input_update_menu_edges`); adopting WASD pan should drop those aliases or
   scope them to open menus. Arrows-only is the safe first cut.
2. **FPV observer mode**: keep behind F12 (recommended, interaction.md §3.6)
   or delete outright once the focus camera pans — decide after the feel
   pass.
3. **Ownership replication** (§2.4) for multi-unit band select: fold into the
   F-track escort work rather than this migration.
4. **Zoom-to-cursor** (zoom toward the cursor ray instead of the focus
   point): a nice-to-have on top of H3; Homeworld zooms to focus, so default
   to focus.
