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
| **`Vector3d` / `Vector3i64`** | **Both stay — out of scope.** | Neither has a DirectXMath equivalent (DirectXMath is float32-only), so they are **not** wrappers and **not** migration targets. `Vector3d` is a legitimate fp64 `Neuron::Math` type carrying the authoritative server sim's precision-sensitive state — the `ShipFrame` orientation basis and the **`carry` sub-unit remainder** that steps the `int64` world (`SimComponents.h` / `FlightSystem.h`), plus `LocalOffset` (`SnapshotInterpolator.h`). `Vector3i64` holds absolute `int64³` world coordinates that must stay exact (see [`MathTests`](../Tests/MathTests.cpp)). The migration touches them only at the **render boundary**: when the legacy float render path consumes a world/local offset, convert double→`XMVECTOR` there (`XMVectorSet((float)dx, …)`). |
| **`GameMath.h` helper layer** | **Use `Neuron::Math` helpers freely.** | `Neuron::Math` is the sanctioned helper layer; calling `Normalize`, `Cross`, `Dot`, `RotateAround`, `Vector3::FORWARD`, etc. is *not* a native-first violation. The no-wrapper rule applies to **new** code: do **not** add fresh thin forwarders, and do **not** keep the legacy `vector.h` wrappers alive. |
| **Determinism** | **Reproducible-enough (no cross-machine bit-exactness).** | Same-build reproducibility is the bar; no lockstep/replay bit-exact guarantee. Default `/fp` is acceptable — no path is forced to stay integer/fixed-point for determinism's sake. |
| **`Transform.rotmat` storage** | **`XMFLOAT4X4` (engine-uniform).** | Matches the rest of the engine's matrices for uniform `XMLoadFloat4x4`/`XMStoreFloat4x4` and easy translation folding; the 3×3 rotation basis occupies the upper-left, row 3 / column 3 identity. |
| **Legacy type naming** | **Fix the docs to match the code.** | `AGENTS.md` / `coding-standards.md` get updated to say `struct vector` / `Matrix`; the code is **not** renamed (the types are deleted in Phase 7 anyway). |
| **Serialization** | **`Transform.location` / `rotmat` are not persisted as 3×double.** | The storage switch is internal-only — **no** wire/save format version bump is required. |
| **Flight integrator (`rotate_vec`)** | **(A) Preserve the legacy shear math in `XMVECTOR` first; (B) true-rotation upgrade is a separate follow-up.** | `rotate_vec` is a sequential **shear integrator**, not a rotation (see [§7](#7-precision--determinism-risk)). Reproducing it faithfully keeps flight feel and lets the golden tests pass tightly; swapping in `XMMatrixRotation*` is a deliberate gameplay-tuning change, done later and on its own. |
| **Build flags (`/arch`, `/fp`)** | **Pin both in the CMake presets and baseline against them.** | DirectXMath's code path and FMA contraction depend on `/arch` (SSE2 vs AVX/AVX2) and `/fp`. Pin them so reproducibility holds within a build and any later `/arch:AVX2` flip is a conscious, re-baselined change — not a silent golden-test break. |

---

## 2. Current state

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

> **Naming note.** `AGENTS.md` and `coding-standards.md` refer to the legacy
> types as `LegacyVector2/3` and `Matrix33/34`. Those names do **not** exist in
> the code — the real types are `struct vector` and `Matrix` (`vector.h`). Treat
> the doc names as aliases for these. Update those references when this lands.

### 2.2 Consumers of legacy math

| File | Legacy math used for |
|---|---|
| [`space.cpp`](../DeepspaceOutpost/space.cpp) (~1360 ln) | `move_local_object`, `rotate_vec`, object position/velocity integration, distance, docking/approach vectors |
| [`threed.cpp`](../DeepspaceOutpost/threed.cpp) (~1000 ln) | `draw_solid_ship`, `render_planet`, `draw_wireframe_planet` — model→camera transform, projection, back-face/normal checks |
| [`swat.cpp`](../DeepspaceOutpost/swat.cpp) (~1230 ln) | `add_new_ship` rotation-matrix setup, NPC spawn orientation, tactics geometry |
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
  float32-only types can't).
- [`Vector3i64.h`](../NeuronCore/Vector3i64.h) — int64 world coordinate (**stays**).
- `GameLogic/`, `NeuronServer/`, `Server/`, most of `Tests/`, and the newer
  client files (`Camera.h`, `CameraFollow.h`, `RenderQueue.h`, `ReplicatedScene.h`)
  already use `XMVECTOR` / `XMFLOAT3` / `Neuron::Math`. They are **not** part of
  this migration except where they touch a `Transform` whose storage type changes.

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
- Add characterization tests in [`Tests/MathTests.cpp`](../Tests/MathTests.cpp)
  that capture the **current** double-precision outputs of `mult_vector`,
  `mult_matrix`, `unit_vector`, `tidy_matrix`, `rotate_vec`, and a full
  `move_local_object` step for representative inputs. These become the
  float-tolerance oracle for every later phase.
- **Snapshot whole matrices and whole transformed vertex sets, not just scalars,
  and use a rotation about a skew axis** — a symmetric/identity case would hide
  the §4 convention transpose and any handedness flip.
- Decide the comparison tolerance for float32 vs. the old double path and record
  it in the test file (relative epsilon scaled by magnitude — see
  [§7](#7-precision--determinism-risk)).

### Phase 1 — Reconcile the helper layer & docs *(no behavior change)*
- Fix `AGENTS.md` / `coding-standards.md` to name the real legacy types
  (`struct vector` / `Matrix`); the code is **not** renamed (decision §1).
- Confirm `GameMath.h` already covers the Phase-5 call sites; add any genuinely
  missing real helper (e.g. a Gram–Schmidt `Orthonormalize(XMMATRIX)` to replace
  `tidy_matrix`) **once**, in `Neuron::Math`.

### Phase 2 — Render-boundary conversions (`Vector3d` / `Vector3i64` → `XMVECTOR`)
- `Vector3d` and `Vector3i64` are **out of scope** (decision §1) — do **not**
  convert their storage or sim-side compute. They stay fp64 / int64.
- The only in-scope work is at the **render boundary**: where the legacy float
  render path consumes a world/local offset (`RelativeTo` / `LocalOffset`, which
  return `Vector3d`), convert double→`XMVECTOR` there with
  `XMVectorSet((float)dx, (float)dy, (float)dz, 0.0f)`. The conversion is the
  load→compute boundary; everything upstream stays double.

### Phase 3 — Migrate storage types (`Transform`, `local_object`)
- In [`GameComponents.h`](../DeepspaceOutpost/GameComponents.h) and
  [`space.h`](../DeepspaceOutpost/space.h): `Vector location → XMFLOAT3`,
  `Matrix rotmat → XMFLOAT4X4` (decision §1).
- This is the **highest-blast-radius** change (every consumer touches these), so
  it lands as a focused commit. These fields are **not** persisted as 3×double
  (decision §1), so the switch is internal-only — **no** wire/save format version
  bump. Still build and run the snapshot/replication paths to confirm nothing
  assumed the old in-memory layout.
- At each compute site introduce explicit `XMLoadFloat3` / `XMStoreFloat3`
  (and `XMLoadFloat4x4` / `XMStoreFloat4x4`) at the load→compute→store boundary.

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
- Build x64 Debug + Release; run the full `Tests/` suite; manual flight/render
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
