# explosion.md — Porting the Darwinia Particle & Explosion Systems into Deepspace Outpost

## 1. Goal

Bring the two Darwinia source pairs from `Expl/` — `ParticleSystem.{h,cpp}` (a billboard
particle engine) and `explosion.{h,cpp}` (a mesh-shatter "flying triangles" effect) — into
Deepspace Outpost as a real 3D visual-effects layer that:

* turns a ship/station death into **tumbling, fading debris triangles** built from the dead
  hull's own mesh, plus a **glowing additive particle burst** (fireball core + sparks), and
* exposes a reusable `CreateParticle(...)` entry point for future effects (muzzle flashes,
  missile trails, thruster plumes).

This **replaces** the current placeholder death effect — the legacy Elite 2D white-pixel spray
in `DeepspaceOutpost/threed.cpp::draw_explosion` and the `ReplicatedExplosion` book-keeping in
`DeepspaceOutpost/space.cpp` are deleted once the new effect is in.

> **Confirmed decisions** (see §10): **(1)** the new 3D effect *replaces* the legacy 2D one;
> **(2)** the whole system — simulation *and* GPU pass — lives in **`NeuronClient`** as a
> **game-agnostic engine subsystem**; the game only *feeds* it (`CreateParticle` /
> `AddExplosion`) and *sets the frame's floating origin*; **(3)** only the **space-relevant
> particle subset** is ported.

> **This is a transformation, not a copy-paste.** The donor files were adapted to a *different*
> engine's conventions. Almost none of the types they name exist here (`LegacyVector3`,
> `Matrix33/34`, `RGBAColor`, `Shape`/`ShapeFragment`, `FastDArray`, `LList`, `SliceDArray`,
> `g_gameTime`, OpenGL `glBegin/glEnd`, `Resource::GetTexture`). Every one maps onto a native
> Deepspace Outpost idiom below. The donor's *behaviour and tuning* is what we keep; its *code*
> is rewritten to this codebase's D3D11 / DirectXMath / std-container / palette conventions.

---

## 2. What the donor code actually does (behaviour to preserve)

**`ParticleSystem`** — a pool of `Particle`s. Each particle is one **camera-facing textured quad**
drawn with **additive blending** (`GL_SRC_ALPHA, GL_ONE`), driven by a `ParticleType` table
(life, size, friction, gravity, spin, start/end colour). Types include `ExplosionCore`,
`ExplosionDebris`, `Spark`, `MuzzleFlash`, `Fire`, `MissileTrail`, `MissileFire`, … Particles
fade out near end of life, drift with friction, some fall under gravity, some spawn child
particles (`ExplosionDebris` → `RocketTrail`), some bounce off the landscape.

**`Explosion` / `ExplosionManager`** — takes a `Shape` (a tree of `ShapeFragment`s, each a list of
triangles) and spawns **one flying `ExplodingTri` per mesh triangle**. Each tri gets an outward
velocity from the shape centre, a shared `Tumbler` (a rotation matrix nudged every frame so the
fragment spins), gravity, friction, and a lifetime. `Render()` draws them as lit, textured,
colour-material triangles that fade to transparent as they die.

**Call site (donor):** `Building::Destroy` fires `AddExplosion(shape, mat)` ×3, a landscape
`Bang`, an "Explode" sound, and a spray of `TypeExplosionCore` particles.

### Space-game adaptation (important)

Darwinia is a **ground game with gravity and a landscape**. Deepspace Outpost is **space**:
zero-g, no ground. So during the port we **drop**:

* `ACCEL_DUE_TO_GRAV` / per-type `gravity` fall and `INITIAL_VERTICAL_SPEED` (no "up"),
* the landscape height-map bounce in `Particle::Advance` (`m_location->m_landscape…`),
* the child-spawning `RocketTrail` off `ExplosionDebris` (optional — keep if we port trails).

We **keep**: outward radial velocity from the blast centre, light friction (so debris settles
into a slow drift), per-fragment tumbling, additive glowing particles, and lifetime fade.

---

## 3. Donor → Deepspace Outpost type & idiom map

