# Math Migration Plan — DirectXMath + `Neuron::Math` (GameMath)

> **Goal.** Replace the legacy hand-rolled vector/matrix math in the ported game
> code with native **DirectXMath** (`XMVECTOR` / `XMMATRIX`) and the sanctioned
> `Neuron::Math` helpers in [`NeuronCore/GameMath.h`](../NeuronCore/GameMath.h).
> No new wrapper functions or wrapper classes — call native DirectXMath (and the
> existing `Neuron::Math` helpers) directly, per the
> [Native-First rule](../.github/coding-standards.md#native-first--no-wrapper-functions-or-classes).

This is a planning document, not a code change. It inventories what exists,
fixes the migration decisions, maps every legacy construct to its native
replacement, and sequences the work in independently shippable phases.

---

## 1. Decisions (locked)

These forks were settled up front; the rest of the plan assumes them.

| Decision | Choice | Consequence |
|---|---|---|
| **Precision** | **Full float32 / DirectXMath.** | Sim *and* render math move to `XMVECTOR`/`XMMATRIX`. The ported flight model goes from `double` to `float`; numeric results of the old double path will shift. See [§7 Precision & determinism](#7-precision--determinism-risk). |
| **`Vector3d` / `Vector3i64`** | **Both stay — out of scope.** | Neither has a DirectXMath equivalent (DirectXMath is float32-only), so they are **not** wrappers and **not** migration targets. `Vector3d` is a legitimate fp64 `Neuron::Math` type carrying the authoritative server sim's precision-sensitive state — the `ShipFrame` orientation basis and the **`carry` sub-unit remainder** that steps the `int64` world (`SimComponents.h` / `FlightSystem.h`), plus `LocalOffset` (`SnapshotInterpolator.h`). `Vector3i64` holds absolute `int64³` world coordinates that must stay exact (see [`MathTests`](../Tests/NeuronCore/MathTests.cpp)). The migration touches them only at the **render boundary**: when the legacy float render path consumes a world/local offset, convert double→`XMVECTOR` there (`XMVectorSet((float)dx, …)`). |
| **`GameMath.h` helper layer** | **Use `Neuron::Math` helpers freely.** | `Neuron::Math` is the sanctioned helper layer; calling `Normalize`, `Cross`, `Dot`, `RotateAround`, `Vector3::FORWARD`, etc. is *not* a native-first violation. The no-wrapper rule applies to **new** code: do **not** add fresh thin forwarders, and do **not** keep the legacy `vector.h` wrappers alive. |
| **Determinism** | **Reproducible-enough (no cross-machine bit-exactness).** | Same-build reproducibility is the bar; no lockstep/replay bit-exact guarantee. Default `/fp` is acceptable — no path is forced to stay integer/fixed-point for determinism's sake. |
| **`Transform.rotmat` storage** | **`XMFLOAT4X4` (engine-uniform).** | Matches the rest of the engine's matrices for uniform `XMLoadFloat4x4`/`XMStoreFloat4x4` and easy translation folding; the 3×3 rotation basis occupies the upper-left, row 3 / column 3 identity. |
| **Legacy type naming** | **Fix the docs to match the code.** | `AGENTS.md` / `coding-standards.md` get updated to say `struct vector` / `Matrix`; the code is **not** renamed (the types are deleted in Phase 7 anyway). |
| **Serialization** | **`Transform.location` / `rotmat` are not persisted as 3×double.** | The storage switch is internal-only — **no** wire/save format version bump is required. |
| **Flight integrator (`rotate_vec`)** | **(A) Preserve the legacy shear math in `XMVECTOR` first; (B) true-rotation upgrade is a separate follow-up.** | `rotate_vec` is a sequential **shear integrator**, not a rotation (see [§7](#7-precision--determinism-risk)). Reproducing it faithfully keeps flight feel and lets the golden tests pass tightly; swapping in `XMMatrixRotation*` is a deliberate gameplay-tuning change, done later and on its own. |
| **Build flags (`/arch`, `/fp`)** | **Pin both in the CMake presets and baseline against them.** | DirectXMath's code path and FMA contraction depend on `/arch` (SSE2 vs AVX/AVX2) and `/fp`. Pin them so reproducibility holds within a build and any later `/arch:AVX2` flip is a conscious, re-baselined change — not a silent golden-test break. |

---

## 2. Current state

> **Sync note.** This inventory reflects `main` as of the latest merge into the
> migration branch. Recent gameplay work (homing missiles, target lock/reticle,
> own-axis flight fix, Wavefront-OBJ ship export) **expanded** the legacy-math
> surface in the client render path (see §2.2) but did not change the migration
> strategy. Server-side additions (`MissileSystem.h`) are `Vector3d`/`int64` and
> stay out of scope; the `tools/shipdata2obj` exporter is unrelated to this work.

### 2.1 Legacy math (the migration target)

[`DeepspaceOutpost/vector.h`](../DeepspaceOutpost/vector.h) +
[`vector.cpp`](../DeepspaceOutpost/vector.cpp) — C-style, double precision:

```c
struct vector { double x, y, z; };       // 3D vector
typedef struct vector Matrix[3];          // 3×3 rotation basis (rows are vectors)
typedef struct vector Vector;

void   mult_matrix(vector *first, vector *second);   // 3×3 · 3×3 → first
void   mult_vector(vector *vec, vector *mat);        // row-vector · 3×3 → vec
double vector_dot_product(vector *a, vector *b);
vector unit_vector(vector *v);                       // normalize
void   set_init_matrix(vector *mat);                 // load identity-ish basis
void   tidy_matrix(vector *mat);                     // re-orthonormalize basis
```

Plus inline rotation helpers in the consumers:
`rotate_vec` ([`space.cpp:73`](../DeepspaceOutpost/space.cpp)) — small-angle
roll/climb integration; `rotate_x_first` / `rotate_z_first` — incremental
axis spins on matrix columns.

> **Naming note (resolved in Phase 1).** The repo docs previously referred to the
> legacy types as `LegacyVector2/3` and `Matrix33/34` — names that never existed
> in the code. The real types are `struct vector` and `Matrix` (`vector.h`). The
> docs (`AGENTS.md`, `coding-standards.md`, `copilot-instructions.md`,
> `SoftwareEngineer.agent.md`) have been corrected to the real names.

### 2.2 Consumers of legacy math

| File | Legacy math used for |
|---|---|
| [`space.cpp`](../DeepspaceOutpost/space.cpp) (~1430 ln) | `move_local_object`, `rotate_vec`, object position/velocity integration, distance, `unit_vector` docking/approach; **(main)** the target-reticle divide-by-z projection and `find_lock_target` forward-cone test |
| [`threed.cpp`](../DeepspaceOutpost/threed.cpp) (~1000 ln) | `draw_solid_ship`, `render_planet`, `draw_wireframe_planet` — model→camera transform, projection, back-face/normal checks |
| [`swat.cpp`](../DeepspaceOutpost/swat.cpp) (~1230 ln) | `add_new_ship` rotation-matrix setup, NPC spawn orientation, tactics geometry |
| [`ReplicatedScene.h`](../DeepspaceOutpost/ReplicatedScene.h) | **`RenderRecord` stores `Vector location` + `Matrix rotmat`**; `BuildRenderRecords` rebases each entity to the floating origin and runs the **world→camera transform** (the transpose convention — see §4). Pure legacy `Vector`/`Matrix`, includes `vector.h` |
| [`Camera.h`](../DeepspaceOutpost/Camera.h) | `Vector position` — camera eye offset from the ship |
| [`pilot.cpp`](../DeepspaceOutpost/pilot.cpp), [`missions.cpp`](../DeepspaceOutpost/missions.cpp), [`intro.cpp`](../DeepspaceOutpost/intro.cpp), [`main.cpp`](../DeepspaceOutpost/main.cpp) | Scattered matrix/vector setup, trig, scripted motion |
| [`GameComponents.h`](../DeepspaceOutpost/GameComponents.h) | ECS `Transform` stores `Vector location` + `Matrix rotmat` (storage layout) |
| [`space.h`](../DeepspaceOutpost/space.h) | `struct local_object { Vector location; Matrix rotmat; … }` |

### 2.3 Already-native math (the destination, already in place)

- [`NeuronCore/GameMath.h`](../NeuronCore/GameMath.h) — `Neuron::Math` over
  DirectXMath: `Set`, `Normalize`, `Cross`, `Dot`/`Dotf`, `Length`,
  `LengthSquare`, `SetLength`, `RotateAround[X/Y/Z]`, `CreateRotationMatrix`,
  `Invert`, and the `Vector3::{ZERO,UNITX,UP,FORWARD,…}` constants.
- [`Vector3d.h`](../NeuronCore/Vector3d.h) — fp64 local-frame vector, a legitimate
  `Neuron::Math` type (**stays — out of scope**; fills a gap DirectXMath's
  float32-only types can't). Server-sim consumers: `FlightSystem.h`,
  `SimComponents.h`, `CombatSystem.h`, `StationServices.h`, `SnapshotInterpolator.h`,
  and **(main)** the new homing-missile system [`MissileSystem.h`](../GameLogic/MissileSystem.h)
  (steering/homing in `Vector3d`, `int64` position stepping) — all reinforce that
  `Vector3d` is the fp64 sim type, not a migration target.
- [`Vector3i64.h`](../NeuronCore/Vector3i64.h) — int64 world coordinate (**stays**).
- `GameLogic/`, `NeuronServer/`, `Server/`, and the `NeuronCore`/`GameLogic` test
  targets use the SIMD / `Neuron::Math` types and are **not** migration targets.
  > **Correction (this sync).** The client render files are **mixed**, not
  > already-native: `CameraFollow.h` uses `Vector3d` (out of scope), but
  > `Camera.h` and `ReplicatedScene.h`/`RenderRecord` still use the legacy
  > `Vector`/`Matrix` and **are** in scope — they are listed in §2.2. `RenderQueue.h`
  > is screen-space ints (no vector math).

---

## 3. Target conventions (from `AGENTS.md`)

The migration must land on the existing SIMD-boundary rules — repeated here so
the plan is self-contained:

- **Storage** (struct/class members, serialized data): `XMFLOAT3` / `XMFLOAT4X4`
  (or `XMFLOAT3X3` for a pure rotation basis). Never do arithmetic on these.
- **Compute** (locals, loop bodies, intermediates): **always** `XMVECTOR` /
  `XMMATRIX`. Load → compute → store, with an explicit boundary.
- **Parameters**: `FXMVECTOR` + `XM_CALLCONV` (non-virtual); `const XMFLOAT3&`
  only when virtual dispatch forbids `XM_CALLCONV`. Follow the
  `FXMVECTOR`/`GXMVECTOR`/`HXMVECTOR`/`CXMVECTOR` ordering rules.
- **Returns**: `XMVECTOR` if the caller keeps computing; `XMFLOAT3` if storing.
- **Anti-pattern**: no `XMFLOAT3` arithmetic operators — they hide load→op→store
  and defeat the SIMD boundary.
- **ABI — x86 *and* x64 are targets.** `XM_CALLCONV` lowers to `__vectorcall`;
  the `FXMVECTOR`/`GXMVECTOR`/`HXMVECTOR`/`CXMVECTOR` parameter-ordering rules are
  stricter on 32-bit (the by-value vector-register slots differ). Verify both ABIs
  when adding `XM_CALLCONV` signatures.
- **`XMFLOAT4X4` default is all-zeros, not identity.** A `XMFLOAT4X4{}` has
  `m33 = 0`; feeding it to `XMVector3TransformCoord` divides by `w = 0` → NaN/inf.
  For the rotation-only `rotmat`, build from `XMMatrixIdentity()` then fill the
  upper-left 3×3, and transform directions with **`XMVector3TransformNormal`**
  (ignores translation and w), never `…TransformCoord`.
- **Don't drip scalars out with `XMVectorGetX/Y/Z`.** Per-component getters force
  SIMD→scalar store-forwarding stalls. In a scalar tail (e.g. the perspective
  divide writing integer screen coords), `XMStoreFloat3` once and read the struct,
  rather than three `XMVectorGet*` calls.
- **Default to precise, not `*Est`.** `XMVector3NormalizeEst` / `LengthEst` widen
  golden-test tolerances; use the precise forms unless a hot path measurably needs
  the estimate.

---

## 4. Type mapping

| Legacy | Compute type | Storage type | Notes |
|---|---|---|---|
| `struct vector` (3 doubles) | `XMVECTOR` | `XMFLOAT3` | w-lane unused / 0 for points & directions |
| `Vector` (alias) | `XMVECTOR` | `XMFLOAT3` | same as above |
| `Matrix` (`vector[3]`, 3×3 basis) | `XMMATRIX` | `XMFLOAT4X4` | rotation/orientation basis in the upper-left 3×3; `XMFLOAT4X4` chosen for engine uniformity (decision §1) |
| `Vector3d` (fp64 local frame) | — | `Vector3d` | **out of scope — not migrated.** No fp64 DirectXMath type exists; it carries the authoritative sim's precision-sensitive state. Convert to `XMVECTOR` only when the render path consumes it (`XMVectorSet((float)x, …)`) |
| `Vector3i64` (world) | — | `Vector3i64` | **unchanged**; convert to `XMVECTOR` at the floating-origin rebase via `RelativeTo`/`LocalOffset` (→ `Vector3d`) then `XMVectorSet` |

### Matrix convention — there is a transpose between legacy and DirectXMath

> ⚠️ **This is the single most error-prone part of the migration. Get it wrong
> and every rotation applies inverted (ships orient the wrong way), without
> crashing.**

Legacy `mult_vector(vec, mat)` ([`vector.cpp:61`](../DeepspaceOutpost/vector.cpp))
computes `result.i = dot(vec, mat[i])`, i.e. **`result = M · v`** (column-vector
convention). DirectXMath `XMVector3Transform(v, M)` computes **`v · M = Mᵀ · v`**.
With a natural row-preserving `XMLoadFloat4x4`, the two differ by a transpose:

```cpp
// legacy  mult_vector(vec, mat)   ≡
XMVector3TransformNormal(v, XMMatrixTranspose(M));   // NOT XMVector3TransformNormal(v, M)
```

For an orthonormal rotation basis `Mᵀ = M⁻¹`, so omitting the transpose silently
applies the **inverse** rotation — no crash, just wrong orientation. Pin the exact
mapping with a golden test on a **rotation about a skew axis** (a symmetric matrix
would hide the transpose). You may instead bake the transpose into how the matrix
is loaded (store legacy rows into matrix columns) — but choose one approach and
assert it.

Separately, the renderer's own manual element-swaps
([`threed.cpp:205`](../DeepspaceOutpost/threed.cpp)) are an *additional*,
intentional transpose of `rotmat` (model↔camera orientation). Track it
independently of the convention transpose above and express it with
`XMMatrixTranspose`, not element swaps. Preserve the legacy left-handed `-Z` basis
(`start_matrix`) so handedness — and the back-face winding test — don't flip.

> **Concrete in-repo example (added on main).** `BuildRenderRecords`'s `toCamera`
> lambda in [`ReplicatedScene.h`](../DeepspaceOutpost/ReplicatedScene.h) was just
> corrected to rebuild the offset **from** the basis
> (`x*side.x + y*roof.x + z*nose.x`, i.e. `B·v`) rather than project **onto** it
> (`x*side.x + y*side.y + z*side.z`). Its own comment documents this as "the
> transpose of a textbook view matrix, but it is what the starfield uses … at the
> identity orientation the two are identical." That is exactly the convention the
> DirectXMath port must reproduce — the same trap, already live in the code. The
> server's `RotateBasis` in [`FlightSystem.h`](../GameLogic/FlightSystem.h) makes
> the same `B·M` choice for the same reason.

---

## 5. Function mapping (legacy → native)

Every legacy routine has a native equivalent — **none of them should survive as
a wrapper.** Delete `vector.cpp`/`vector.h` at the end of the migration.

| Legacy | Native replacement | Header |
|---|---|---|
| `vector_dot_product(a,b)` | `XMVector3Dot` / `Neuron::Math::Dotf` (scalar) | DirectXMath / GameMath |
| `unit_vector(v)` | `XMVector3Normalize` / `Neuron::Math::Normalize` | DirectXMath / GameMath |
| length (`sqrt(x²+y²+z²)`) | `XMVector3Length` / `Neuron::Math::Length` | DirectXMath / GameMath |
| `mult_vector(v, mat)` | `XMVector3TransformNormal(v, XMMatrixTranspose(M))` — **mind the transpose** (see §4); directions take `…Normal` (no translation) | DirectXMath |
| `mult_matrix(a, b)` | `XMMatrixMultiply` / `operator*` (verify factor order against `mult_matrix`, which writes into `first`) | DirectXMath |
| `set_init_matrix(mat)` | `XMMatrixIdentity` + explicit `-Z` basis, or build from `Vector3::FORWARD/UP` constants | DirectXMath / GameMath |
| `tidy_matrix(mat)` (re-orthonormalize) | New `Neuron::Math::Orthonormalize` mirroring the **legacy axis order**: fix Z (forward), re-perpendicularize Y, then X = `Cross(Y,Z)` — *not* a different Gram–Schmidt order, or golden tests won't match | GameMath `Normalize`/`Cross` |
| `rotate_vec(v, α, β)` | **(A) faithful:** reproduce the sequential shear in `XMVECTOR`. **(B) follow-up:** `XMMatrixRotation*` / `RotateAround[X/Y/Z]` — behavior change (see §1, §7) | DirectXMath / GameMath |
| `rotate_x_first` / `rotate_z_first` | `XMMatrixRotationX/Z` then `XMMatrixMultiply` | DirectXMath |
| inverse | `XMMatrixInverse` / `Neuron::Math::Invert` | DirectXMath / GameMath |

If a needed operation has no native form, it goes into `Neuron::Math`
(`GameMath.h`) as a **real** helper (genuine behavior over `XMVECTOR`), never as
a per-file local wrapper and never as a method on a storage type.

---

## 6. Phased plan

Each phase is independently buildable and (where a target exists) testable. The
ordering goes **leaf utilities → storage → hot loops → cleanup** so behavior is
locked before storage layout churns the ECS.

### Phase 0 — Scaffolding & golden tests *(no behavior change)*
- **Done:** characterization tests for the pure leaf functions live in
  [`Tests/NeuronCore/LegacyMathGoldenTests.cpp`](../Tests/NeuronCore/LegacyMathGoldenTests.cpp)
  (registered in [`Tests/NeuronCore/CMakeLists.txt`](../Tests/NeuronCore/CMakeLists.txt)
  under the `NeuronCore.Tests` GoogleTest target). They capture
  the **current** double-precision outputs of `unit_vector`,
  `vector_dot_product`, `mult_vector`, `mult_matrix`, `tidy_matrix`, and
  `rotate_vec`, and become the float-tolerance oracle for every later phase.
- **Framework: GoogleTest** (the project standard since the test migration) —
  `TEST(LegacyMath, …)` cases with `EXPECT_NEAR`. The tests sit under
  `NeuronCore.Tests` because the migration's replacement helper
  (`Neuron::Math::Orthonormalize`) lives in NeuronCore and there is no
  DeepspaceOutpost test project (the client is a Win32 exe).
- The legacy `vector.cpp` `#include`s the heavy DeepspaceOutpost pch and can't
  compile into the dependency-light test exe, so the tests embed a **verbatim
  oracle** of those pure functions; keep it bit-identical to `vector.cpp` until
  Phase 7 deletes the original.
- **`mult_vector` uses a non-symmetric (cyclic-permutation) matrix** so the test
  pins the `M·v` column convention and exposes the §4 transpose (a symmetric
  matrix would hide it); `tidy_matrix` characterizes the left-handed `-Z`
  start-basis (handedness). Drift-sensitive cases carry `TODO(windows)` markers
  plus orthogonality invariants — fill the literals from the first MSVC run.
- **`move_local_object` is a *manual integration check*, not a unit golden** — it
  depends on `PlayerFlight()`, `ship_list`, and other `space.cpp` globals that
  can't be isolated header-only. Validate it by the Phase-4 flight smoke test.
- Comparison uses `EXPECT_NEAR` with a tight `kEps` (double oracle vs double
  oracle); the later float32-port comparison loosens to a magnitude-scaled
  epsilon (see [§7](#7-precision--determinism-risk)).

### Phase 1 — Reconcile the helper layer & docs *(no behavior change)* — **done**
- **Docs fixed** to name the real legacy types (`struct vector` / `Matrix`,
  `DeepspaceOutpost/vector.{h,cpp}`); the code is **not** renamed (decision §1).
  Corrected in `AGENTS.md`, [`coding-standards.md`](../.github/coding-standards.md),
  [`copilot-instructions.md`](../.github/copilot-instructions.md), and
  [`SoftwareEngineer.agent.md`](../.github/agents/SoftwareEngineer.agent.md) — the
  nonexistent `LegacyVector2/3` / `Matrix33/34` / `Matrix3x` names are gone.
- **`GameMath.h` coverage confirmed.** Phase 5 (`threed.cpp`) needs only native
  DirectXMath (`XMVector3Transform*`, `XMMatrixTranspose`, `XMMatrixMultiply`) plus
  existing helpers (`Dotf`, `Normalize`, `Cross`) — no new helper required there.
- **Added one genuinely-missing helper:** `Neuron::Math::Orthonormalize(FXMMATRIX)`
  in [`GameMath.h`](../NeuronCore/GameMath.h) to replace `tidy_matrix`
  (space.cpp:190,476). DirectXMath has no orthonormalize call, so this is real
  behavior, not a wrapper. It **faithfully reproduces the legacy single-component
  adjustment — *not* classic Gram–Schmidt** — so the §4/Phase-0 goldens match; it
  mirrors the already-certified `Detail::Orthonormalize` in
  [`FlightSystem.h`](../GameLogic/FlightSystem.h). Float-exactness vs. the legacy
  golden is cross-checked when its consumer lands in **Phase 4** (and the
  `TODO(windows)` `tidy_matrix` literals are captured).

### Phase 2 — Render-boundary conversions (`Vector3d` / `Vector3i64` → `XMVECTOR`)
- `Vector3d` and `Vector3i64` are **out of scope** (decision §1) — do **not**
  convert their storage or sim-side compute. They stay fp64 / int64.
- The only in-scope work is at the **render boundary**: where the legacy float
  render path consumes a world/local offset (`RelativeTo` / `LocalOffset`, which
  return `Vector3d`), convert double→`XMVECTOR` there with
  `XMVectorSet((float)dx, (float)dy, (float)dz, 0.0f)`. The conversion is the
  load→compute boundary; everything upstream stays double.

### Phase 3 — Migrate storage types (`Transform`, `local_object`, `RenderRecord`)
- In [`GameComponents.h`](../DeepspaceOutpost/GameComponents.h),
  [`space.h`](../DeepspaceOutpost/space.h), and **(main)**
  [`ReplicatedScene.h`](../DeepspaceOutpost/ReplicatedScene.h)'s `RenderRecord` +
  [`Camera.h`](../DeepspaceOutpost/Camera.h)'s `Vector position`:
  `Vector location → XMFLOAT3`, `Matrix rotmat → XMFLOAT4X4` (decision §1).
- This is the **highest-blast-radius** change (every consumer touches these), so
  it lands as a focused commit. These fields are **not** persisted as 3×double
  (decision §1), so the switch is internal-only — **no** wire/save format version
  bump. Still build and run the snapshot/replication paths to confirm nothing
  assumed the old in-memory layout.
- At each compute site introduce explicit `XMLoadFloat3` / `XMStoreFloat3`
  (and `XMLoadFloat4x4` / `XMStoreFloat4x4`) at the load→compute→store boundary.

**Execution — whole-cluster migration delivered in buildable installments.** The
`Vector`/`Matrix` typedefs are shared, so consumers cascade; rather than one
unverifiable ~270-site blind diff, each installment migrates one storage type and
**converts at the boundary** to not-yet-migrated code, keeping the tree buildable.
(I can't compile MSVC/DirectXMath here, so each installment needs a Windows build.)

- **Installment 1 — `RenderRecord` (done, pending build).** `RenderRecord` →
  `XMFLOAT3`/`XMFLOAT4X4`; `BuildRenderRecords` rewritten in `XMVECTOR`/`XMMATRIX`
  (`XMVector3TransformNormal` for the world→camera transform, `XMMatrixMultiply`
  for the orientation — the §4 `B·v` convention, exercised headlessly by
  `ReplicatedSceneTests`). Consumed only by `space.cpp` (converted at the
  `obj.location = rec.location` boundary into the still-legacy `local_object`) and
  the test (rotmat checks → `XMFLOAT4X4._rc`). `find_lock_target` reads `.x/.y/.z`
  unchanged. `ReplicatedScene.h` no longer includes `vector.h`.
- **Installment 2 — `local_object` / `Transform`** (`space.h`, `GameComponents.h`)
  + `space.cpp` / `threed.cpp` / `swat.cpp` consumers. The large one.
- **Installment 3 — `Camera.h`** `Vector position` + `ApplyCamera`, and the
  remaining `pilot/missions/intro/main` sites.
- **Then Phase 7** deletes `vector.{h,cpp}` once nothing references them.

### Phase 4 — Migrate `space.cpp` (sim / motion)
- Rewrite `move_local_object`, `rotate_vec`, approach/docking vectors in
  `XMVECTOR`/`XMMATRIX`. Hoist `XMLoad*` before loops, `XMStore*` after.
- **`rotate_vec` is a sequential shear integrator, not a rotation** — reproduce
  the exact shear in `XMVECTOR` (decision §1(A)). Do **not** substitute
  `XMMatrixRotation*` here: `α = roll/256` (up to ~0.5) is a linear coefficient,
  not radians (`sin 0.5 ≠ 0.5`), so true rotation changes handling rates and makes
  `tidy_matrix` partly redundant. The true-rotation upgrade is a separate, tuned
  follow-up (§1(B)).
- Preserve the flight-intent semantics (roll/climb/speed from `FlightRates`).
- Validate against the Phase-0 golden tests within tolerance.
- **Reference (out of scope, server-side).** The authoritative
  [`FlightSystem.h`](../GameLogic/FlightSystem.h) now rotates a ship's basis about
  its **own** axes via `RotateBasis` (`B' = B·M`, where `M`'s columns come from the
  same legacy shear), bit-identical to the world-frame form at the identity
  orientation. It runs in `Vector3d` and is **not** migrated, but it is the
  canonical "rotate about own axes" reference if the client motion is ever reframed.
  Note `StepFlight` now **skips `Missile` entities** (homing missiles are integrated
  by `StepMissiles`).

### Phase 5 — Migrate `threed.cpp` (render / projection)
- Model→camera vertex loop: transform the whole vertex array with
  **`XMVector3TransformCoordStream`** (and `XMVector3TransformNormalStream` for
  normals) rather than a hand `XMVector3Transform` per vertex — the idiomatic,
  faster DirectXMath form. Ship points are **ints**: load via
  `XMVectorSet((float)x, …)` (or `XMLoadSInt3` + `XMConvertVectorIntToFloat`), not
  a float reinterpret.
- Replace the manual element-swap transpose ([`threed.cpp:205`](../DeepspaceOutpost/threed.cpp))
  with `XMMatrixTranspose`, and apply the §4 convention transpose correctly.
- **Do not turn the projection into a matrix.** The legacy projection
  ([`threed.cpp:233`](../DeepspaceOutpost/threed.cpp)) is a hand divide-by-z
  (`sx=(rx*256)/rz; sy=-(ry*256)/rz; +128/+96`) with a `rz<=0 → rz=1` near clamp.
  `XMMatrixPerspectiveFovLH` would change NDC mapping and the clamp. Vectorize only
  the **transform**; keep the divide and clamp as-is (scalar, or `XMVectorReciprocal`).
- Back-face / normal checks: `Neuron::Math::Dotf`. The signed-area back-face test
  and `sy = -sy` are coupled to winding/handedness — preserve the `-Z` basis **and**
  the winding together, or faces cull inversely.
- **(main) Also migrate the client render-frame transform** in
  [`ReplicatedScene.h`](../DeepspaceOutpost/ReplicatedScene.h) `BuildRenderRecords`
  / `toCamera` — it is the `B·v` transpose convention (§4), and the obvious place to
  get the transpose wrong. The new **target-reticle** and **`find_lock_target`**
  sites in [`space.cpp`](../DeepspaceOutpost/space.cpp) reuse the same divide-by-z
  projection (`x*256/z + 128`, `-(y*256/z) + 96`) and forward-cone test — migrate
  them the same way (vectorize the transform, keep the scalar divide/clamp).
- Verify ships/planets aren't mirrored or inside-out after the change.

### Phase 6 — Migrate `swat.cpp` + remaining consumers
- `add_new_ship` orientation setup, NPC spawn matrices, tactics geometry →
  `XMMATRIX` / `Neuron::Math::CreateRotationMatrix`.
- Sweep `pilot.cpp`, `missions.cpp`, `intro.cpp`, `main.cpp` for residual
  `struct vector` / `Matrix` / `mult_*` / `unit_vector` calls.

### Phase 7 — Delete the legacy layer
- Remove [`vector.h`](../DeepspaceOutpost/vector.h) and
  [`vector.cpp`](../DeepspaceOutpost/vector.cpp); drop them from
  `DeepspaceOutpost/CMakeLists.txt`.
- Grep-gate: no remaining `struct vector`, `Matrix[`, `mult_matrix`,
  `mult_vector`, `vector_dot_product`, `unit_vector`, `set_init_matrix`,
  `tidy_matrix`, `rotate_vec` outside history.
- Build x64 Debug + Release; run the full test suite via `ctest`
  (the `NeuronCore.Tests` GoogleTest target includes the math goldens); manual flight/render
  smoke test (fly, rotate, dock, view ships & planets).

---

## 7. Precision & determinism risk

Moving the **authoritative** flight/rotation path from `double` to `float32` is
the one behavioral risk in this plan and deserves explicit attention:

- **Determinism.** The bar is **reproducible-enough**, not cross-machine
  bit-exactness (decision §1), so default `/fp` is acceptable and no path is
  forced to stay integer/fixed-point. Note that float32 SIMD still differs
  bit-for-bit from the old double path and potentially across x86 vs. x64 — fine
  under this bar, but if a future feature (lockstep/replay) raises it, revisit:
  pin `/fp:precise` (or `/fp:strict`) and avoid FMA-contraction differences. The
  roadmap's deterministic integer/fixed-point physics
  ([`MIGRATION_ROADMAP.md` §"good news"](MIGRATION_ROADMAP.md)) remains in
  integer where it already is — this migration only moves the float/double math.
- **Shear integrator.** `rotate_vec` is a non-orthonormal shear *by design*; its
  drift is exactly why `tidy_matrix` exists. Reproduce the shear faithfully
  (§1(A)) rather than substituting a true rotation, which would change handling
  and partly obsolete `tidy_matrix`.
- **Drift.** Re-orthonormalization (`tidy_matrix` → `Orthonormalize`) matters more
  in float32; keep it every frame, in the legacy axis order, so the basis doesn't
  skew.
- **World precision is unaffected** — absolute positions stay `Vector3i64`; only
  the small floating-origin-relative delta is ever in float, which is exactly the
  range float32 handles cleanly.
- **Tolerance.** Phase-0 golden tests compare float results to the old double
  oracle within a documented epsilon, scaled by magnitude (relative, not
  absolute, for large coordinates).

---

## 8. DirectXMath performance & ABI notes

Consolidated DirectXMath-specific guidance (also referenced inline above):

- **Batch vertex transforms with the Stream APIs.** `XMVector3TransformCoordStream`
  / `XMVector3TransformNormalStream` over a vertex array beat a hand
  `XMVector3Transform` loop and are the idiomatic form (Phase 5).
- **Cross the SIMD→scalar boundary once.** Avoid repeated `XMVectorGetX/Y/Z`;
  `XMStoreFloat3` and read the struct in scalar tails (the perspective divide).
- **Precise over `*Est`.** Keep golden-test tolerances tight; opt into
  `*Est`/`*Reciprocal` only where a hot path measurably benefits.
- **No `XMVECTOR`/`XMMATRIX` in storage or containers.** They need 16-byte
  alignment; keep them out of ECS components, `std::vector` elements, and
  serialized structs. Storage stays `XMFLOAT3` / `XMFLOAT4X4` (unaligned, movable).
  C++23 aligned-`new` exists, but storage types sidestep the issue entirely.
- **`XMFLOAT4X4{}` ≠ identity.** Zero-init leaves `m33 = 0`; start from
  `XMMatrixIdentity()` and use `XMVector3TransformNormal` for rotation-only data.
- **Pin `/arch` and `/fp` in the presets.** Reproducibility within a build depends
  on them; treat any change as a re-baseline of the golden tests.
- **x86 + x64 both ship.** Re-check `XM_CALLCONV` / `FXMVECTOR`…`CXMVECTOR`
  ordering on 32-bit, where the vector-register passing rules are stricter.

---

## 9. Native-first guardrails (apply throughout)

- ✅ Call `XMVector3Normalize` / `XMVector3Transform` / `XMMatrixMultiply` and the
  existing `Neuron::Math` helpers directly.
- ❌ Do **not** add new thin forwarders (a `Multiply(a,b)` that just calls
  `XMMatrixMultiply`), and do **not** add math methods to storage types
  (`XMFLOAT3` / `Transform`).
- ❌ Do **not** add `XMFLOAT3` arithmetic operators.
- ❌ Do **not** keep `vector.h`/`vector.cpp` alive "just in case" — deletion in
  Phase 7 is the definition of done.
- ✅ A genuinely new operation (real behavior over `XMVECTOR`, e.g. basis
  orthonormalization) goes into `Neuron::Math` once, never per-file.

---

## 10. Resolved decisions

The earlier open questions are now settled (folded into [§1](#1-decisions-locked)):

1. **Legacy type naming** → fix `AGENTS.md`/`coding-standards.md` to match the
   code (`struct vector` / `Matrix`); don't rename the code. (Phase 1)
2. **Serialized / wire layout** → `Transform.location` / `rotmat` are **not**
   persisted as 3×double; the storage switch is internal-only, **no** format
   version bump. (Phase 3)
3. **Determinism** → **reproducible-enough**, not bit-exact; default `/fp`,
   nothing forced to stay integer for determinism. (Phase 4 / §7)
4. **`Matrix` storage** → **`XMFLOAT4X4`** (engine-uniform), not `XMFLOAT3X3`.
   (Phase 3)
