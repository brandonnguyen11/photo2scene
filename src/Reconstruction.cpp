#include "Reconstruction.h"

#include <glm/glm.hpp>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <limits>

void gridSizeFor(int w, int h, int resolution, int& cols, int& rows) {
    resolution = std::max(resolution, 2);
    if (w >= h) {
        cols = std::min(resolution, w);
        rows = static_cast<int>(std::lround(static_cast<double>(cols) * h / w));
    } else {
        rows = std::min(resolution, h);
        cols = static_cast<int>(std::lround(static_cast<double>(rows) * w / h));
    }
    cols = std::max(cols, 2);
    rows = std::max(rows, 2);
}

float mapDepthToDistance(float d, const ReconstructionSettings& s) {
    d = std::clamp(d, 0.0f, 1.0f);
    if (s.invertDepth) d = 1.0f - d;
    const float zNear = std::max(s.nearDistance, 1e-3f);
    const float zFar = std::max(s.farDistance, zNear + 1e-3f);
    if (s.mapping == DepthMapping::Disparity) {
        // Interpolate in inverse-depth space: d=1 -> near, d=0 -> far.
        const float inv = (1.0f / zFar) + d * ((1.0f / zNear) - (1.0f / zFar));
        return 1.0f / inv;
    }
    return zFar + d * (zNear - zFar);
}

std::vector<float> resampleBilinear(const DepthImage& depth, int cols, int rows) {
    std::vector<float> out(static_cast<size_t>(cols) * rows);
    for (int j = 0; j < rows; ++j) {
        // Map grid row j to the source image so the first/last samples land on
        // the first/last pixel centers.
        const float sy = rows > 1 ? static_cast<float>(j) * (depth.height - 1) / (rows - 1) : 0.0f;
        const int y0 = static_cast<int>(sy);
        const int y1 = std::min(y0 + 1, depth.height - 1);
        const float fy = sy - y0;
        for (int i = 0; i < cols; ++i) {
            const float sx = cols > 1 ? static_cast<float>(i) * (depth.width - 1) / (cols - 1) : 0.0f;
            const int x0 = static_cast<int>(sx);
            const int x1 = std::min(x0 + 1, depth.width - 1);
            const float fx = sx - x0;
            const float top = depth.at(x0, y0) * (1 - fx) + depth.at(x1, y0) * fx;
            const float bot = depth.at(x0, y1) * (1 - fx) + depth.at(x1, y1) * fx;
            out[static_cast<size_t>(j) * cols + i] = top * (1 - fy) + bot * fy;
        }
    }
    return out;
}

void bilateralSmooth(std::vector<float>& grid, int cols, int rows, float rangeSigma,
                     int iterations) {
    if (iterations <= 0 || rangeSigma <= 0.0f) return;
    constexpr int kRadius = 2;
    constexpr float kSpatialSigma = 1.5f;
    const float invTwoSpatial = 1.0f / (2.0f * kSpatialSigma * kSpatialSigma);
    const float invTwoRange = 1.0f / (2.0f * rangeSigma * rangeSigma);

    float spatial[2 * kRadius + 1][2 * kRadius + 1];
    for (int dy = -kRadius; dy <= kRadius; ++dy)
        for (int dx = -kRadius; dx <= kRadius; ++dx)
            spatial[dy + kRadius][dx + kRadius] =
                std::exp(-static_cast<float>(dx * dx + dy * dy) * invTwoSpatial);

    std::vector<float> tmp(grid.size());
    for (int it = 0; it < iterations; ++it) {
        for (int y = 0; y < rows; ++y) {
            for (int x = 0; x < cols; ++x) {
                const float center = grid[static_cast<size_t>(y) * cols + x];
                float sum = 0.0f, wsum = 0.0f;
                for (int dy = -kRadius; dy <= kRadius; ++dy) {
                    const int yy = std::clamp(y + dy, 0, rows - 1);
                    for (int dx = -kRadius; dx <= kRadius; ++dx) {
                        const int xx = std::clamp(x + dx, 0, cols - 1);
                        const float v = grid[static_cast<size_t>(yy) * cols + xx];
                        const float diff = v - center;
                        const float w = spatial[dy + kRadius][dx + kRadius] *
                                        std::exp(-diff * diff * invTwoRange);
                        sum += w * v;
                        wsum += w;
                    }
                }
                tmp[static_cast<size_t>(y) * cols + x] = wsum > 0.0f ? sum / wsum : center;
            }
        }
        grid.swap(tmp);
    }
}

