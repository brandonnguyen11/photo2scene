#pragma once
// World.h — data-driven immersive world (terrain, props, water, grass, sky defaults)

#include "Mesh.h"
#include "Sky.h"
#include "Terrain.h"

#include <glad/gl.h>
#include <glm/glm.hpp>

#include <functional>
#include <string>
#include <vector>

struct PhotoSpot {
    std::string name;
    glm::vec3 position{0.0f};
    float facingYawDeg = 0.0f;
};

enum class WorldMaterial : int {
    Plain = 0,
    Grass = 1,
    Gravel = 2,
    Hedge = 3,
    Sand = 4,
    Stone = 5,
    Water = 6
};

struct WorldObject {
    std::string name;
    Mesh mesh;
    glm::mat4 model{1.0f};
    glm::vec3 baseColor{0.7f};
    WorldMaterial material = WorldMaterial::Plain;
    bool castsShadow = true;
    bool visible = true;
    bool twoSided = false;
};

struct GrassInstance {
    glm::vec4 posScale{0, 0, 0, 1};  // xyz + uniform scale
    float yaw = 0.0f;
    float phase = 0.0f;
};

struct WorldDesc {
    std::string name = "World";
    std::string id = "world";
    std::string builder;  // royal_garden | beach | forest
    TerrainDesc terrain;
    SkySettings sky;
    std::vector<PhotoSpot> photoSpots;
    bool hasWater = false;
    glm::vec3 waterCenter{0.0f};
    float waterSize = 80.0f;
    float waterLevel = 0.0f;
    int grassCount = 60000;
};

class World {
public:
    bool loadFromFile(const std::string& path, std::string* error);
    bool loadFromDesc(const WorldDesc& desc, std::string* error);
    void shutdown();

    void update(float timeSeconds, float dt);

    const WorldDesc& desc() const { return desc_; }
    Terrain& terrain() { return terrain_; }
    const Terrain& terrain() const { return terrain_; }
    Sky& sky() { return sky_; }
    const Sky& sky() const { return sky_; }

    const std::vector<WorldObject>& objects() const { return objects_; }
    const Mesh& terrainMesh() const { return terrainGpu_; }
    const Mesh& backdropMesh() const { return backdropGpu_; }
    const Mesh& waterMesh() const { return waterGpu_; }
    const Mesh& grassMesh() const { return grassGpu_; }
    bool hasGrass() const { return grassGpu_.instanceCount() > 0; }
    bool hasWater() const { return desc_.hasWater && waterGpu_.valid(); }

    glm::vec3 center() const { return center_; }
    float radius() const { return radius_; }
    const std::vector<PhotoSpot>& photoSpots() const { return desc_.photoSpots; }

    glm::vec3 terrainColor() const { return desc_.terrain.baseColor; }
    WorldMaterial terrainMaterial() const { return terrainMaterial_; }
    glm::vec3 backdropColor() const { return backdropColor_; }
    glm::vec3 waterDeepColor() const { return waterDeep_; }
    glm::vec3 waterShallowColor() const { return waterShallow_; }

    float timeSeconds() const { return timeSeconds_; }
    float windStrength() const { return sky_.settings.windStrength; }

private:
    void buildRoyalGarden();
    void buildBeach();
    void buildForest();
    void buildGrass(int count, float minX, float maxX, float minZ, float maxZ,
                    const std::function<bool(float, float)>& mask);
    void addBox(const std::string& name, const glm::vec3& minC, const glm::vec3& maxC,
                const glm::vec3& color, bool castShadow = true,
                WorldMaterial material = WorldMaterial::Plain);
    void addMesh(const std::string& name, MeshData data, const glm::mat4& model,
                 const glm::vec3& color, bool castShadow = true,
                 WorldMaterial material = WorldMaterial::Plain);
    void finishBounds();

    WorldDesc desc_;
    Terrain terrain_;
    Sky sky_;
    Mesh terrainGpu_;
    Mesh backdropGpu_;
    Mesh waterGpu_;
    Mesh grassGpu_;
    std::vector<WorldObject> objects_;
    glm::vec3 center_{0.0f, 1.0f, 0.0f};
    float radius_ = 40.0f;
    WorldMaterial terrainMaterial_ = WorldMaterial::Grass;
    glm::vec3 backdropColor_{0.35f, 0.45f, 0.28f};
    glm::vec3 waterDeep_{0.02f, 0.12f, 0.22f};
    glm::vec3 waterShallow_{0.15f, 0.45f, 0.48f};
    float timeSeconds_ = 0.0f;
};
