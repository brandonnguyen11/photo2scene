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
