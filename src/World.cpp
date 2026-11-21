#include "World.h"

#include <nlohmann/json.hpp>

#include <glm/gtc/matrix_transform.hpp>

#include <cmath>
#include <fstream>
#include <random>

using json = nlohmann::json;

namespace {

float hash01(float x, float z) {
    return std::fmod(std::sin(x * 12.9898f + z * 78.233f) * 43758.5453f, 1.0f);
}

glm::mat4 trs(const glm::vec3& t, float yawDeg, const glm::vec3& s) {
    return glm::translate(glm::mat4(1.0f), t) *
           glm::rotate(glm::mat4(1.0f), glm::radians(yawDeg), glm::vec3(0, 1, 0)) *
           glm::scale(glm::mat4(1.0f), s);
}

}  // namespace

bool World::loadFromFile(const std::string& path, std::string* error) {
    std::ifstream in(path);
    if (!in) {
        if (error) *error = "Could not open world file: " + path;
        return false;
    }
    json j;
    try {
        in >> j;
    } catch (const std::exception& e) {
        if (error) *error = std::string("JSON parse error: ") + e.what();
        return false;
    }

    WorldDesc d;
    d.id = j.value("id", "world");
    d.name = j.value("name", "World");
    d.builder = j.value("builder", "royal_garden");
    if (j.contains("terrain")) {
        const auto& t = j["terrain"];
        d.terrain.type = t.value("type", "flat");
        d.terrain.size = t.value("size", 160.0f);
        d.terrain.resolution = t.value("resolution", 192);
        d.terrain.maxHeight = t.value("maxHeight", 8.0f);
        if (t.contains("baseColor") && t["baseColor"].is_array() && t["baseColor"].size() >= 3) {
            d.terrain.baseColor = {t["baseColor"][0], t["baseColor"][1], t["baseColor"][2]};
        }
    }
    if (j.contains("lighting")) {
        const auto& L = j["lighting"];
        d.sky.timeOfDayHours = L.value("timeOfDay", 10.0f);
        d.sky.latitudeDeg = L.value("latitude", 48.8f);
        d.sky.turbidity = L.value("turbidity", 2.7f);
        d.sky.windStrength = L.value("wind", 0.45f);
        d.sky.fogDensity = L.value("fog", 0.018f);
        d.sky.cloudCover = L.value("cloudCover", 0.25f);
    }
    if (j.contains("water")) {
        d.hasWater = true;
        const auto& w = j["water"];
        d.waterSize = w.value("size", 80.0f);
        d.waterLevel = w.value("level", 0.0f);
        if (w.contains("center") && w["center"].size() >= 3) {
            d.waterCenter = {w["center"][0], w["center"][1], w["center"][2]};
        }
    }
    d.grassCount = j.value("grassCount", 60000);
    if (j.contains("photoSpots")) {
        for (const auto& s : j["photoSpots"]) {
            PhotoSpot spot;
            spot.name = s.value("name", "Spot");
            if (s.contains("position") && s["position"].size() >= 3) {
                spot.position = {s["position"][0], s["position"][1], s["position"][2]};
            }
            spot.facingYawDeg = s.value("facingYawDeg", 0.0f);
            d.photoSpots.push_back(spot);
        }
    }
    return loadFromDesc(d, error);
}

bool World::loadFromDesc(const WorldDesc& desc, std::string* error) {
    shutdown();
    desc_ = desc;
    sky_.settings = desc.sky;

    if (!terrain_.build(desc.terrain)) {
        if (error) *error = "Failed to build terrain";
        return false;
    }
    terrainGpu_.upload(terrain_.mesh());

    if (desc.builder == "beach") {
        buildBeach();
    } else if (desc.builder == "forest") {
        buildForest();
    } else {
        buildRoyalGarden();
    }

    finishBounds();
    return true;
}

void World::shutdown() {
    for (WorldObject& o : objects_) o.mesh.destroy();
    objects_.clear();
    terrainGpu_.destroy();
    backdropGpu_.destroy();
    waterGpu_.destroy();
    grassGpu_.destroy();
    terrain_.clear();
}

void World::update(float timeSeconds, float dt) {
    timeSeconds_ = timeSeconds;
    sky_.tick(dt);
}

