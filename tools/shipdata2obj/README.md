# shipdata2obj

Exports the built-in Elite ship geometry to Wavefront OBJ files plus per-ship
JSON sidecars, written into `GameData/Models/`.

This is the first (reversible) step of moving ship models out of compiled C++
tables. The game is **unchanged** — it still compiles `shipdata.cpp` /
`shipface.cpp`. This tool only generates the asset files so the format can be
validated before a runtime loader is wired up.

## What it produces

For each of the 33 ships:

- `<ship>.obj` — vertices (`v`) from `ship_point`, and coloured polygon faces
  (`f`, 3–8 sided) from `ship_face`. Each face is tagged with `usemtl col_<n>`
  where `<n>` is the original palette index.
- `<ship>.json` — the non-geometry data that has no place in an OBJ:
  gameplay stats (`energy`, `bounty`, `laser_strength`, `size`, …) and the
  geometry-coupled vertex indices `front_laser` / `vanish_point`.

Plus one shared `elite.mtl`, whose materials are the `scanner.bmp` palette
colours actually used by the ships.

### Notes

- File names follow the source variable names, not display names, because the
  display names collide ("Cobra MkIII" and "Python" each appear twice).
- `num_faces` in the JSON is the original `ship_data.num_faces` (the count of
  face *normals* used for visibility). It can be smaller than the number of
  polygons in the OBJ, because the solid model splits some surfaces into more
  render polygons. Both numbers are faithful to the source.
- The wireframe `ship_line` table, the per-vertex face lists, and the face
  normals are intentionally not exported — they are all derivable from the
  vertices + faces at load time.

## Build & run (from repo root)

The data files include `pch.h`, which pulls in the full engine. To build the
converter against just the data, compile copies of the two data files next to
the lightweight stub headers in `stubs/`:

```sh
BUILD=$(mktemp -d)
cp DeepspaceOutpost/shipdata.cpp DeepspaceOutpost/shipface.cpp \
   tools/shipdata2obj/convert.cpp tools/shipdata2obj/stubs/* "$BUILD"/
g++ -std=c++17 -I "$BUILD" -I DeepspaceOutpost \
    "$BUILD/convert.cpp" "$BUILD/shipdata.cpp" "$BUILD/shipface.cpp" \
    -o "$BUILD/convert"
"$BUILD/convert" GameData
```

The converter links directly against the real `ship_data` / `ship_solids`
arrays, so the exported geometry is guaranteed to match what the game compiles.
