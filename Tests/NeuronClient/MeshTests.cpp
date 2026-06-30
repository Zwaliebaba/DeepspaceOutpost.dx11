#include <gtest/gtest.h>

#include <vector>

#include "Mesh.h"

using namespace Neuron::Graphics;

namespace
{
  // A unit quad in the z=0 plane, fan order (0,1,2,3).
  const MeshPoint kQuadPoints[4] = {
      {0.0f, 0.0f, 0.0f}, {1.0f, 0.0f, 0.0f}, {1.0f, 1.0f, 0.0f}, {0.0f, 1.0f, 0.0f}};

  MeshFace MakeFace(uint32_t _rgba, int _count, int _i0, int _i1, int _i2, int _i3)
  {
    MeshFace f{};
    f.rgba = _rgba;
    f.nx = 0.0f;
    f.ny = 0.0f;
    f.nz = 1.0f;
    f.count = _count;
    f.idx[0] = _i0;
    f.idx[1] = _i1;
    f.idx[2] = _i2;
    f.idx[3] = _i3;
    return f;
  }
}

TEST(Mesh, QuadFanProducesTwoTrianglesInGfxPolygonOrder)
{
  const MeshFace face = MakeFace(0x11223344u, 4, 0, 1, 2, 3);
  const MeshData mesh = BuildSolidMesh(kQuadPoints, 4, &face, 1);

  // One set of vertices per face (no sharing), two triangles.
  ASSERT_EQ(mesh.vertices.size(), 4u);
  const std::vector<uint32_t> expected = {0, 1, 2, 0, 2, 3};
  EXPECT_EQ(mesh.indices, expected);

  // Positions copied from the point table, in face order.
  for (int k = 0; k < 4; ++k)
  {
    EXPECT_FLOAT_EQ(mesh.vertices[static_cast<size_t>(k)].x, kQuadPoints[k].x);
    EXPECT_FLOAT_EQ(mesh.vertices[static_cast<size_t>(k)].y, kQuadPoints[k].y);
    EXPECT_FLOAT_EQ(mesh.vertices[static_cast<size_t>(k)].z, kQuadPoints[k].z);
  }
}

TEST(Mesh, FlatShadingGivesEveryVertexTheFaceColourAndNormal)
{
  const MeshFace face = MakeFace(0xAABBCCDDu, 4, 0, 1, 2, 3);
  const MeshData mesh = BuildSolidMesh(kQuadPoints, 4, &face, 1);

  ASSERT_EQ(mesh.vertices.size(), 4u);
  for (const MeshVertex& v : mesh.vertices)
  {
    EXPECT_EQ(v.rgba, 0xAABBCCDDu);
    EXPECT_FLOAT_EQ(v.nx, 0.0f);
    EXPECT_FLOAT_EQ(v.ny, 0.0f);
    EXPECT_FLOAT_EQ(v.nz, 1.0f);
  }
}

TEST(Mesh, TriangleFaceProducesOneTriangle)
{
  const MeshFace face = MakeFace(0x010203FFu, 3, 0, 1, 2, 0);
  const MeshData mesh = BuildSolidMesh(kQuadPoints, 4, &face, 1);

  ASSERT_EQ(mesh.vertices.size(), 3u);
  const std::vector<uint32_t> expected = {0, 1, 2};
  EXPECT_EQ(mesh.indices, expected);
}

TEST(Mesh, LineOrDegenerateFacesAreSkipped)
{
  const MeshFace line = MakeFace(0xFFFFFFFFu, 2, 0, 1, 0, 0);
  const MeshData mesh = BuildSolidMesh(kQuadPoints, 4, &line, 1);

  EXPECT_TRUE(mesh.vertices.empty());
  EXPECT_TRUE(mesh.indices.empty());
}

TEST(Mesh, MultipleFacesOffsetIndicesByPriorVertexCount)
{
  const MeshFace faces[2] = {MakeFace(0x11111111u, 4, 0, 1, 2, 3), MakeFace(0x22222222u, 4, 0, 1, 2, 3)};
  const MeshData mesh = BuildSolidMesh(kQuadPoints, 4, faces, 2);

  ASSERT_EQ(mesh.vertices.size(), 8u);
  // Second face's indices are based at 4, not 0.
  const std::vector<uint32_t> expected = {0, 1, 2, 0, 2, 3, 4, 5, 6, 4, 6, 7};
  EXPECT_EQ(mesh.indices, expected);

  // Each face kept its own colour (flat per face).
  EXPECT_EQ(mesh.vertices[0].rgba, 0x11111111u);
  EXPECT_EQ(mesh.vertices[4].rgba, 0x22222222u);
}

TEST(Mesh, FaceWithOutOfRangeIndexIsSkipped)
{
  const MeshFace bad = MakeFace(0x12345678u, 4, 0, 1, 2, 99); // 99 >= numPoints
  const MeshData mesh = BuildSolidMesh(kQuadPoints, 4, &bad, 1);

  EXPECT_TRUE(mesh.vertices.empty());
  EXPECT_TRUE(mesh.indices.empty());
}

TEST(Mesh, NullOrEmptyInputsReturnEmptyMesh)
{
  const MeshFace face = MakeFace(0x11223344u, 4, 0, 1, 2, 3);

  EXPECT_TRUE(BuildSolidMesh(nullptr, 4, &face, 1).vertices.empty());
  EXPECT_TRUE(BuildSolidMesh(kQuadPoints, 4, nullptr, 1).vertices.empty());
  EXPECT_TRUE(BuildSolidMesh(kQuadPoints, 0, &face, 1).vertices.empty());
  EXPECT_TRUE(BuildSolidMesh(kQuadPoints, 4, &face, 0).vertices.empty());
}
