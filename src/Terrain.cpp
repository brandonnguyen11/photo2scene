#include "Terrain.h"

#include <algorithm>
#include <cmath>

namespace {

float hash2(float x, float z) {
    return std::fmod(std::sin(x * 127.1f + z * 311.7f) * 43758.5453f, 1.0f);
}

float valueNoise(float x, float z) {
    const float xi = std::floor(x), zi = std::floor(z);
    const float xf = x - xi, zf = z - zi;
    const float u = xf * xf * (3.0f - 2.0f * xf);
    const float v = zf * zf * (3.0f - 2.0f * zf);
    const float a = hash2(xi, zi);
    const float b = hash2(xi + 1.0f, zi);
    const float c = hash2(xi, zi + 1.0f);
    const float d = hash2(xi + 1.0f, zi + 1.0f);
    return glm::mix(glm::mix(a, b, u), glm::mix(c, d, u), v);
}

float fbm(float x, float z, int octaves) {
    float amp = 0.5f, freq = 1.0f, sum = 0.0f, norm = 0.0f;
    for (int i = 0; i < octaves; ++i) {
        sum += amp * valueNoise(x * freq, z * freq);
        norm += amp;
        amp *= 0.5f;
        freq *= 2.0f;
    }
    return sum / std::max(norm, 1e-5f);
}

float ridged(float x, float z) {
    return 1.0f - std::abs(fbm(x, z, 4) * 2.0f - 1.0f);
}

}  // namespace

bool Terrain::build(const TerrainDesc& desc) {
    clear();
    desc_ = desc;
    res_ = std::max(desc.resolution, 8);
    half_ = desc.size * 0.5f;
    heights_.assign(static_cast<size_t>(res_) * res_, 0.0f);

    for (int z = 0; z < res_; ++z) {
        for (int x = 0; x < res_; ++x) {
            const float u = (static_cast<float>(x) / (res_ - 1)) * 2.0f - 1.0f;
            const float v = (static_cast<float>(z) / (res_ - 1)) * 2.0f - 1.0f;
            const float wx = u * half_;
            const float wz = v * half_;
            float h = 0.0f;

            if (desc.type == "beach") {
                // Rising inland (+Z), dropping into the ocean (−Z) with dunes & ripples.
                const float inland = std::clamp((wz + 25.0f) / 70.0f, 0.0f, 1.0f);
                const float ocean = std::clamp((-wz - 5.0f) / 40.0f, 0.0f, 1.0f);

                // Broad dune ridges parallel to the shore.
                const float dunes =
                    ridged(wx * 0.035f + 2.0f, wz * 0.08f) * inland * desc.maxHeight * 0.85f;
                const float mounds =
                    (fbm(wx * 0.06f, wz * 0.06f, 5) - 0.35f) * inland * desc.maxHeight * 0.55f;
                const float ripples =
                    std::sin(wx * 2.8f + fbm(wx * 0.2f, wz * 0.2f, 2) * 3.0f) * 0.08f * inland;

                h = inland * inland * 1.2f + dunes + mounds + ripples;
                // Underwater shelf slopes gently down.
                h -= ocean * ocean * 2.8f;
                if (wz < -8.0f) h = std::min(h, -0.15f - ocean * 1.5f);
            } else if (desc.type == "hills") {
                h = fbm(wx * 0.018f + 3.1f, wz * 0.018f + 1.7f, 6) * desc.maxHeight;
                h += ridged(wx * 0.04f, wz * 0.04f) * desc.maxHeight * 0.25f;
            } else {
                // Formal garden: gentle bowl + lawn undulation; paths sit slightly lower.
                const float lawn = (fbm(wx * 0.04f, wz * 0.04f, 4) - 0.5f) * 0.55f;
                const float edgeRise =
                    std::pow(std::max(std::abs(wx), std::abs(wz)) / half_, 2.5f) * 1.8f;
                // Soft depression along the main axes (where gravel paths go).
                const float pathX = std::exp(-std::pow(wx / 3.6f, 2.0f)) * 0.18f;
                const float pathZ = std::exp(-std::pow(wz / 3.6f, 2.0f)) * 0.12f;
                h = lawn + edgeRise - pathX - pathZ;
            }

            heights_[static_cast<size_t>(z) * res_ + x] = h;
        }
    }

    mesh_.vertices.clear();
    mesh_.indices.clear();
    mesh_.vertices.reserve(static_cast<size_t>(res_) * res_);

    // Tighter UVs so procedural shaders get more frequency on the ground.
    const float uvScale = desc.size / 4.5f;
    for (int z = 0; z < res_; ++z) {
        for (int x = 0; x < res_; ++x) {
            const float u = static_cast<float>(x) / (res_ - 1);
            const float v = static_cast<float>(z) / (res_ - 1);
            const float wx = (u * 2.0f - 1.0f) * half_;
            const float wz = (v * 2.0f - 1.0f) * half_;
            const float wy = heights_[static_cast<size_t>(z) * res_ + x];
            Vertex vert;
            vert.position = {wx, wy, wz};
            vert.uv = {u * uvScale, v * uvScale};
            vert.normal = {0, 1, 0};
            mesh_.vertices.push_back(vert);
        }
    }

    for (int z = 0; z < res_ - 1; ++z) {
        for (int x = 0; x < res_ - 1; ++x) {
            const uint32_t i0 = static_cast<uint32_t>(z * res_ + x);
            const uint32_t i1 = i0 + 1;
            const uint32_t i2 = i0 + static_cast<uint32_t>(res_);
            const uint32_t i3 = i2 + 1;
            mesh_.indices.insert(mesh_.indices.end(), {i0, i2, i1, i1, i2, i3});
        }
    }
    mesh_.computeNormals();
    mesh_.computeBounds();
    return true;
}