void World::addBox(const std::string& name, const glm::vec3& minC, const glm::vec3& maxC,
                   const glm::vec3& color, bool castShadow, WorldMaterial material) {
    addMesh(name, makeBoxMesh(minC, maxC), glm::mat4(1.0f), color, castShadow, material);
}

void World::addMesh(const std::string& name, MeshData data, const glm::mat4& model,
                    const glm::vec3& color, bool castShadow, WorldMaterial material) {
    WorldObject obj;
    obj.name = name;
    obj.mesh.upload(data);
    obj.model = model;
    obj.baseColor = color;
    obj.material = material;
    obj.castsShadow = castShadow;
    objects_.push_back(std::move(obj));
}

void World::buildGrass(int count, float minX, float maxX, float minZ, float maxZ,
                       const std::function<bool(float, float)>& mask) {
    count = std::max(count, 0);
    std::vector<GrassInstance> instances;
    instances.reserve(static_cast<size_t>(count));
    std::mt19937 rng(42);
    std::uniform_real_distribution<float> ux(minX, maxX);
    std::uniform_real_distribution<float> uz(minZ, maxZ);
    std::uniform_real_distribution<float> us(0.7f, 1.35f);
    std::uniform_real_distribution<float> uy(0.0f, 6.2831853f);
    std::uniform_real_distribution<float> up(0.0f, 6.2831853f);

    int attempts = 0;
    while (static_cast<int>(instances.size()) < count && attempts < count * 8) {
        ++attempts;
        const float x = ux(rng);
        const float z = uz(rng);
        if (mask && !mask(x, z)) continue;
        const float y = terrain_.heightAt(x, z);
        GrassInstance g;
        g.posScale = {x, y, z, us(rng)};
        g.yaw = uy(rng);
        g.phase = up(rng);
        instances.push_back(g);
    }

    grassGpu_.upload(makeGrassBladeMesh());
    if (!instances.empty()) {
        grassGpu_.setInstances(instances.data(), sizeof(GrassInstance), instances.size());
    }
}

