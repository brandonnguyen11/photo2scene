#include "MeshData.h"

#include <glm/geometric.hpp>

#include <limits>

void MeshData::computeBounds() {
    if (vertices.empty()) {
        boundsMin = boundsMax = glm::vec3(0.0f);
        return;
    }
    boundsMin = glm::vec3(std::numeric_limits<float>::max());
    boundsMax = glm::vec3(std::numeric_limits<float>::lowest());
    for (const Vertex& v : vertices) {
        boundsMin = glm::min(boundsMin, v.position);
        boundsMax = glm::max(boundsMax, v.position);
    }
}

void MeshData::computeNormals() {
    std::vector<glm::vec3> accum(vertices.size(), glm::vec3(0.0f));
    for (size_t t = 0; t + 2 < indices.size(); t += 3) {
        const uint32_t a = indices[t], b = indices[t + 1], c = indices[t + 2];
        const glm::vec3& pa = vertices[a].position;
        const glm::vec3& pb = vertices[b].position;
        const glm::vec3& pc = vertices[c].position;
        // Un-normalized cross product = normal weighted by 2x triangle area.
        const glm::vec3 n = glm::cross(pb - pa, pc - pa);
        accum[a] += n;
        accum[b] += n;
        accum[c] += n;
    }
    for (size_t i = 0; i < vertices.size(); ++i) {
        const float len = glm::length(accum[i]);
        vertices[i].normal = len > 1e-12f ? accum[i] / len : glm::vec3(0.0f, 0.0f, 1.0f);
    }
}

MeshData makePlaneMesh() {
    MeshData m;
    const glm::vec3 n(0.0f, 1.0f, 0.0f);
    m.vertices = {
        {{-1.0f, 0.0f, -1.0f}, n, {0.0f, 0.0f}},
        {{-1.0f, 0.0f, 1.0f}, n, {0.0f, 1.0f}},
        {{1.0f, 0.0f, 1.0f}, n, {1.0f, 1.0f}},
        {{1.0f, 0.0f, -1.0f}, n, {1.0f, 0.0f}},
    };
    m.indices = {0, 1, 2, 0, 2, 3};
    m.computeBounds();
    return m;
}

MeshData makeCubeMesh(float h) {
    MeshData m;
    const glm::vec3 normals[6] = {{1, 0, 0}, {-1, 0, 0}, {0, 1, 0}, {0, -1, 0}, {0, 0, 1}, {0, 0, -1}};
    for (const glm::vec3& n : normals) {
        // Build two tangent axes for this face.
        const glm::vec3 u = glm::abs(n.y) > 0.5f ? glm::vec3(1, 0, 0) : glm::vec3(0, 1, 0);
        const glm::vec3 t = glm::cross(n, u);
        const glm::vec3 b = glm::cross(n, t);
        const uint32_t base = static_cast<uint32_t>(m.vertices.size());
        const glm::vec2 corners[4] = {{-1, -1}, {1, -1}, {1, 1}, {-1, 1}};
        for (const glm::vec2& c : corners) {
            m.vertices.push_back({(n + t * c.x + b * c.y) * h, n, (c + 1.0f) * 0.5f});
        }
        // Wind counter-clockwise when viewed from outside.
        const glm::vec3 e1 = m.vertices[base + 1].position - m.vertices[base].position;
        const glm::vec3 e2 = m.vertices[base + 2].position - m.vertices[base].position;
        if (glm::dot(glm::cross(e1, e2), n) > 0.0f) {
            m.indices.insert(m.indices.end(), {base, base + 1, base + 2, base, base + 2, base + 3});
        } else {
            m.indices.insert(m.indices.end(), {base, base + 2, base + 1, base, base + 3, base + 2});
        }
    }
    m.computeBounds();
    return m;
}
