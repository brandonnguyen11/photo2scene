#include "MeshData.h"

#include <glm/geometric.hpp>

#include <algorithm>
#include <cmath>
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

MeshData makeBoxMesh(const glm::vec3& minC, const glm::vec3& maxC) {
    MeshData m;
    const glm::vec3 c = (minC + maxC) * 0.5f;
    const glm::vec3 h = (maxC - minC) * 0.5f;
    MeshData unit = makeCubeMesh(1.0f);
    for (Vertex& v : unit.vertices) {
        v.position = c + v.position * h;
    }
    unit.computeBounds();
    return unit;
}

MeshData makeCylinderMesh(float radius, float height, int segments) {
    MeshData m;
    segments = std::max(segments, 6);
    const float half = height * 0.5f;
    // Side
    for (int i = 0; i <= segments; ++i) {
        const float t = static_cast<float>(i) / segments;
        const float a = t * 6.2831853f;
        const float c = std::cos(a), s = std::sin(a);
        const glm::vec3 n(c, 0, s);
        m.vertices.push_back({{c * radius, -half, s * radius}, n, {t, 0}});
        m.vertices.push_back({{c * radius, half, s * radius}, n, {t, 1}});
    }
    for (int i = 0; i < segments; ++i) {
        const uint32_t b = static_cast<uint32_t>(i * 2);
        m.indices.insert(m.indices.end(), {b, b + 1, b + 2, b + 2, b + 1, b + 3});
    }
    // Caps
    const uint32_t botCenter = static_cast<uint32_t>(m.vertices.size());
    m.vertices.push_back({{0, -half, 0}, {0, -1, 0}, {0.5f, 0.5f}});
    const uint32_t topCenter = static_cast<uint32_t>(m.vertices.size());
    m.vertices.push_back({{0, half, 0}, {0, 1, 0}, {0.5f, 0.5f}});
    for (int i = 0; i < segments; ++i) {
        const float a0 = static_cast<float>(i) / segments * 6.2831853f;
        const float a1 = static_cast<float>(i + 1) / segments * 6.2831853f;
        const uint32_t b0 = static_cast<uint32_t>(m.vertices.size());
        m.vertices.push_back({{std::cos(a0) * radius, -half, std::sin(a0) * radius}, {0, -1, 0}, {0, 0}});
        m.vertices.push_back({{std::cos(a1) * radius, -half, std::sin(a1) * radius}, {0, -1, 0}, {1, 0}});
        m.indices.insert(m.indices.end(), {botCenter, b0 + 1, b0});
        const uint32_t t0 = static_cast<uint32_t>(m.vertices.size());
        m.vertices.push_back({{std::cos(a0) * radius, half, std::sin(a0) * radius}, {0, 1, 0}, {0, 1}});
        m.vertices.push_back({{std::cos(a1) * radius, half, std::sin(a1) * radius}, {0, 1, 0}, {1, 1}});
        m.indices.insert(m.indices.end(), {topCenter, t0, t0 + 1});
    }
    m.computeBounds();
    return m;
}

MeshData makeConeMesh(float radius, float height, int segments) {
    MeshData m;
    segments = std::max(segments, 6);
    const uint32_t tip = 0;
    m.vertices.push_back({{0, height, 0}, {0, 1, 0}, {0.5f, 1}});
    for (int i = 0; i <= segments; ++i) {
        const float t = static_cast<float>(i) / segments;
        const float a = t * 6.2831853f;
        const float c = std::cos(a), s = std::sin(a);
        // Approximate smooth side normal.
        const glm::vec3 n = glm::normalize(glm::vec3(c * height, radius, s * height));
        m.vertices.push_back({{c * radius, 0, s * radius}, n, {t, 0}});
    }
    for (int i = 0; i < segments; ++i) {
        m.indices.insert(m.indices.end(),
                         {tip, static_cast<uint32_t>(1 + i), static_cast<uint32_t>(2 + i)});
    }
    const uint32_t baseCenter = static_cast<uint32_t>(m.vertices.size());
    m.vertices.push_back({{0, 0, 0}, {0, -1, 0}, {0.5f, 0.5f}});
    for (int i = 0; i < segments; ++i) {
        m.indices.insert(m.indices.end(), {baseCenter, static_cast<uint32_t>(2 + i),
                                           static_cast<uint32_t>(1 + i)});
    }
    m.computeNormals();
    m.computeBounds();
    return m;
}