ReconstructionResult reconstructMesh(const DepthImage& depth, const ReconstructionSettings& s) {
    const auto t0 = std::chrono::steady_clock::now();
    ReconstructionResult result;

    if (depth.empty() || depth.width < 2 || depth.height < 2) {
        result.error = "Depth map is empty or too small.";
        return result;
    }

    int cols = 0, rows = 0;
    gridSizeFor(depth.width, depth.height, s.gridResolution, cols, rows);

    // 1) Resample to grid resolution and clean invalid values.
    std::vector<float> grid = resampleBilinear(depth, cols, rows);
    for (float& v : grid) v = std::isfinite(v) ? std::clamp(v, 0.0f, 1.0f) : 0.0f;

    // 2) Edge-preserving smoothing (reduces noise without blurring silhouettes).
    if (!s.flatPlane) bilateralSmooth(grid, cols, rows, s.smoothRangeSigma, s.smoothIterations);

    // 3) Back-project every grid sample through the pinhole camera.
    const float aspect = static_cast<float>(depth.width) / static_cast<float>(depth.height);
    const float halfH = std::tan(glm::radians(std::clamp(s.fovYDeg, 5.0f, 150.0f)) * 0.5f);
    const float halfW = halfH * aspect;
    const float flatZ = 0.5f * (s.nearDistance + s.farDistance);

    const size_t n = static_cast<size_t>(cols) * rows;
    std::vector<Vertex> verts(n);
    std::vector<float> zs(n);
    std::vector<char> valid(n, 1);

    for (int j = 0; j < rows; ++j) {
        const float v = static_cast<float>(j) / (rows - 1);  // 0 = top row of the photo
        for (int i = 0; i < cols; ++i) {
            const float u = static_cast<float>(i) / (cols - 1);
            const size_t k = static_cast<size_t>(j) * cols + i;
            float d = grid[k];
            if (s.invertDepth) d = 1.0f - d;
            if (s.backgroundClip > 0.0f && !s.flatPlane && d < s.backgroundClip) valid[k] = 0;

            const float z = s.flatPlane ? flatZ : mapDepthToDistance(grid[k], s);
            zs[k] = z;
            verts[k].position = glm::vec3((2.0f * u - 1.0f) * halfW * z, (1.0f - 2.0f * v) * halfH * z, -z);
            verts[k].uv = glm::vec2(u, v);
        }
    }

    // 4) Triangulate neighboring samples, skipping triangles that straddle a
    //    depth discontinuity (they would stretch into long "rubber sheets").
    std::vector<uint32_t> indices;
    indices.reserve(static_cast<size_t>(cols - 1) * (rows - 1) * 6);
    size_t dropped = 0;

    auto tryTriangle = [&](size_t a, size_t b, size_t c) {
        if (!valid[a] || !valid[b] || !valid[c]) {
            ++dropped;
            return;
        }
        if (s.cutDiscontinuities && !s.flatPlane) {
            const float zmin = std::min({zs[a], zs[b], zs[c]});
            const float zmax = std::max({zs[a], zs[b], zs[c]});
            if (zmax / zmin - 1.0f > s.discontinuityThreshold) {
                ++dropped;
                return;
            }
        }
        // Counter-clockwise as seen from the original camera.
        indices.push_back(static_cast<uint32_t>(a));
        indices.push_back(static_cast<uint32_t>(b));
        indices.push_back(static_cast<uint32_t>(c));
    };

    for (int j = 0; j < rows - 1; ++j) {
        for (int i = 0; i < cols - 1; ++i) {
            const size_t i00 = static_cast<size_t>(j) * cols + i;
            const size_t i10 = i00 + 1;
            const size_t i01 = i00 + cols;
            const size_t i11 = i01 + 1;
            tryTriangle(i00, i01, i10);
            tryTriangle(i10, i01, i11);
        }
    }

    if (indices.empty()) {
        result.error = "All triangles were removed. Lower the background clip or raise the edge threshold.";
        return result;
    }

    // 5) Compact: drop vertices no triangle uses.
    std::vector<uint32_t> remap(n, std::numeric_limits<uint32_t>::max());
    MeshData& mesh = result.mesh;
    mesh.vertices.reserve(n);
    for (uint32_t& idx : indices) {
        if (remap[idx] == std::numeric_limits<uint32_t>::max()) {
            remap[idx] = static_cast<uint32_t>(mesh.vertices.size());
            mesh.vertices.push_back(verts[idx]);
        }
        idx = remap[idx];
    }
    mesh.indices = std::move(indices);

    // 6) Normals + bounds.
    mesh.computeNormals();
    mesh.computeBounds();

    result.ok = true;
    result.stats.gridCols = cols;
    result.stats.gridRows = rows;
    result.stats.vertexCount = mesh.vertices.size();
    result.stats.triangleCount = mesh.triangleCount();
    result.stats.droppedTriangles = dropped;
    result.stats.timeMs =
        std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
    return result;
}