void World::buildRoyalGarden() {
    terrainMaterial_ = WorldMaterial::Grass;
    backdropColor_ = {0.32f, 0.42f, 0.22f};
    // Distant soft hills — far enough that fog blends them into the horizon.
    backdropGpu_.upload(makeBackdropRing(desc_.terrain.size * 0.62f, desc_.terrain.size * 0.95f,
                                         10.0f, 72, false));
    backdropColor_ = {0.45f, 0.55f, 0.42f};

    const glm::vec3 hedge(0.18f, 0.38f, 0.16f);
    const glm::vec3 hedgeDark(0.12f, 0.28f, 0.12f);
    const glm::vec3 gravel(0.55f, 0.50f, 0.42f);
    const glm::vec3 stone(0.62f, 0.60f, 0.56f);
    const glm::vec3 palace(0.86f, 0.82f, 0.74f);
    const glm::vec3 roof(0.45f, 0.22f, 0.18f);
    const glm::vec3 water(0.25f, 0.55f, 0.62f);

    // Central gravel axis (mirror-symmetric about Z).
    addBox("path_main", {-3.2f, -0.02f, -55.0f}, {3.2f, 0.03f, 42.0f}, gravel, false,
           WorldMaterial::Gravel);
    addBox("path_cross", {-28.0f, -0.02f, -3.0f}, {28.0f, 0.03f, 3.0f}, gravel, false,
           WorldMaterial::Gravel);

    // Parterre hedges — curling box patterns, mirrored.
    auto hedgeBox = [&](float x0, float z0, float x1, float z1, float h = 0.85f) {
        addBox("hedge", {x0, 0.0f, z0}, {x1, h, z1}, hedge, true, WorldMaterial::Hedge);
    };
    for (int side : {-1, 1}) {
        const float s = static_cast<float>(side);
        // Outer frame
        hedgeBox(s * 6.0f, -22.0f, s * 7.0f, 18.0f, 1.1f);
        hedgeBox(s * 22.0f, -22.0f, s * 23.0f, 18.0f, 1.1f);
        hedgeBox(s * 6.0f, -22.0f, s * 23.0f, -21.0f, 1.1f);
        hedgeBox(s * 6.0f, 17.0f, s * 23.0f, 18.0f, 1.1f);
        // Inner scrolls
        hedgeBox(s * 9.0f, -16.0f, s * 10.0f, -4.0f);
        hedgeBox(s * 9.0f, -16.0f, s * 18.0f, -15.0f);
        hedgeBox(s * 17.0f, -16.0f, s * 18.0f, -8.0f);
        hedgeBox(s * 12.0f, -10.0f, s * 18.0f, -9.0f);
        hedgeBox(s * 9.0f, 2.0f, s * 18.0f, 3.0f);
        hedgeBox(s * 9.0f, 2.0f, s * 10.0f, 12.0f);
        hedgeBox(s * 17.0f, 8.0f, s * 18.0f, 12.0f);
        hedgeBox(s * 12.0f, 11.0f, s * 18.0f, 12.0f);
        // Diamond accents
        hedgeBox(s * 12.5f, -1.5f, s * 14.5f, 0.5f, 0.7f);
    }

    // Cone + ball topiaries along the main path.
    MeshData cone = makeConeMesh(0.55f, 2.4f, 14);
    MeshData ball = makeSphereMesh(0.55f, 14, 10);
    MeshData stem = makeCylinderMesh(0.08f, 0.7f, 8);
    for (int side : {-1, 1}) {
        for (int i = 0; i < 8; ++i) {
            const float z = -40.0f + i * 8.0f;
            const float x = static_cast<float>(side) * 4.4f;
            const float y = terrain_.heightAt(x, z);
            addMesh("topiary_cone", cone, trs({x, y, z}, 0, {1, 1, 1}), hedgeDark, true,
                    WorldMaterial::Hedge);
            addMesh("topiary_stem", stem,
                    trs({x * 1.0f + static_cast<float>(side) * 1.8f, y + 0.35f, z}, 0, {1, 1, 1}),
                    {0.35f, 0.22f, 0.12f});
            addMesh("topiary_ball", ball,
                    trs({x + static_cast<float>(side) * 1.8f, y + 1.1f, z}, 0, {1, 1, 1}), hedge, true,
                    WorldMaterial::Hedge);
        }
    }

    // Central fountain pool + jets (static cylinders; renderer animates water elsewhere).
    addMesh("fountain_pool", makeCylinderMesh(5.5f, 0.45f, 32),
            trs({0, 0.1f, 0}, 0, {1, 1, 1}), stone, true, WorldMaterial::Stone);
    addBox("fountain_water", {-4.6f, 0.28f, -4.6f}, {4.6f, 0.34f, 4.6f}, water, false,
           WorldMaterial::Water);
    MeshData jet = makeCylinderMesh(0.07f, 2.8f, 6);
    for (int i = 0; i < 8; ++i) {
        const float a = i * 6.2831853f / 8.0f;
        const float r = 2.2f;
        addMesh("jet", jet, trs({std::cos(a) * r, 1.5f, std::sin(a) * r}, 0, {1, 1, 1}),
                {0.75f, 0.88f, 0.95f}, false);
    }
    addMesh("fountain_center", makeCylinderMesh(0.35f, 3.2f, 12), trs({0, 1.6f, 0}, 0, {1, 1, 1}),
            stone, true, WorldMaterial::Stone);

    // Palace façade at +Z end (simple repeating windows/columns).
    addBox("palace_body", {-28.0f, 0.0f, 48.0f}, {28.0f, 14.0f, 56.0f}, palace, true,
           WorldMaterial::Stone);
    addBox("palace_roof", {-29.0f, 14.0f, 47.5f}, {29.0f, 17.5f, 56.5f}, roof);
    for (int i = -5; i <= 5; ++i) {
        const float x = i * 4.6f;
        addBox("column", {x - 0.45f, 0.0f, 47.6f}, {x + 0.45f, 11.0f, 48.2f}, {0.92f, 0.90f, 0.84f});
        addBox("window", {x - 1.1f, 3.0f, 47.85f}, {x + 1.1f, 8.5f, 48.05f}, {0.25f, 0.35f, 0.45f},
               false);
    }
    // Plinth statues
    for (int side : {-1, 1}) {
        addBox("plinth", {side * 10.0f - 0.8f, 0, 30.0f}, {side * 10.0f + 0.8f, 1.2f, 31.6f}, stone);
        addMesh("statue", makeCylinderMesh(0.35f, 2.2f, 10),
                trs({static_cast<float>(side) * 10.0f, 2.3f, 30.8f}, 0, {1, 1, 1}), {0.75f, 0.72f, 0.68f});
    }

    // Tree rows at edges
    MeshData trunk = makeCylinderMesh(0.35f, 6.0f, 8);
    MeshData canopy = makeSphereMesh(2.8f, 12, 8);
    for (int side : {-1, 1}) {
        for (int i = 0; i < 14; ++i) {
            const float z = -50.0f + i * 7.5f;
            const float x = static_cast<float>(side) * 32.0f + hash01(static_cast<float>(side), z) * 2.0f;
            const float y = terrain_.heightAt(x, z);
            addMesh("trunk", trunk, trs({x, y + 3.0f, z}, 0, {1, 1, 1}), {0.35f, 0.22f, 0.12f});
            addMesh("canopy", canopy, trs({x, y + 7.2f, z}, 0, {1, 1, 1}), hedgeDark, true,
                    WorldMaterial::Hedge);
        }
    }

    // Fountain pool as animated water plane (small).
    desc_.hasWater = true;
    desc_.waterCenter = {0.0f, 0.32f, 0.0f};
    desc_.waterSize = 10.0f;
    desc_.waterLevel = 0.32f;
    waterDeep_ = {0.05f, 0.25f, 0.35f};
    waterShallow_ = {0.2f, 0.55f, 0.58f};
    waterGpu_.upload(makeGridMesh(desc_.waterSize, 48));

    if (desc_.photoSpots.empty()) {
        desc_.photoSpots.push_back({"Fountain court", {0.0f, 0.0f, -12.0f}, 0.0f});
        desc_.photoSpots.push_back({"Parterre east", {12.0f, 0.0f, 0.0f}, -90.0f});
    }

    buildGrass(desc_.grassCount, -30.0f, 30.0f, -48.0f, 40.0f, [&](float x, float z) {
        // Keep gravel paths and fountain clear.
        if (std::abs(x) < 3.4f && z > -55.0f && z < 42.0f) return false;
        if (std::abs(z) < 3.2f && std::abs(x) < 28.0f) return false;
        if (x * x + z * z < 6.5f * 6.5f) return false;
        return true;
    });
}