MeshData makeSphereMesh(float radius, int slices, int stacks) {
    MeshData m;
    slices = std::max(slices, 6);
    stacks = std::max(stacks, 3);
    for (int y = 0; y <= stacks; ++y) {
        const float v = static_cast<float>(y) / stacks;
        const float phi = v * 3.14159265f;
        for (int x = 0; x <= slices; ++x) {
            const float u = static_cast<float>(x) / slices;
            const float theta = u * 6.2831853f;
            const glm::vec3 n(std::sin(phi) * std::cos(theta), std::cos(phi),
                              std::sin(phi) * std::sin(theta));
            m.vertices.push_back({n * radius, n, {u, v}});
        }
    }
    for (int y = 0; y < stacks; ++y) {
        for (int x = 0; x < slices; ++x) {
            const uint32_t i0 = static_cast<uint32_t>(y * (slices + 1) + x);
            const uint32_t i1 = i0 + static_cast<uint32_t>(slices + 1);
            m.indices.insert(m.indices.end(), {i0, i1, i0 + 1, i0 + 1, i1, i1 + 1});
        }
    }
    m.computeBounds();
    return m;
}

MeshData makeGrassBladeMesh() {
    MeshData m;
    // Thin vertical blade, slight taper.
    m.vertices = {
        {{-0.02f, 0.0f, 0.0f}, {0, 0, 1}, {0, 0}},
        {{0.02f, 0.0f, 0.0f}, {0, 0, 1}, {1, 0}},
        {{0.01f, 0.35f, 0.0f}, {0, 0, 1}, {0.75f, 0.7f}},
        {{0.0f, 0.55f, 0.0f}, {0, 0, 1}, {0.5f, 1}},
        {{-0.01f, 0.35f, 0.0f}, {0, 0, 1}, {0.25f, 0.7f}},
    };
    m.indices = {0, 1, 2, 0, 2, 4, 4, 2, 3};
    m.computeNormals();
    m.computeBounds();
    return m;
}

MeshData makeGridMesh(float size, int resolution) {
    MeshData m;
    resolution = std::max(resolution, 2);
    const float half = size * 0.5f;
    for (int z = 0; z < resolution; ++z) {
        for (int x = 0; x < resolution; ++x) {
            const float u = static_cast<float>(x) / (resolution - 1);
            const float v = static_cast<float>(z) / (resolution - 1);
            m.vertices.push_back({{(u * 2 - 1) * half, 0, (v * 2 - 1) * half},
                                  {0, 1, 0},
                                  {u, v}});
        }
    }
    for (int z = 0; z < resolution - 1; ++z) {
        for (int x = 0; x < resolution - 1; ++x) {
            const uint32_t i0 = static_cast<uint32_t>(z * resolution + x);
            const uint32_t i1 = i0 + 1;
            const uint32_t i2 = i0 + static_cast<uint32_t>(resolution);
            const uint32_t i3 = i2 + 1;
            m.indices.insert(m.indices.end(), {i0, i2, i1, i1, i2, i3});
        }
    }
    m.computeBounds();
    return m;
}

MeshData makeRockMesh(float radius, int slices, int stacks, float seed) {
    MeshData m = makeSphereMesh(radius, slices, stacks);
    for (size_t i = 0; i < m.vertices.size(); ++i) {
        Vertex& v = m.vertices[i];
        const float n = std::sin(v.position.x * (4.1f + seed) + v.position.y * 5.3f +
                                 v.position.z * (3.7f + seed * 0.5f));
        const float n2 = std::sin(v.position.y * 7.0f + seed * 2.0f);
        const float k = 1.0f + 0.28f * n + 0.12f * n2;
        v.position *= k;
        // Flatten the bottom a bit so rocks sit on ground.
        if (v.position.y < -radius * 0.35f) v.position.y *= 0.55f;
    }
    m.computeNormals();
    m.computeBounds();
    return m;
}

