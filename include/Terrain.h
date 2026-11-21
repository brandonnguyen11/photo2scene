#pragma once
// Terrain.h — heightmap mesh + height queries for walking / placement

#include "MeshData.h"

#include <glm/glm.hpp>

#include <functional>
#include <string>
#include <vector>

struct TerrainDesc {
    std::string type = "flat";  // flat | beach | hills
    float size = 160.0f;        // world extent in meters (−size/2 … +size/2)
    int resolution = 192;       // grid verts per side
    float maxHeight = 8.0f;
    glm::vec3 baseColor{0.28f, 0.42f, 0.18f};
};

class Terrain {
public:
    bool build(const TerrainDesc& desc);
    void clear();

    const MeshData& mesh() const { return mesh_; }
    const TerrainDesc& desc() const { return desc_; }

    // Bilinear sample of the heightfield (y). Outside returns edge height.
    float heightAt(float x, float z) const;
    glm::vec3 normalAt(float x, float z) const;

    bool valid() const { return !mesh_.empty(); }

private:
    float sampleRaw(int ix, int iz) const;

    TerrainDesc desc_;
    int res_ = 0;
    float half_ = 0.0f;
    std::vector<float> heights_;
    MeshData mesh_;
};

// Backdrop ring of distant hills / sea horizon so the world has no hard edge (W-5).
MeshData makeBackdropRing(float innerRadius, float outerRadius, float height, int segments,
                          bool seaHorizon);