void Terrain::clear() {
    heights_.clear();
    mesh_ = MeshData{};
    res_ = 0;
}

float Terrain::sampleRaw(int ix, int iz) const {
    ix = std::clamp(ix, 0, res_ - 1);
    iz = std::clamp(iz, 0, res_ - 1);
    return heights_[static_cast<size_t>(iz) * res_ + ix];
}

float Terrain::heightAt(float x, float z) const {
    if (res_ < 2) return 0.0f;
    const float u = (x / half_ + 1.0f) * 0.5f * (res_ - 1);
    const float v = (z / half_ + 1.0f) * 0.5f * (res_ - 1);
    const int x0 = static_cast<int>(std::floor(u));
    const int z0 = static_cast<int>(std::floor(v));
    const float tx = u - x0;
    const float tz = v - z0;
    const float h00 = sampleRaw(x0, z0);
    const float h10 = sampleRaw(x0 + 1, z0);
    const float h01 = sampleRaw(x0, z0 + 1);
    const float h11 = sampleRaw(x0 + 1, z0 + 1);
    return glm::mix(glm::mix(h00, h10, tx), glm::mix(h01, h11, tx), tz);
}

glm::vec3 Terrain::normalAt(float x, float z) const {
    const float e = desc_.size / std::max(res_, 2);
    const float hl = heightAt(x - e, z);
    const float hr = heightAt(x + e, z);
    const float hd = heightAt(x, z - e);
    const float hu = heightAt(x, z + e);
    return glm::normalize(glm::vec3(hl - hr, 2.0f * e, hd - hu));
}

MeshData makeBackdropRing(float innerRadius, float outerRadius, float height, int segments,
                          bool seaHorizon) {
    MeshData m;
    segments = std::max(segments, 16);
    for (int i = 0; i <= segments; ++i) {
        const float t = static_cast<float>(i) / segments;
        const float a = t * 6.2831853f;
        const float c = std::cos(a), s = std::sin(a);
        const float hNoise =
            seaHorizon ? 0.15f
                       : (0.45f + 0.55f * (0.5f + 0.5f * std::sin(a * 2.5f + 0.4f)) *
                                      (0.5f + 0.5f * std::sin(a * 5.0f)));
        Vertex inner{{c * innerRadius, seaHorizon ? -1.0f : 0.0f, s * innerRadius},
                     {0, 1, 0},
                     {t * 8.0f, 0.0f}};
        Vertex outer{{c * outerRadius, height * hNoise, s * outerRadius},
                     {-c, 0.25f, -s},
                     {t * 8.0f, 1.0f}};
        if (seaHorizon) {
            outer.position.y = -0.8f;
            outer.normal = {0, 1, 0};
        }
        m.vertices.push_back(inner);
        m.vertices.push_back(outer);
    }
    for (int i = 0; i < segments; ++i) {
        const uint32_t b = static_cast<uint32_t>(i * 2);
        m.indices.insert(m.indices.end(), {b, b + 1, b + 2, b + 2, b + 1, b + 3});
    }
    m.computeNormals();
    m.computeBounds();
    return m;
}
