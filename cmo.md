# Modified CMO ("DSOM") — a single-file model format for DeepspaceOutpost

## Why touch CMO at all

Each ship under `GameData/Models` is currently **two files**:

- `*.obj` — a Wavefront OBJ with a position table (`v x y z`) and coloured
  polygon faces (`usemtl col_N` + `f a b c …`, 2..8 vertices per face,
  fan-ordered), plus a shared `elite.mtl` palette-colour material library.
- `*.json` — a sidecar with the non-geometry gameplay stats (`ship_id`,
  `size`, `energy`, `bounty`, `laser_strength`, the derived
  `num_points`/`num_lines`/`num_faces`, …).

Microsoft's **CMO** (Compiled Mesh Object, from DirectXTK) is the natural
single-file, load-and-upload binary target — but stock CMO is built for a
*different* engine: `wchar_t` (UTF-16) strings, an 8-texture + pixel-shader
material block, a fat `pos/normal/tangent/color/uv` vertex, and skeletal
animation. Our ships have **none** of that. They are flat-shaded,
untextured, palette-coloured, static polyhedra, and the runtime vertex is
already fixed (`Neuron::Graphics::MeshVertex`: `pos3 + normal3 + rgba8`).

This document proposes **DSOM** ("Deepspace Outpost Model") — a *modified*
CMO that keeps CMO's container shape (mesh → materials → submeshes → IB →
VB → extents) but swaps the payload structs for what this engine actually
renders, and folds the JSON stats into the same file.

The design goals, in order:

1. **One file per ship** — geometry + gameplay stats together.
2. **Load = read + upload** — the vertex/index blocks map 1:1 onto
   `MeshVertex` / 32-bit indices, no CPU rework at load time.
3. **Round-trippable** — keep the palette-index material names (`col_N`)
   and the original point table + line list so a DSOM can be exported back
   to OBJ + JSON without loss.
4. **Portable** — UTF-8 strings and fixed little-endian widths (the sim is
   unit-tested headlessly on Linux CI; UTF-16 `wchar_t` is a Windows-ism we
   drop deliberately).

Nothing in the live engine changes to *read* this today; it is a proposal
for the on-disk format plus the shape of the loader/exporter.

---

## Conventions

- **Endianness:** little-endian for every scalar.
- **`UINT`:** unsigned 32-bit. **`INT`:** signed 32-bit. **`float`:** IEEE-754
  32-bit. **`BYTE`:** unsigned 8-bit.
- **Strings** (`STR`): `UINT` byte-length `n`, then `n` bytes of **UTF-8**
  (no terminator, no BOM). `n == 0` means empty. This replaces stock CMO's
  `UINT length + wchar_t[]` UTF-16 strings.
- **`RGBA`:** `UINT` packed `0xAABBGGRR` (R in the low byte) — identical to
  `MeshVertex::rgba`, Render2D, and `col_rgba`. No conversion at load.
- **`Vec3`:** three `float` (x, y, z).
- All multi-byte blocks are tightly packed; no implicit padding is written
  between fields. Vertex/index arrays are naturally aligned by construction.

---

## Top-level layout

```
DSOM file
├── Header
├── ShipStats chunk        (the JSON sidecar, promoted into the file)
└── UINT  meshCount
    └── Mesh[meshCount]     (in practice always 1 for a ship)
```

Stock CMO starts *directly* with `UINT meshCount`. DSOM prepends a **Header**
(so a loader can identify/version the file and reject stock CMO) and a
**ShipStats** chunk (so the `.json` disappears), then continues with the
familiar `meshCount` + `Mesh[]` body.

### Header

| Field       | Type      | Notes                                                        |
|-------------|-----------|--------------------------------------------------------------|
| `magic`     | `BYTE[4]` | `'D','S','O','M'` (0x4D4F5344 LE). Absent in stock CMO.       |
| `version`   | `UINT`    | Format version. `1` for this proposal.                       |
| `flags`     | `UINT`    | Bit 0 `HAS_LINES`, bit 1 `HAS_STATS`, bit 2 `HAS_SKELETON`. Others reserved 0. |

