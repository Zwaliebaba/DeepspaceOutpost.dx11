#include <gtest/gtest.h>

#include <cmath>
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

TEST(Mesh, UVSphereHasExpectedGridCountsAndBakedColour)
{
  const int stacks = 8;
  const int slices = 12;
  const MeshData sphere = BuildUVSphere(10.0f, stacks, slices, 0xAABBCCDDu);

  // (stacks+1) rings x (slices+1) vertices (seam longitude duplicated), 2 triangles per quad.
  EXPECT_EQ(sphere.vertices.size(), static_cast<size_t>((stacks + 1) * (slices + 1)));
  EXPECT_EQ(sphere.indices.size(), static_cast<size_t>(stacks * slices * 6));
  for (const MeshVertex& v : sphere.vertices)
    EXPECT_EQ(v.rgba, 0xAABBCCDDu);
}

TEST(Mesh, UVSphereVerticesLieOnRadiusWithUnitNormals)
{
  const float radius = 7.5f;
  const MeshData sphere = BuildUVSphere(radius, 16, 24, 0xFFFFFFFFu);
  ASSERT_FALSE(sphere.vertices.empty());

  for (const MeshVertex& v : sphere.vertices)
  {
    const float r = std::sqrt(v.x * v.x + v.y * v.y + v.z * v.z);
    EXPECT_NEAR(r, radius, 1e-3f);
    const float n = std::sqrt(v.nx * v.nx + v.ny * v.ny + v.nz * v.nz);
    EXPECT_NEAR(n, 1.0f, 1e-4f);
    // The normal is the outward radial direction: position == radius * normal.
    EXPECT_NEAR(v.x, radius * v.nx, 1e-3f);
    EXPECT_NEAR(v.y, radius * v.ny, 1e-3f);
    EXPECT_NEAR(v.z, radius * v.nz, 1e-3f);
  }
}

TEST(Mesh, UVSphereRejectsDegenerateParameters)
{
  EXPECT_TRUE(BuildUVSphere(0.0f, 8, 8, 0xFFFFFFFFu).vertices.empty());
  EXPECT_TRUE(BuildUVSphere(-1.0f, 8, 8, 0xFFFFFFFFu).vertices.empty());
  EXPECT_TRUE(BuildUVSphere(1.0f, 1, 8, 0xFFFFFFFFu).vertices.empty());
  EXPECT_TRUE(BuildUVSphere(1.0f, 8, 2, 0xFFFFFFFFu).vertices.empty());
}