void World::buildBeach() {
    terrainMaterial_ = WorldMaterial::Sand;
    backdropColor_ = {0.72f, 0.78f, 0.85f};
    // Sea horizon ring sits low and far so it does not paint a grey wall.
    backdropGpu_.upload(makeBackdropRing(desc_.terrain.size * 0.7f, desc_.terrain.size * 1.05f, 1.2f,
                                         72, true));
    backdropColor_ = {0.55f, 0.62f, 0.70f};

    const glm::vec3 sand = desc_.terrain.baseColor;
    const glm::vec3 wood(0.45f, 0.30f, 0.18f);
    const glm::vec3 rock(0.45f, 0.44f, 0.42f);
    const glm::vec3 palmTrunk(0.42f, 0.30f, 0.16f);
    const glm::vec3 frond(0.18f, 0.45f, 0.16f);

    desc_.hasWater = true;
    if (desc_.waterSize < 10.0f) desc_.waterSize = 90.0f;
    // Keep the ocean in the −Z basin so the sandy shore stays walkable.
    if (std::abs(desc_.waterCenter.z) < 1.0f) desc_.waterCenter = {0.0f, desc_.waterLevel, -55.0f};
    waterDeep_ = {0.02f, 0.10f, 0.22f};
    waterShallow_ = {0.12f, 0.42f, 0.48f};
    waterGpu_.upload(makeGridMesh(desc_.waterSize, 96));

    // Wooden pier
    addBox("pier_deck", {-2.2f, 0.9f, -18.0f}, {2.2f, 1.15f, 8.0f}, wood);
    for (int i = 0; i < 8; ++i) {
        const float z = -16.0f + i * 3.2f;
        addBox("pier_post", {-1.8f, -0.5f, z - 0.15f}, {-1.5f, 1.0f, z + 0.15f}, wood);
        addBox("pier_post", {1.5f, -0.5f, z - 0.15f}, {1.8f, 1.0f, z + 0.15f}, wood);
    }

    // Cabana
    addBox("cabana", {10.0f, 0.0f, 6.0f}, {16.0f, 3.2f, 10.0f}, {0.85f, 0.82f, 0.72f});
    addBox("cabana_roof", {9.5f, 3.2f, 5.5f}, {16.5f, 4.0f, 10.5f}, {0.55f, 0.25f, 0.15f});

    // Rocks
    MeshData boulder = makeSphereMesh(1.0f, 10, 8);
    for (int i = 0; i < 12; ++i) {
        const float x = -22.0f + hash01(i, 2) * 14.0f;
        const float z = -8.0f + hash01(i, 5) * 18.0f;
        const float y = terrain_.heightAt(x, z);
        const float s = 0.6f + hash01(i, 9) * 1.4f;
        addMesh("rock", boulder, trs({x, y + s * 0.4f, z}, hash01(i, 1) * 360.0f, {s, s * 0.7f, s}),
                rock);
    }

    // Palm trees
    MeshData trunk = makeCylinderMesh(0.25f, 7.0f, 8);
    MeshData leaf = makeConeMesh(2.2f, 0.35f, 8);
    for (int i = 0; i < 10; ++i) {
        const float x = (i % 2 == 0 ? -1.0f : 1.0f) * (8.0f + (i / 2) * 5.0f);
        const float z = 4.0f + (i % 5) * 4.5f;
        const float y = terrain_.heightAt(x, z);
        addMesh("palm_trunk", trunk, trs({x, y + 3.5f, z}, 12.0f * (i % 3), {1, 1, 1}), palmTrunk);
        for (int f = 0; f < 6; ++f) {
            const float yaw = f * 60.0f + i * 10.0f;
            addMesh("frond", leaf,
                    trs({x, y + 7.0f, z}, yaw, {1.0f, 1.0f, 1.0f}) *
                        glm::rotate(glm::mat4(1.0f), glm::radians(-55.0f), glm::vec3(1, 0, 0)),
                    frond);
        }
    }

    if (desc_.photoSpots.empty()) {
        desc_.photoSpots.push_back({"Shore", {0.0f, 0.0f, -6.0f}, 180.0f});
        desc_.photoSpots.push_back({"Pier", {0.0f, 1.2f, -10.0f}, 180.0f});
    }

    buildGrass(std::min(desc_.grassCount, 25000), -40.0f, 40.0f, 0.0f, 40.0f,
               [&](float x, float z) { return terrain_.heightAt(x, z) > 0.6f; });
    (void)sand;
}