| Donor construct | Deepspace Outpost replacement | Source of truth |
|---|---|---|
| `LegacyVector3` (arithmetic vector) | `Neuron::Math::Vector3d` (`+ - *`, `Dot`, `Cross`, `Length`, `Normalized`) in new modern code | `NeuronCore/Vector3d.h` |
| `Matrix33` / `Matrix34` (tumble/transform) | `XMMATRIX` + `Neuron::Math::CreateRotationMatrix` / `RotateAround`; store `XMFLOAT3X3`/`XMFLOAT4X4` | `NeuronCore/GameMath.h` (AGENTS.md §DirectXMath) |
| `RGBAColor` / `rgb_colour` | palette index `int` (`GFX_COL_*`) → `Renderer::paletteColour(idx)` → packed `uint32_t` `0xAABBGGRR`; or store `uint32_t`/`XMFLOAT4` directly | `NeuronClient/GamePalette.h`, `platform/Renderer.h` |
| `Shape` / `ShapeFragment` / `ShapeTriangle` / `VertexPosCol` | `Neuron::Graphics::MeshData` / `MeshVertex{pos,normal,rgba}`, **already fan-triangulated** (triangle *t* = indices[3t..3t+2]) | `NeuronClient/Mesh.h`, `DeepspaceOutpost/SceneMeshes.cpp::build_ship_mesh` |
| `FastDArray<T>`, `LList<T*>`, `SliceDArray<T>` | `std::vector<T>` (swap-erase / `std::erase_if` to cull) — these custom containers **do not exist and must not be reintroduced** | AGENTS.md §Native-First |
| `NEW T[]` / `delete[]` / manual pool | value semantics inside `std::vector`; no manual `new` | — |
| `frand()` → [0,1) | `randint() / 2147483647.0f` (add a small local `frand01()` helper) | `DeepspaceOutpost/random.h` |
| `sfrand(x)` / `syncsfrand(x)` → [-x,x] | `((rand255() - 128) / 128.0f) * x` (local `sfrand(x)` helper) | `DeepspaceOutpost/random.h` |
| `darwiniaRandom()` | `randint()` | `DeepspaceOutpost/random.h` |
| `g_gameTime` / `g_advanceTime` / `SERVER_ADVANCE_PERIOD` | **fixed per-frame dt** — the client is frame-count driven; advance per-frame by a fixed step (mirror `local_object.exp_delta` / `ReplicatedExplosion.frames`). No wall clock. | `NeuronClient/ClientEngine.cpp::Frame` |
| `GRAVITY`, landscape bounce, vertical speed | **removed** (space, zero-g) | — |
| `glBegin/glColor/glTexCoord/glVertex/glEnd` | build a `std::vector<ParticleVertex>` / `MeshVertex` and upload to a **dynamic VB**, draw with a shader | `NeuronClient/graphics/Scene3D.cpp` (`renderDust`, `renderBillboard` templates) |
| `glBlendFunc(GL_SRC_ALPHA, GL_ONE)` (additive) | a **new** `ID3D11BlendState` (`SrcBlend=SRC_ALPHA, DestBlend=ONE, Op=ADD`) — none exists yet | create in the new pass |
| depth for transparent tris | a **new** depth state: `DepthEnable=TRUE, DepthWriteMask=ZERO` (test against ships, don't occlude each other) — none exists yet | create in the new pass |
| `Resource::GetTexture("textures\\particle.bmp")` | `TextureManager::LoadTexture("Textures/Particle.dds")` (`.dds` assets already present) | `NeuronClient/graphics/TextureManager.h`; `GameData/Textures/Particle.dds`, `Glow.dds`, `Shapewireframe.dds`, `Starburst.dds` |
| `g_app->m_renderer->SetObjectLighting()` | reuse the `Scene3D` lit/flat mesh path for debris; particles are unlit additive | `Scene3D::SetLightingEnabled` |
| `g_app->m_particleSystem->CreateParticle(...)` | `Neuron::Client::EffectsInstance().CreateParticle(...)` (engine subsystem singleton) | this doc |
| `g_explosionManager.AddExplosion(shape, mat)` | `Neuron::Client::EffectsInstance().AddExplosion(shipType, worldPos, rotmat, fraction)` (pulls `MeshData` from the already-registered `Scene3D` mesh provider) | this doc |
| `g_app->m_location->Bang(pos, i, i/4)` (camera shake) | optional: publish a camera-shake reaction on `g_clientBus`, or omit | `DeepspaceOutpost/main.cpp` bus |
| `g_app->m_soundSystem->TriggerBuildingEvent(this,"Explode")` | `snd_play_sample(SND_EXPLODE)` (already called by the death handlers) | `NeuronClient/sound.h` |
| `START_PROFILE/END_PROFILE` | drop (no equivalent macro in the client presentation tier) | — |

---

## 4. How this hooks into the frame loop (engine-owned subsystem)

Deepspace Outpost has **no `GameApp` singleton** with `m_particleSystem` / `m_explosionManager`
members. The frame is driven by `ClientEngine::Frame` → `GameApp::Update()` → `game_update()` and
`GameApp::RenderScene()` → `game_render_scene()`. Per decision **(2)**, the new effects are an
**engine subsystem in `NeuronClient`**, reached through a Meyers-singleton accessor
`Neuron::Client::EffectsInstance()` — exactly the pattern of `Client::ReplicationClientInstance()`
and `Client::MainCamera()`. Its **lifecycle is engine-owned**; the game only spawns into it and
supplies the per-frame origin:

* **Advance (engine)** — `ClientEngine::Frame` already computes the fixed timestep and calls
  `m_main->Update(dt)`. Advance the subsystem there (or first thing in `game_update_flight`),
  so the game never has to tick it. `EffectsInstance().Advance(step)` integrates every particle
  and `ExplodingTri` and compacts dead ones.
* **Spawn (game)** — from the existing client event-bus subscriptions in
  `DeepspaceOutpost/main.cpp` (`register_client_event_handlers`). Re-point the two bodies that
  today call the legacy spawners:
  * `g_clientBus.Subscribe<Neuron::Msg::EntityDeath>` (was `spawn_replicated_explosion(vs)`)
  * `g_clientBus.Subscribe<Neuron::Msg::ExplosionAt>` (was `spawn_explosion_at(pt, scale)`)
  at `EffectsInstance().AddExplosion(...)` + `EffectsInstance().CreateParticle(...)`.
  `snd_play_sample(SND_EXPLODE)` stays.
* **Origin (game)** — once per frame the game passes the floating-origin the render frame is
  measured against: `EffectsInstance().SetOrigin(camera_rig_origin())` (the engine can't reach
  the game-side `CameraRig`). This is the `Scene3D::SetDust` precedent — a per-frame setter.
* **Render (engine)** — the subsystem's GPU pass is invoked from `gfx_render_3d_scene()`
  (`NeuronClient/platform/GameScene.cpp`) **after** `Scene3D::RenderModels` and before
  `SceneGlow::Composite`, so debris/particles depth-test against the world and pick up bloom.

The game therefore links no new sim code of its own: it *registers* the mesh/texture providers
(already done for `Scene3D`), *spawns*, and *sets the origin*. Everything else is in `NeuronClient`.

---

## 5. Rendering design (native Direct3D 11)

There is **no OpenGL and no immediate mode** here. Rendering is the "submit then flush" seam:
the game fills world-frame records and a static renderer draws them with `MainCamera().View()` /
`Projection()` and the hardware depth buffer. The scene pass is `Neuron::Graphics::Scene3D`
(`NeuronClient/graphics/Scene3D.cpp`), driven by `gfx_render_3d_scene()`
(`NeuronClient/platform/GameScene.cpp`). `SceneGlow` (opt-in) blooms the scene target afterward,
so **additive particles drawn into that target get bloom for free**.

Add a new sibling renderer **`Neuron::Graphics::SceneParticles`** in `NeuronClient/graphics/`
(mirroring the all-static `SceneGlow` / `Scene3D` shape, `winrt::com_ptr`, `check_hresult`,
`PascalCase`, 2-space indent). The `EffectsInstance()` subsystem (§6) hands it the two per-frame
vertex batches it built from the live simulation. It owns two sub-passes, both drawn after the
opaque ship pass and using the camera the scene already set (`s_viewMat`/`s_projMat` convention —
matrices uploaded **transposed**, consumed as `mul(u_MVP, pos)`; see `Scene3D` comments):

### 5a. Debris-triangle pass (the mesh shatter)

* Vertex format: **reuse `Neuron::Graphics::MeshVertex`** (`pos`, `normal`, `rgba`) and the
  existing `scene3d` mesh shader / input layout — debris is just loose, per-frame triangles.
* State: depth **test + write ON**, `CULL_NONE` (solid opaque fragments, like ships). Fade is by
  **shrinking + culling** at end of life (opaque path), or — if a soft fade is wanted — a second
  draw with the additive/alpha blend state below and depth-write OFF for the last ~20% of life.
* Geometry: each frame, `SceneParticles` rebuilds a `std::vector<MeshVertex>` from the live
  `ExplodingTri`s (3 verts/tri, transformed by the tumbler and offset by the fragment position),
  uploads to a lazily-grown **dynamic VB** (Map `WRITE_DISCARD`/memcpy/Unmap — copy the
  `renderDust` pattern in `Scene3D.cpp`), and `DrawIndexed`/`Draw`.

### 5b. Additive billboard particle pass (the fireball / sparks / trails)

* New vertex `struct ParticleVertex { float x,y,z; float u,v; uint32_t rgba; };` (a new tiny
  input layout: `POSITION` R32G32B32, `TEXCOORD` R32G32, `COLOR` R8G8B8A8_UNORM).
* New shader pair `sceneparticleVS/PS.hlsl` (offline-compiled by fxc → `CompiledShaders/`), a
  textured quad tinted by per-vertex colour: `return tex.Sample(samp, uv) * vtxColor;`.
* **New `ID3D11BlendState`** — additive: `BlendEnable=TRUE, SrcBlend=SRC_ALPHA, DestBlend=ONE,
  BlendOp=ADD`. **New depth state** — `DepthEnable=TRUE, DepthWriteMask=ZERO` (occluded by ships,
  but particles don't occlude each other). Linear sampler.
* Camera-facing quads: build each particle's 4 corners in **camera space** exactly like
  `Scene3D::renderBillboard` (offset ±half-size in camera x/y at the particle's view-space depth),
  or in world space using `MainCamera().Up()` and `right = normalize(cross(forward, up))` — the
  donor's `cameraController->GetUp()/GetRight()` equivalent. Batch **all** particles into one
  dynamic VB and issue a single draw (donor did one `glBegin(GL_QUADS)` per particle; we batch).
* Texture: `TextureManager::LoadTexture("Textures/Particle.dds")` (soft dot) for cores/sparks;
  `Glow.dds` / `Starburst.dds` are alternatives for the fireball flash.

### 5c. Where the pass is invoked

Add `SceneParticles::Render(rtv, dsv, MainCamera(), vpX,vpY,vpW,vpH)` at the tail of
`gfx_render_3d_scene()` in `GameScene.cpp` (after `Scene3D::RenderModels`, before
`SceneGlow::Composite`), so the effects land in the glow target and share the scene depth buffer.
The `SceneParticles` renderer only draws the two vertex batches the `EffectsInstance()` subsystem
produced this frame (built inside `Advance`/`BuildVertices`) and pushed via
`SceneParticles::SetDebris(const MeshVertex*, int)` / `SceneParticles::SetParticles(const
ParticleVertex*, int)` (the `Scene3D::SetDust` precedent). Both the renderer and the simulation
now live in `NeuronClient`, so this is a self-contained engine pass — `gfx_render_3d_scene` just
drives it.

---

## 6. Simulation design (the ported logic)

Two small classes hold the CPU-side state, **in `NeuronClient`** (decision 2), modern
`PascalCase`, 2-space indent, `Neuron::Client` namespace. A thin subsystem
`Neuron::Client::Effects` owns a `ParticleSystem` + `ExplosionManager` and the `SceneParticles`
renderer, and is exposed as `Neuron::Client::EffectsInstance()` (Meyers singleton, like
`ReplicationClientInstance`). `Advance` builds the frame's two vertex batches and pushes them to
`SceneParticles`; `SetOrigin`, `CreateParticle`, `AddExplosion` are the game-facing entry points.

### 6a. `Particle` / `ParticleType` / `ParticleSystem`

Straight port of the donor with the space adaptations from §2:

```cpp
// Storage-only members (XMFLOAT3), compute in XMVECTOR per AGENTS.md.
struct Particle {
  Neuron::Math::Vector3i64 anchor;  // absolute world point the offset is measured from
  DirectX::XMFLOAT3 offset;         // render-frame position = (anchor - camOrigin) + offset
  DirectX::XMFLOAT3 vel;            // world units / step
  int   typeId;
  float size;
  float age;                        // frames (or seconds) lived
  uint32_t colour;                  // 0xAABBGGRR, lerped colour1→colour2 at spawn
};
```

* `ParticleType` table (`life`, `size`, `friction`, `spin`, `colour1`, `colour2`) ports verbatim
  **minus `gravity`** (or keep `gravity` as a generic "drift" accel that defaults to 0 in space).
  Colours become palette-resolved `uint32_t` or literal `Render2D::Rgba(r,g,b,a)` values instead
  of `RGBAColor::Set(r,g,b)`.
* `Advance(step)`: `offset += vel*step; vel *= (1 - friction*step);` fade the colour's alpha over
  the last quarter of life (donor's `startFade` logic); cull when `age > life`. **Remove** the
  landscape bounce and the `ExplosionDebris`→`RocketTrail` child-spawn (or keep the child-spawn
  as a pure-VFX option). Use `std::erase_if` to compact the pool (donor used a slice pool).
* `CreateParticle(worldPos, vel, typeId, size)` replaces the donor signature; it pushes into the
  `std::vector<Particle>`.
* `BuildVertices(camOrigin, camera) -> std::vector<ParticleVertex>`: per particle, compute the
  camera-facing quad and append 6 verts; hand to `SceneParticles::SetParticles`.

Port the **space-relevant subset** (decision 3): `ExplosionCore`, `ExplosionDebris`, `Spark`,
`MuzzleFlash`, `Fire`, `MissileTrail`, `MissileFire`. Drop the Darwinia-only `Leaf`,
`DarwinianFire`, `Brass`, `ControlFlash`, `BlueSpark`, and `RocketTrail` (or keep `RocketTrail`
only if the `ExplosionDebris` child-spawn trail is wanted). The `TypeInvalid`/`TypeNumTypes`
enum sentinels carry over.

### 6b. `Tumbler` / `ExplodingTri` / `Explosion` / `ExplosionManager`

```cpp
struct Tumbler {              // per-fragment spin; donor Matrix33 → XMFLOAT3X3 + angular vel
  DirectX::XMFLOAT3X3 rot;    // nudged each Advance()
  DirectX::XMFLOAT3   angVel;
  void Advance(float step);   // rot = rot * rotationFromAngVel(step); angVel *= rotFriction
};

struct ExplodingTri {
  DirectX::XMFLOAT3 v1, v2, v3;   // triangle verts relative to its own centre
  DirectX::XMFLOAT3 normal;
  DirectX::XMFLOAT3 offset;       // render-frame centre (integrated by vel)
  DirectX::XMFLOAT3 vel;
  uint32_t colour;
  int   tumbler;                  // index into the explosion's tumbler array
  float age;
};
```

* `Explosion(shipType, worldPos, rotmat, fraction)`:
  * Get triangles from the **`Scene3D` mesh provider** the game already registered
    (`Scene3D::SetMeshProvider(&build_ship_mesh)`) — `AddExplosion` invokes that callback to fill
    a `MeshData` for `shipType`, yielding `MeshVertex` triples already fan-triangulated. Reusing
    the registered provider is what keeps the engine subsystem **game-agnostic** (it never
    touches `ship_data`/`ship_solids` directly). (This is the donor's `_frag->m_triangles` loop,
    but the mesh is flat here — **no fragment tree**; §1 confirms one point table + one face list
    per hull, so no recursion.)
  * For each triangle: colour = `MeshVertex.rgba`; centre = average of the 3 verts; store verts
    relative to centre; `normal` from the mesh or `Cross(v1-v2, v2-v3)`; **velocity = outward
    from the hull centre × speed** (donor `(center - fragCenter) * MAX_INITIAL_SPEED`, **without**
    the `+= INITIAL_VERTICAL_SPEED`); assign a random tumbler; `age = 0`. Skip degenerate/tiny
    triangles (donor's `circum < 6` guard, rescaled to hull size).
  * `fraction < 1` randomly drops that share of triangles (donor behaviour; use `frand01()`).
  * Allocate `NUM_TUMBLERS` `Tumbler`s in a `std::vector` (donor used `NEW Tumbler[]`).
* `Advance(step)`: advance tumblers; integrate each tri's `offset += vel*step`, apply light
  friction, **no gravity**; return `true` when the whole explosion outlives `EXPLOSION_LIFETIME`.
* `AppendVertices(camOrigin) -> MeshVertex[]`: per live tri, `worldVert = tumbler.rot * vRel +
  offset (+ anchor - camOrigin)`; alpha ramps down with age; append 3 `MeshVertex`. Hand to
  `SceneParticles::SetDebris`.
* `ExplosionManager` holds `std::vector<Explosion>` (donor `LList<Explosion*>`), `Advance()`
  compacts finished ones, and `AddExplosion(shipType, worldPos, rotmat, fraction=1)` mirrors the
  donor's two overloads collapsed to one (no fragment recursion needed).

### 6c. Floating-origin correctness

Absolute world positions are `int64³` and the render frame is **relative to a moving origin**
(`camera_rig_origin()`), exactly as `ReplicatedExplosion` already handles. Store each
explosion/particle's spawn point as a `Neuron::Math::Vector3i64` **anchor**, integrate motion in
a small `XMFLOAT3` **offset**, and each frame compute the render position as
`(anchor - camOrigin) + offset`. Because the subsystem lives in `NeuronClient` and can't reach
the game-side `CameraRig`, the game hands it the current origin each frame via
`EffectsInstance().SetOrigin(camera_rig_origin())`; `Advance`/`BuildVertices` use that stored
origin. This keeps precision near the player and lets a blast stay put in the world while the
camera moves — the donor never needed this (single small world), so it is **new logic** we add.

---

## 7. The equivalent of the donor call site

The donor's `Building::Destroy` becomes the body of the existing bus handlers in
`DeepspaceOutpost/main.cpp` — no new call site, we fill in the two that already fire:

```cpp
// EntityDeath handler (was spawn_replicated_explosion)
auto& fx = Neuron::Client::EffectsInstance();
fx.AddExplosion(snap.type, {snap.x,snap.y,snap.z}, snap_rotmat, 1.0f);   // pulls MeshData via provider
for (int i = 0; i < sparkCount(snap); ++i)                    // donor's intensity/4 loop
  fx.CreateParticle({snap.x,snap.y,snap.z},
      randomOutwardVel(100.0f), Particle::TypeExplosionCore, 100.0f);
snd_play_sample(SND_EXPLODE);                                 // was TriggerBuildingEvent(...)
// (optional) publish a camera-shake reaction on g_clientBus — the Bang() analogue
```

`EffectsInstance().SetOrigin(camera_rig_origin())` is called once per frame from
`game_update_flight()`; `Advance` is engine-driven (§4).

`ExplosionAt` (player-kill broadcast) does the same at `{_boom.x,_boom.y,_boom.z}` using a Viper
hull for the debris mesh (matching today's `spawn_explosion_at`), scaled by `_boom.scale`.

---

## 8. Implementation phases

1. **Renderer skeleton** — add `NeuronClient/graphics/SceneParticles.{h,cpp}` (blend/depth
   states, `SetDebris`/`SetParticles`, `Render`), the `sceneparticleVS/PS.hlsl` shaders, and the
   `ParticleVertex` input layout. Wire `Render()` into `gfx_render_3d_scene()`. Add the sources to
   `NeuronClient/CMakeLists.txt` (shaders auto-glob via the existing fxc step). Prove it by
   drawing one hard-coded additive quad at the origin.
2. **Particle system + subsystem** — port `ParticleType`/`Particle`/`ParticleSystem`
   (space-adapted subset), the `frand01`/`sfrand` helpers, and add the `Neuron::Client::Effects`
   subsystem + `EffectsInstance()` accessor holding it and the renderer, with `SetOrigin`,
   `CreateParticle`, `Advance` (builds + pushes the particle batch). Drive `Advance` from
   `ClientEngine::Frame`/`game_update_flight`. Test with a manual burst on a keypress.
3. **Mesh explosion** — port `Tumbler`/`ExplodingTri`/`Explosion`/`ExplosionManager` into the
   subsystem, sourcing triangles from the registered `Scene3D` mesh provider; build + push the
   debris batch.
4. **Trigger integration + retire legacy** — re-point the `EntityDeath` / `ExplosionAt` bus
   handlers in `main.cpp` at `EffectsInstance()`; **delete** the legacy `draw_explosion`
   (`threed.cpp`) and `ReplicatedExplosion` / `spawn_replicated_explosion` / `spawn_explosion_at`
   (`space.cpp`) path (decision 1).
5. **Tuning & polish** — colours via the palette, sizes vs. hull `ship_data.size`, particle
   counts vs. `ExplosionAt.scale`, optional `SceneGlow` interplay, optional camera shake.

## 9. Testing

* **Headless unit tests** (GoogleTest, `Tests/NeuronClient/`): the pure simulation is
  device-free — cover `Tumbler::Advance` (rotation stays orthonormal, ang-vel decays),
  `Explosion` construction from a synthetic `MeshData` (one tri in → one `ExplodingTri` out,
  degenerate tris skipped, `fraction` culls the right share with a seeded `set_rand_seed`),
  `Particle::Advance` fade/cull, and the floating-origin rebase math (anchor + offset −
  camOrigin). No D3D needed for any of these.
* **Manual**: build x64 Debug + Release; kill an NPC and a player, confirm debris + fireball at
  the death point, correct occlusion behind ships, and that a blast stays world-anchored as the
  camera orbits.

## 10. Decisions (confirmed)

1. **Replace** the legacy 2D `draw_explosion` / `ReplicatedExplosion` effect — delete the
   pixel-spray path once the 3D version is in (no runtime toggle).
2. **All in `NeuronClient`** — the whole particle/explosion engine (simulation *and* GPU pass) is
   a **game-agnostic engine subsystem** (`Neuron::Client::Effects`, reached via
   `EffectsInstance()`). The game only registers providers (already done), spawns
   (`CreateParticle` / `AddExplosion`), and sets the per-frame origin. No sim code in the exe.
3. **Space-relevant particle subset** — `ExplosionCore`, `ExplosionDebris`, `Spark`,
   `MuzzleFlash`, `Fire`, `MissileTrail`, `MissileFire`; Darwinia-only types dropped.

## 11. File checklist

**New — all engine-side (`NeuronClient`)**
* `NeuronClient/graphics/SceneParticles.h` / `.cpp` — additive billboard + debris D3D11 pass.
* `NeuronClient/shaders/sceneparticleVS.hlsl` / `sceneparticlePS.hlsl` — textured additive quad.
* `NeuronClient/ParticleSystem.h` / `.cpp` — ported particle sim (space-adapted subset).
* `NeuronClient/ExplosionManager.h` / `.cpp` — ported mesh-shatter sim.
* `NeuronClient/Effects.h` / `.cpp` — the `Neuron::Client::Effects` subsystem + `EffectsInstance()`
  accessor owning the two sims and the renderer (`SetOrigin` / `CreateParticle` / `AddExplosion` /
  `Advance`).
* `Tests/NeuronClient/…` — sim unit tests (device-free).

**Edited**
* `NeuronClient/platform/GameScene.cpp` — invoke `SceneParticles::Render` in `gfx_render_3d_scene`.
* `NeuronClient/ClientEngine.cpp` — advance `EffectsInstance()` with the frame step.
* `NeuronClient/CMakeLists.txt` — add the new engine sources (shaders auto-glob).
* `DeepspaceOutpost/main.cpp` — re-point the `EntityDeath` / `ExplosionAt` bus handlers at
  `EffectsInstance()`; call `SetOrigin(camera_rig_origin())` each frame.
* `DeepspaceOutpost/space.cpp` — **delete** `ReplicatedExplosion` / `spawn_replicated_explosion` /
  `spawn_explosion_at` and the `s_explosions` draw block.
* `DeepspaceOutpost/threed.cpp` — **delete** the legacy 2D `draw_explosion` and its
  `draw_ship` call.
