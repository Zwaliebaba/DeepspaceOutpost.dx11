#include "pch.h"

#include "SceneMeshes.h"

#include <vector>

#include "Mesh.h"
#include "Scene3D.h"

#include "Renderer.h" // master palette (palette index -> RGBA)
#include "GamePalette.h"      // GFX_COL_* palette indices
#include "elite.h"    // ship_list
#include "shipdata.h"
#include "shipface.h"

// World-space radius of the 3D planet sphere. Sized to the planet's PHYSICAL extent - the
// server's PLANET_KILL_RADIUS of 4000 (GameLogic/CollisionSystem.h), i.e. the visible
// surface is the deadly surface. The old value (24576, inherited from the retired billboard's
// artificial on-screen size) was 3x the station's 8000-unit orbit, so the planet sphere
// ENCLOSED the station and any ship near it - the camera sat inside it and the whole view
// flooded green on launch. At 4000 the planet is a sphere sitting below the station and the
// space around the station reads as open space again.
static constexpr float PLANET_RADIUS = 4000.0f;

// Build a GPU-ready mesh for one legacy ship type from its point table + solid faces.
// Returns false if the type is not a real ship (planet/sun and out-of-range types have
// no solid mesh - they are drawn by other paths). Pure data: Scene3D caches the result
// as immutable vertex/index buffers on first use.
static bool build_ship_mesh(int _type, Neuron::Graphics::MeshData& _out)
{
  // The planet is a procedural 3D sphere (migrated from the old camera-facing billboard):
  // a smooth UV-sphere lit per-vertex by the scene's directional light, in the classic
  // green. The mesh is cached as immutable vertex/index buffers on first use.
  if (_type == SHIP_PLANET)
  {
    Renderer* r = platform_renderer();
    const uint32_t rgba = (r ? r->paletteColour(GFX_COL_GREEN_1) : 0xFF33AA33u) | 0xFF000000u;
    _out = Neuron::Graphics::BuildUVSphere(PLANET_RADIUS, 32, 48, rgba);
    return !_out.vertices.empty() && !_out.indices.empty();
  }

  if (_type < 1 || _type > NO_OF_SHIPS)
    return false;

  const struct ship_data* ship = ship_list[_type];
  if (ship == nullptr || ship->num_points <= 0)
    return false;

  const struct ship_solid& solid = ship_solids[_type];
  if (solid.face_data == nullptr || solid.num_faces <= 0)
    return false;

  // Resolve palette indices through the master palette (opaque), matching col_rgba.
  Renderer* r = platform_renderer();

  std::vector<Neuron::Graphics::MeshPoint> points;
  points.reserve(static_cast<size_t>(ship->num_points));
  for (int i = 0; i < ship->num_points; i++)
    points.push_back({static_cast<float>(ship->points[i].x), static_cast<float>(ship->points[i].y),
                      static_cast<float>(ship->points[i].z)});

  std::vector<Neuron::Graphics::MeshFace> faces;
  faces.reserve(static_cast<size_t>(solid.num_faces));
  for (int i = 0; i < solid.num_faces; i++)
  {
    const struct ship_face& f = solid.face_data[i];

    Neuron::Graphics::MeshFace mf{};
    mf.rgba = (r ? r->paletteColour(f.colour) : 0xFFFFFFFFu) | 0xFF000000u;
    mf.nx = static_cast<float>(f.norm_x);
    mf.ny = static_cast<float>(f.norm_y);
    mf.nz = static_cast<float>(f.norm_z);
    mf.count = f.points;
    mf.idx[0] = f.p1;
    mf.idx[1] = f.p2;
    mf.idx[2] = f.p3;
    mf.idx[3] = f.p4;
    mf.idx[4] = f.p5;
    mf.idx[5] = f.p6;
    mf.idx[6] = f.p7;
    mf.idx[7] = f.p8;
    faces.push_back(mf);
  }

  _out = Neuron::Graphics::BuildSolidMesh(points.data(), static_cast<int>(points.size()), faces.data(),
                                          static_cast<int>(faces.size()));
  return !_out.vertices.empty() && !_out.indices.empty();
}

void register_scene_meshes(void)
{
  Neuron::Graphics::Scene3D::SetMeshProvider(&build_ship_mesh);
}