`HAS_SKELETON` is defined for forward-compat only; the current exporter
never sets it and the skeletal section below is omitted entirely when it is
clear (mirroring stock CMO's "if no skeleton, file ends early" rule).

### ShipStats chunk *(present iff `HAS_STATS`)*

The `*.json` fields, verbatim, as fixed-width binary. Strings are `STR`.

| Field            | Type   | JSON key         |
|------------------|--------|------------------|
| `name`           | `STR`  | `name`           |
| `shipId`         | `INT`  | `ship_id`        |
| `numPoints`      | `UINT` | `num_points`     |
| `numLines`       | `UINT` | `num_lines`      |
| `numFaces`       | `UINT` | `num_faces`      |
| `maxLoot`        | `INT`  | `max_loot`       |
| `scoopType`      | `INT`  | `scoop_type`     |
| `size`           | `float`| `size`           |
| `frontLaser`     | `INT`  | `front_laser`    |
| `bounty`         | `INT`  | `bounty`         |
| `vanishPoint`    | `INT`  | `vanish_point`   |
| `energy`         | `INT`  | `energy`         |
| `velocity`       | `INT`  | `velocity`       |
| `missiles`       | `INT`  | `missiles`       |
| `laserStrength`  | `INT`  | `laser_strength` |

`model` (the `.obj` filename) is intentionally dropped — the geometry now
lives in this same file. `num_points/num_lines/num_faces` are kept because
the game reads them as gameplay/scanner values, not just as geometry counts;
they are validated against the actual geometry at load.

> **Extensibility:** to add a stat later, bump `version` and append at the
> end of this chunk. Because `meshCount` follows a fixed-size stats block,
> readers of an older version stop at the known field count. (If we expect
> frequent additions, an alternative is to length-prefix the chunk with a
> `UINT byteSize` so unknown trailing bytes can be skipped — recommended if
> stats churn.)

---

## Mesh

One `Mesh` per entry. Structure mirrors stock CMO with three payload
substitutions (**Material**, **Vertex**, and an added **line index** block)
and the skeletal tail gated behind `HAS_SKELETON`.

```
Mesh
├── STR   name
├── UINT  materialCount
│   └── Material[materialCount]          (MODIFIED: palette material, no textures/shaders)
├── BYTE  hasSkeleton                     (kept for CMO parity; equals flags.HAS_SKELETON)
├── UINT  subMeshCount
│   └── SubMesh[subMeshCount]            (MODIFIED: slimmer struct)
├── UINT  ibCount
│   └── { UINT indexCount; UINT32 indices[indexCount] }   (MODIFIED: 32-bit, was USHORT)
├── UINT  vbCount
│   └── { UINT vertexCount; Vertex[vertexCount] }         (MODIFIED: DSOM vertex)
│
├── (REMOVED) skinning VB count + skinning verts          (no skinning)
│
├── UINT  pointCount                      (NEW: original OBJ position table)
│   └── Vec3 points[pointCount]
├── UINT  lineCount                        (NEW, iff HAS_LINES: wireframe edges)
│   └── { UINT16 a; UINT16 b }[lineCount]  (indices into points[])
│
├── MeshExtents                            (kept as-is)
└── [Skeleton section]                     (iff HAS_SKELETON — see below; omitted otherwise)
```

### Material *(modified)*

Stock CMO's material is `Ambient/Diffuse/Specular/Power/Emissive/UVTransform`
+ a pixel-shader name + **8** texture-name slots. Our ships are flat
palette colours with no textures and no per-material shader, so DSOM's
material collapses to:

| Field          | Type   | Notes                                                       |
|----------------|--------|-------------------------------------------------------------|
| `name`         | `STR`  | The OBJ material name, e.g. `col_248`.                       |
| `paletteIndex` | `UINT` | The `scanner.bmp` palette entry `N` from `col_N` (round-trip). |
| `diffuse`      | `RGBA` | Resolved colour (`col_rgba` of the palette entry).          |

No `Ambient/Specular/Emissive`, no `UVTransform`, no shader name, no texture
slots. `diffuse` already equals the per-vertex `rgba` the builder bakes in
(see below), so the material table is really a **name/palette registry for
round-tripping and tooling**, not something the renderer samples.

> A future textured/lit material can be introduced under a new `version`
> without disturbing this one.

### SubMesh *(modified)*

Stock CMO SubMesh is `MaterialIndex, IndexBufferIndex, VertexBufferIndex,
StartIndex, PrimCount`. DSOM keeps the same five fields (a single mesh
generally has one VB/IB, so `IndexBufferIndex`/`VertexBufferIndex` are
usually 0), all `UINT`:

| Field           | Type   |
|-----------------|--------|
| `materialIndex` | `UINT` |
| `ibIndex`       | `UINT` |
| `vbIndex`       | `UINT` |
| `startIndex`    | `UINT` |
| `primCount`     | `UINT` | triangle count |

Because each face already carries its own vertices (colour/normal are
per-face — vertices are never shared across faces), the simplest valid
encoding is **one submesh spanning the whole IB** with the material set to a
sentinel/first entry; per-face colour lives in the vertices, not in submesh
material splits. Exporters MAY instead emit one submesh per material run if
a future renderer wants to bind materials — the format supports both.

### Index buffer *(modified: 32-bit)*

Stock CMO indices are `USHORT` (16-bit). DSOM uses **`UINT32`** to match the
runtime (`MeshData::indices` is `std::vector<uint32_t>`) and to lift the
65 535-vertex ceiling. Current ships are tiny (≤ ~18 points), but the
runtime already committed to 32-bit indices, so the file matches it and
avoids a widening copy at load.

Indices are the **fan-triangulation** of the solid faces, produced exactly
like `BuildSolidMesh`: face `(v0,v1,…,vN-1)` → triangles `(v0, vi, vi+1)`
for `i` in `1..N-2`, winding preserved. They index into the DSOM **Vertex**
buffer below.

### Vertex *(modified: the engine vertex)*

Stock CMO's vertex is `Position, Normal, Tangent, UINT color, UV`. DSOM's is
byte-for-byte `Neuron::Graphics::MeshVertex`:

| Field       | Type   | Bytes | Notes                                  |
|-------------|--------|-------|----------------------------------------|
| `position`  | `Vec3` | 12    | model-space, from the OBJ point table  |
| `normal`    | `Vec3` | 12    | model-space per-face normal            |
| `rgba`      | `RGBA` | 4     | resolved palette colour (per-face)     |

**28 bytes/vertex.** No tangent, no UV. Vertices are **pre-expanded
per-face** (one set of positions per face) so the flat per-face colour and
normal survive — this is the same layout `BuildSolidMesh` produces, so the
VB block can be `memcpy`'d straight into the GPU vertex buffer.

> **Normals** are not in the OBJ today (`v` lines only). The exporter
> computes each face's model-space normal (e.g. Newell's method over the
> face's points) — the same normal the runtime associates with the face —
> and writes it here so load requires no geometry processing.

### Original points + line list *(new)*

To stay round-trippable to OBJ/JSON and to feed **wireframe** ship rendering
(the legacy `num_lines` / scanner mode), DSOM also stores the *un-expanded*
data the triangle VB throws away:

- `points[]` — the raw OBJ `v x y z` table (`Vec3`), `pointCount == numPoints`.
- `lines[]` *(iff `HAS_LINES`)* — `lineCount` edge pairs, each two `UINT16`
  indices into `points[]`. These are the wireframe edges (the legacy
  "line faces" with < 3 points, plus the polygon edges the wireframe
  renderer draws). `lineCount == numLines`.

A pure solid-only build can clear `HAS_LINES` and omit the line block
entirely.

### MeshExtents *(kept)*

Unchanged from stock CMO — a bounding sphere + AABB, useful for cull/scanner
range:

| Field       | Type   |
|-------------|--------|
| `center`    | `Vec3` |
| `radius`    | `float`|
| `min`       | `Vec3` |
| `max`       | `Vec3` |

Computed over `points[]` at export time.

---

## Skeleton section *(present iff `HAS_SKELETON`; omitted for all current ships)*

Kept identical in spirit to stock CMO so the door stays open, but with DSOM
`STR` strings. Emitted only when `flags.HAS_SKELETON` is set; otherwise the
file ends after `MeshExtents` (exactly the stock-CMO "no skeleton → file
ends here" behaviour).

```
UINT boneCount
  { STR boneName; Bone bone }[boneCount]           // Bone = INT parent + 3x XMFLOAT4X4 (inv-bind, bind, local)
UINT animClipCount
  { STR clipName; float startTime; float endTime;
    UINT keyframeCount; Keyframe keyframe[keyframeCount] }[animClipCount]   // Keyframe = UINT bone + float time + XMFLOAT4X4 transform
```

No current ship uses this; it is documented so a future animated model can
set the flag without a new `version`.

---

## Side-by-side: stock CMO vs DSOM

| Aspect            | Stock CMO                              | DSOM                                        |
|-------------------|----------------------------------------|---------------------------------------------|
| File signature    | none (starts at `UINT meshCount`)      | `'DSOM'` magic + `version` + `flags`        |
| Strings           | `UINT` + `wchar_t[]` (UTF-16)          | `UINT` + UTF-8 bytes                         |
| Gameplay stats    | —                                       | ShipStats chunk (the old `.json`)           |
| Material          | ADSEmissive + UVXform + shader + 8 tex | `name` + `paletteIndex` + `diffuse RGBA`    |
| Vertex            | pos+nrm+tangent+color+uv               | pos + nrm + rgba (`MeshVertex`, 28 B)       |
| Index width       | `USHORT` (16-bit)                      | `UINT32`                                     |
| Skinning VB       | present                                 | removed                                      |
| Point table       | —                                       | raw OBJ points (round-trip + extents)       |
| Line list         | —                                       | wireframe edges (`num_lines`)               |
| Skeleton          | trailing, gated by a `BYTE`            | trailing, gated by `flags.HAS_SKELETON`     |

---

## Load / export sketch

**Load** (`.dsom` → runtime): read Header, validate `magic`/`version`;
read ShipStats into the ship-stats struct; for the (single) mesh, `memcpy`
the VB block into a `std::vector<MeshVertex>` and the IB block into a
`std::vector<uint32_t>` (both already the runtime layout) → hand to Scene3D
as an immutable mesh. Keep `points[]`/`lines[]` for the wireframe path.
Validate `numPoints/numFaces/numLines` against the geometry.

**Export** (`.obj` + `.json` → `.dsom`): the natural home is the existing
`tools/shipdata2obj` converter (or a sibling `obj2dsom`). It already has the
palette (`scanner.bmp`), the point table, and the faces; it would fan-
triangulate exactly like `BuildSolidMesh`, compute per-face normals and
extents, resolve `col_N` → `RGBA`, and write the blocks above. Because both
the exporter and the runtime share the `BuildSolidMesh` triangulation rule,
the on-disk VB/IB is identical to what the engine builds today — the format
just moves that build to bake time.

---

## Open questions

1. **Submesh granularity** — one whole-mesh submesh (colour is per-vertex
   anyway) vs one-per-material-run. Proposal: one whole-mesh submesh now;
   the struct supports runs later.
2. **Stats chunk growth** — fixed field list (bump `version` to extend) vs a
   `UINT byteSize`-prefixed, skip-unknown chunk. Proposal: fixed for v1;
   switch to length-prefixed if stats start changing often.
3. **Endianness marker** — we assume LE everywhere (the only target). If a
   BE target ever appears, the `magic` byte order already detects it.
4. **File extension** — `.dsom` proposed; `.cmo` would be misleading since a
   stock DirectXTK loader cannot read it (different vertex/material/strings).
