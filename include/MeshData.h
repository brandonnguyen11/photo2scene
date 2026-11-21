#pragma once
// MeshData.h — CPU-side triangle mesh (no OpenGL), shared by the
// reconstruction module (Member 4) and the renderer (Member 1).

#include <glm/glm.hpp>

#include <cstdint>
#include <vector>

struct Vertex {
    glm::vec3 position{0.0f};
    glm::vec3 normal{0.0f, 0.0f, 1.0f};
    glm::vec2 uv{0.0f};
};

struct MeshData {
    std::vector<Vertex> vertices;
    std::vector<uint32_t> indices;  // triangle list
    glm::vec3 boundsMin{0.0f};
    glm::vec3 boundsMax{0.0f};

    bool empty() const { return vertices.empty() || indices.empty(); }
    size_t triangleCount() const { return indices.size() / 3; }

    void computeBounds();
    // Area-weighted smooth vertex normals from the triangle list.
    void computeNormals();
};

// Unit ground plane on y = 0 spanning x,z in [-1, 1], normal +Y.
MeshData makePlaneMesh();
// Axis-aligned cube centered at the origin with flat normals.
MeshData makeCubeMesh(float halfSize);
// Box from min to max corner (axis-aligned).
MeshData makeBoxMesh(const glm::vec3& minCorner, const glm::vec3& maxCorner);
// Vertical cylinder (Y-up), centered on origin.
MeshData makeCylinderMesh(float radius, float height, int segments = 16);
// Cone pointing +Y, base on XZ at y=0, tip at y=height.
MeshData makeConeMesh(float radius, float height, int segments = 16);
// UV sphere centered at origin.
MeshData makeSphereMesh(float radius, int slices = 16, int stacks = 12);
// Single grass blade in XY plane (thin strip), base at origin.
MeshData makeGrassBladeMesh();
// Flat water grid on XZ, centered at origin.
MeshData makeGridMesh(float size, int resolution);
// Irregular boulder (noisy sphere).
MeshData makeRockMesh(float radius, int slices = 14, int stacks = 10, float seed = 1.0f);
// Tapered trunk from baseRadius to topRadius.
MeshData makeTaperedTrunk(float baseRadius, float topRadius, float height, int segments = 10);
// Broad leaf / palm frond as a bent strip.
MeshData makeFrondMesh(float length, float width);