void World::buildForest() {
    terrainMaterial_ = WorldMaterial::Grass;
    backdropColor_ = {0.18f, 0.28f, 0.14f};
    backdropGpu_.upload(makeBackdropRing(desc_.terrain.size * 0.45f, desc_.terrain.size * 0.7f, 18.0f,
                                         64, false));

    MeshData trunk = makeCylinderMesh(0.4f, 9.0f, 8);
    MeshData canopy = makeSphereMesh(3.5f, 10, 8);
    const glm::vec3 bark(0.28f, 0.18f, 0.10f);
    const glm::vec3 leaf(0.12f, 0.32f, 0.12f);

    for (int i = 0; i < 40; ++i) {
        const float x = (hash01(i, 1) - 0.5f) * 70.0f;
        const float z = (hash01(i, 2) - 0.5f) * 70.0f;
        if (x * x + z * z < 100.0f) continue;  // clearing
        const float y = terrain_.heightAt(x, z);
        const float s = 0.75f + hash01(i, 3) * 0.6f;
        addMesh("trunk", trunk, trs({x, y + 4.5f * s, z}, 0, {s, s, s}), bark);
        addMesh("canopy", canopy, trs({x, y + 9.0f * s, z}, 0, {s, s, s}), leaf);
    }

    if (desc_.photoSpots.empty()) {
        desc_.photoSpots.push_back({"Clearing", {0.0f, 0.0f, 0.0f}, 0.0f});
    }
    buildGrass(desc_.grassCount, -35.0f, 35.0f, -35.0f, 35.0f, nullptr);
}

void World::finishBounds() {
    center_ = {0.0f, 2.0f, 0.0f};
    radius_ = desc_.terrain.size * 0.45f;
}