MeshData makeTaperedTrunk(float baseRadius, float topRadius, float height, int segments) {
    MeshData m;
    segments = std::max(segments, 6);
    const int rings = 8;
    for (int y = 0; y <= rings; ++y) {
        const float t = static_cast<float>(y) / rings;
        const float yy = t * height;
        const float r = glm::mix(baseRadius, topRadius, t);
        // Slight lean noise baked into radius.
        for (int i = 0; i <= segments; ++i) {
            const float a = static_cast<float>(i) / segments * 6.2831853f;
            const float c = std::cos(a), s = std::sin(a);
            const glm::vec3 n = glm::normalize(glm::vec3(c, (baseRadius - topRadius) / height, s));
            m.vertices.push_back({{c * r, yy, s * r}, n, {static_cast<float>(i) / segments, t}});
        }
    }
    for (int y = 0; y < rings; ++y) {
        for (int i = 0; i < segments; ++i) {
            const uint32_t i0 = static_cast<uint32_t>(y * (segments + 1) + i);
            const uint32_t i1 = i0 + static_cast<uint32_t>(segments + 1);
            m.indices.insert(m.indices.end(), {i0, i1, i0 + 1, i0 + 1, i1, i1 + 1});
        }
    }
    m.computeNormals();
    m.computeBounds();
    return m;
}

MeshData makeFrondMesh(float length, float width) {
    MeshData m;
    const int segs = 8;
    for (int i = 0; i <= segs; ++i) {
        const float t = static_cast<float>(i) / segs;
        const float x = t * length;
        // Droop toward the tip.
        const float y = -t * t * length * 0.35f;
        const float w = width * (1.0f - t * 0.85f);
        m.vertices.push_back({{x, y, -w}, {0, 1, 0}, {0, t}});
        m.vertices.push_back({{x, y, w}, {0, 1, 0}, {1, t}});
    }
    for (int i = 0; i < segs; ++i) {
        const uint32_t b = static_cast<uint32_t>(i * 2);
        m.indices.insert(m.indices.end(), {b, b + 1, b + 2, b + 2, b + 1, b + 3});
    }
    m.computeNormals();
    m.computeBounds();
    return m;
}

MeshData makeDiscMesh(float radius, int rings, int segments) {
    MeshData m;
    rings = std::max(rings, 2);
    segments = std::max(segments, 8);
    // Center vertex
    m.vertices.push_back({{0, 0, 0}, {0, 1, 0}, {0.5f, 0.5f}});
    for (int r = 1; r <= rings; ++r) {
        const float t = static_cast<float>(r) / rings;
        const float rad = t * radius;
        for (int i = 0; i < segments; ++i) {
            const float a = static_cast<float>(i) / segments * 6.2831853f;
            const float c = std::cos(a), s = std::sin(a);
            m.vertices.push_back({{c * rad, 0, s * rad},
                                  {0, 1, 0},
                                  {c * 0.5f * t + 0.5f, s * 0.5f * t + 0.5f}});
        }
    }
    // First ring fans from center
    for (int i = 0; i < segments; ++i) {
        const uint32_t i0 = 1 + static_cast<uint32_t>(i);
        const uint32_t i1 = 1 + static_cast<uint32_t>((i + 1) % segments);
        m.indices.insert(m.indices.end(), {0, i0, i1});
    }
    for (int r = 1; r < rings; ++r) {
        const uint32_t base0 = 1 + static_cast<uint32_t>((r - 1) * segments);
        const uint32_t base1 = 1 + static_cast<uint32_t>(r * segments);
        for (int i = 0; i < segments; ++i) {
            const uint32_t i0 = base0 + static_cast<uint32_t>(i);
            const uint32_t i1 = base0 + static_cast<uint32_t>((i + 1) % segments);
            const uint32_t i2 = base1 + static_cast<uint32_t>(i);
            const uint32_t i3 = base1 + static_cast<uint32_t>((i + 1) % segments);
            m.indices.insert(m.indices.end(), {i0, i2, i1, i1, i2, i3});
        }
    }
    m.computeBounds();
    return m;
}
