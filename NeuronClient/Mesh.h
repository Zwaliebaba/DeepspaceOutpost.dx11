#pragma once

// Mesh - CPU-side triangle mesh data plus a builder that turns the legacy solid
// model tables into GPU-ready geometry.
//
// The legacy ships are stored as a point table plus a list of solid faces (2..8
// vertices each, fan-ordered), which the retired CPU renderer fan-triangulated and
// depth-sorted as flat 2D polygons. The GPU migration (see 3Dmigration.md) uploads
// each ship type once into an immutable vertex/index buffer. This header is the
// pure-data builder for that geometry: it fan-triangulates the faces the same way,
// with one set of vertices per face so the flat per-face colour and normal are
// preserved (vertices are never shared across faces).
//
// Deliberately free of D3D and of the game's ship_data/ship_face headers (which
// live in the DeepspaceOutpost exe, above this layer): the builder takes neutral
// POD inputs, so it is unit-tested headlessly and the client adapts the real
// tables (resolving palette colours via col_rgba, etc.) when it registers meshes.

#include <cmath>
#include <cstdint>
#include <vector>

namespace Neuron::Graphics
{
  // One interleaved GPU vertex: model-space position, model-space normal (for
  // flat/lit shading), and an RGBA8 colour (0xAABBGGRR, R in the low byte - the
  // same order Render2D and the palette use).
  struct MeshVertex
  {
    float x, y, z;
    float nx, ny, nz;
    uint32_t rgba;
  };

  // A triangle mesh: an interleaved vertex list and a 32-bit index list.
  struct MeshData
  {
    std::vector<MeshVertex> vertices;
    std::vector<uint32_t> indices;
  };

  // A model-space point in the source table.
  struct MeshPoint
  {
    float x, y, z;
  };

  // A solid face to triangulate: a resolved RGBA colour, a model-space normal, and
  // 2..8 point indices in the legacy fan order (p1..pN). Faces with fewer than 3
  // points (the legacy line "faces") or more than 8 are skipped by the triangle
  // builder - they are not solid polygons.
  struct MeshFace
  {
    uint32_t rgba;
    float nx, ny, nz;
    int count;
    int idx[8];
  };

  // Fan-triangulate solid faces into a flat-shaded triangle mesh, exactly as the
  // legacy CPU fan did: a face (p0, p1, ..., pN-1) becomes triangles
  // (p0, pi, pi+1) for i in 1..N-2. Each face contributes its own vertices, so the
  // per-face colour and normal are preserved. A face with an out-of-range point
  // index, fewer than 3 points, or more than 8 points is skipped (it is not a
  // solid polygon). The winding order matches the legacy emission order; the
  // backface-cull direction is chosen later in Scene3D's rasterizer state.
  [[nodiscard]] inline MeshData BuildSolidMesh(const MeshPoint* _points, int _numPoints, const MeshFace* _faces,
                                               int _numFaces)
  {
    MeshData out;
    if (_points == nullptr || _faces == nullptr || _numPoints <= 0 || _numFaces <= 0)
      return out;

    for (int f = 0; f < _numFaces; ++f)
    {
      const MeshFace& face = _faces[f];
      if (face.count < 3 || face.count > 8)
        continue; // line/degenerate or malformed face: not a solid polygon

      bool valid = true;
      for (int k = 0; k < face.count; ++k)
      {
        if (face.idx[k] < 0 || face.idx[k] >= _numPoints)
        {
          valid = false;
          break;
        }
      }
      if (!valid)
        continue;

      const uint32_t base = static_cast<uint32_t>(out.vertices.size());
      for (int k = 0; k < face.count; ++k)
      {
        const MeshPoint& p = _points[face.idx[k]];
        out.vertices.push_back(MeshVertex{p.x, p.y, p.z, face.nx, face.ny, face.nz, face.rgba});
      }

      for (int i = 1; i + 1 < face.count; ++i)
      {
        out.indices.push_back(base);
        out.indices.push_back(base + static_cast<uint32_t>(i));
        out.indices.push_back(base + static_cast<uint32_t>(i + 1));
      }
    }

    return out;
  }

  // Build a smooth UV sphere of the given model-space radius as a lit, single-colour mesh
  // (used for the 3D planet). Each vertex's normal is its outward unit direction, so the
  // per-vertex directional lighting in the scene shader gives a smooth day/night terminator.
  // _stacks is the latitude band count (pole to pole), _slices the longitude count; the seam
  // longitude is duplicated so the grid is regular. Winding is CCW when viewed from outside,
  // but Scene3D rasterizes cull-none, so the z-buffer resolves visibility either way.
  [[nodiscard]] inline MeshData BuildUVSphere(float _radius, int _stacks, int _slices, uint32_t _rgba)
  {
    MeshData out;
    if (_radius <= 0.0f || _stacks < 2 || _slices < 3)
      return out;

    constexpr float PI = 3.14159265358979323846f;

    // (_stacks+1) rings x (_slices+1) vertices. phi runs 0..pi (top pole to bottom pole),
    // theta runs 0..2pi around the axis.
    for (int i = 0; i <= _stacks; ++i)
    {
      const float phi = PI * static_cast<float>(i) / static_cast<float>(_stacks);
      const float sp = std::sin(phi);
      const float cp = std::cos(phi);
      for (int j = 0; j <= _slices; ++j)
      {
        const float theta = 2.0f * PI * static_cast<float>(j) / static_cast<float>(_slices);
        const float nx = sp * std::cos(theta);
        const float ny = cp;
        const float nz = sp * std::sin(theta);
        out.vertices.push_back(MeshVertex{nx * _radius, ny * _radius, nz * _radius, nx, ny, nz, _rgba});
      }
    }

    // Two triangles per grid quad.
    const int ring = _slices + 1;
    for (int i = 0; i < _stacks; ++i)
    {
      for (int j = 0; j < _slices; ++j)
      {
        const uint32_t a = static_cast<uint32_t>(i * ring + j);
        const uint32_t b = static_cast<uint32_t>((i + 1) * ring + j);
        const uint32_t c = static_cast<uint32_t>((i + 1) * ring + j + 1);
        const uint32_t d = static_cast<uint32_t>(i * ring + j + 1);
        out.indices.push_back(a);
        out.indices.push_back(b);
        out.indices.push_back(c);
        out.indices.push_back(a);
        out.indices.push_back(c);
        out.indices.push_back(d);
      }
    }

    return out;
  }
}
