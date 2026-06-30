#include "pch.h"

#include "SceneMeshes.h"

#include <vector>

#include "Mesh.h"
#include "Scene3D.h"

#include "Renderer.h" // master palette (palette index -> RGBA)
#include "elite.h"    // ship_list
#include "shipdata.h"
#include "shipface.h"

// Build a GPU-ready mesh for one legacy ship type from its point table + solid faces.
// Returns false if the type is not a real ship (planet/sun and out-of-range types have
// no solid mesh - they are drawn by other paths). Pure data: Scene3D caches the result
// as immutable vertex/index buffers on first use.
static bool build_ship_mesh(int _type, Neuron::Graphics::MeshData& _out)
{
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
