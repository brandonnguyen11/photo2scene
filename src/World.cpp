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
    const glm::vec3 water(0.25f, 0.55f, 0.62f);

    // Central gravel axis (mirror-symmetric about Z).
    addBox("path_main", {-3.2f, -0.02f, -55.0f}, {3.2f, 0.03f, 42.0f}, gravel, false,
           WorldMaterial::Gravel);
    addBox("path_cross", {-28.0f, -0.02f, -3.0f}, {28.0f, 0.03f, 3.0f}, gravel, false,
           WorldMaterial::Gravel);

    // Parterre hedges — box body + soft rounded cap so they feel less brick-like.
    auto hedgeBox = [&](float x0, float z0, float x1, float z1, float h = 0.85f) {
        addBox("hedge", {x0, 0.0f, z0}, {x1, h * 0.82f, z1}, hedge, true, WorldMaterial::Hedge);
        const float mx = (x0 + x1) * 0.5f;
        const float mz = (z0 + z1) * 0.5f;
        const float hx = std::max(0.15f, (x1 - x0) * 0.5f);
        const float hz = std::max(0.15f, (z1 - z0) * 0.5f);
        // Capsule-ish top: flattened sphere stretched to the hedge footprint.
        addMesh("hedge_cap", makeSphereMesh(1.0f, 10, 7),
                trs({mx, h * 0.75f, mz}, 0, {hx, h * 0.35f, hz}), hedge, true,
                WorldMaterial::Hedge);
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

    // ---- Central fountain (Apollo basin) ----
    addMesh("fountain_base", makeCylinderMesh(6.2f, 0.55f, 36), trs({0, 0.15f, 0}, 0, {1, 1, 1}),
            stone, true, WorldMaterial::Stone);
    addMesh("fountain_rim", makeCylinderMesh(5.7f, 0.35f, 36), trs({0, 0.55f, 0}, 0, {1, 1, 1}),
            stone, true, WorldMaterial::Stone);
    addMesh("fountain_mid", makeCylinderMesh(2.4f, 0.4f, 24), trs({0, 1.4f, 0}, 0, {1, 1, 1}), stone,
            true, WorldMaterial::Stone);
    addMesh("fountain_top", makeCylinderMesh(0.9f, 0.3f, 16), trs({0, 2.5f, 0}, 0, {1, 1, 1}), stone,
            true, WorldMaterial::Stone);
    addMesh("fountain_stem", makeTaperedTrunk(0.35f, 0.18f, 2.6f, 12), trs({0, 0.4f, 0}, 0, {1, 1, 1}),
            stone, true, WorldMaterial::Stone);
    (void)water;

    // Secondary Latona-style basin further down the axis toward the canal.
    addMesh("latona_rim", makeCylinderMesh(4.0f, 0.4f, 28), trs({0, 0.2f, -28.0f}, 0, {1, 1, 1}),
            stone, true, WorldMaterial::Stone);
    addBox("latona_water", {-3.4f, 0.35f, -31.4f}, {3.4f, 0.42f, -24.6f}, {0.22f, 0.48f, 0.55f}, false,
           WorldMaterial::Water);
    addMesh("latona_center", makeCylinderMesh(0.9f, 1.2f, 12), trs({0, 0.9f, -28.0f}, 0, {1, 1, 1}),
            stone, true, WorldMaterial::Stone);

    // Grand Canal — long reflecting pool toward −Z (Versailles signature).
    addBox("canal_rim", {-5.5f, -0.05f, -72.0f}, {5.5f, 0.35f, -38.0f}, stone, true,
           WorldMaterial::Stone);
    addBox("canal_water", {-4.8f, 0.22f, -71.0f}, {4.8f, 0.30f, -39.0f}, {0.15f, 0.38f, 0.48f}, false,
           WorldMaterial::Water);

    // Extra gravel alleys + cross walks so the grounds aren't empty lawn.
    addBox("path_upper", {-3.0f, -0.02f, 20.0f}, {3.0f, 0.03f, 46.0f}, gravel, false,
           WorldMaterial::Gravel);
    addBox("path_diag_e", {3.0f, -0.02f, 18.0f}, {22.0f, 0.03f, 20.5f}, gravel, false,
           WorldMaterial::Gravel);
    addBox("path_diag_w", {-22.0f, -0.02f, 18.0f}, {-3.0f, 0.03f, 20.5f}, gravel, false,
           WorldMaterial::Gravel);
    addBox("path_south", {-2.5f, -0.02f, -55.0f}, {2.5f, 0.03f, -38.0f}, gravel, false,
           WorldMaterial::Gravel);
    addBox("path_east_allée", {26.0f, -0.02f, -40.0f}, {29.0f, 0.03f, 40.0f}, gravel, false,
           WorldMaterial::Gravel);
    addBox("path_west_allée", {-29.0f, -0.02f, -40.0f}, {-26.0f, 0.03f, 40.0f}, gravel, false,
           WorldMaterial::Gravel);

    // Flower parterre beds (color blocks between hedges).
    const glm::vec3 flowerA(0.72f, 0.22f, 0.28f);
    const glm::vec3 flowerB(0.85f, 0.72f, 0.25f);
    const glm::vec3 flowerC(0.55f, 0.35f, 0.65f);
    for (int side : {-1, 1}) {
        const float s = static_cast<float>(side);
        addBox("flowers", {s * 11.0f, 0.02f, -14.0f}, {s * 16.0f, 0.18f, -11.0f}, flowerA, false,
               WorldMaterial::Plain);
        addBox("flowers", {s * 11.0f, 0.02f, 5.0f}, {s * 16.0f, 0.18f, 8.0f}, flowerB, false,
               WorldMaterial::Plain);
        addBox("flowers", {s * 13.0f, 0.02f, -7.0f}, {s * 15.5f, 0.18f, -5.0f}, flowerC, false,
               WorldMaterial::Plain);
    }

    // ---- Versailles-inspired palace façade ----
    const glm::vec3 cream(0.91f, 0.87f, 0.78f);
    const glm::vec3 trim(0.96f, 0.94f, 0.88f);
    const glm::vec3 gold(0.78f, 0.62f, 0.28f);
    const glm::vec3 glass(0.18f, 0.28f, 0.40f);
    const glm::vec3 slate(0.28f, 0.30f, 0.34f);
    const float facadeZ = 48.4f;

    // Deep massing: center corps + projecting avant-corps + lower wings + end pavilions.
    addBox("corps", {-18.0f, 0.0f, facadeZ}, {18.0f, 14.0f, 56.5f}, cream, true,
           WorldMaterial::Stucco);
    addBox("avant", {-7.0f, 0.0f, facadeZ - 1.4f}, {7.0f, 15.2f, facadeZ + 0.2f}, cream, true,
           WorldMaterial::Stucco);
    addBox("wing_l", {-36.0f, 0.0f, facadeZ + 0.8f}, {-17.5f, 11.5f, 57.0f}, cream, true,
           WorldMaterial::Stucco);
    addBox("wing_r", {17.5f, 0.0f, facadeZ + 0.8f}, {36.0f, 11.5f, 57.0f}, cream, true,
           WorldMaterial::Stucco);
    addBox("pav_l", {-40.0f, 0.0f, facadeZ}, {-35.0f, 13.5f, 57.5f}, cream, true,
           WorldMaterial::Stucco);
    addBox("pav_r", {35.0f, 0.0f, facadeZ}, {40.0f, 13.5f, 57.5f}, cream, true,
           WorldMaterial::Stucco);

    // Rusticated ground floor (banded stone).
    addBox("rust_base", {-40.5f, 0.0f, facadeZ - 1.6f}, {40.5f, 3.2f, 57.8f}, stone, true,
           WorldMaterial::Stone);
    for (int b = 0; b < 4; ++b) {
        const float y0 = 0.3f + b * 0.7f;
        addBox("rust_band", {-40.3f, y0, facadeZ - 1.65f}, {40.3f, y0 + 0.18f, facadeZ - 1.45f},
               trim, false, WorldMaterial::Stone);
    }

    // Grand terrace steps up to the palace.
    for (int step = 0; step < 5; ++step) {
        const float z0 = 44.0f + step * 0.85f;
        const float y0 = step * 0.28f;
        addBox("stair", {-12.0f - step * 0.4f, y0, z0}, {12.0f + step * 0.4f, y0 + 0.3f, z0 + 0.9f},
               stone, true, WorldMaterial::Stone);
    }
    addBox("terrace", {-28.0f, 1.35f, 46.5f}, {28.0f, 1.55f, facadeZ - 1.5f}, stone, true,
           WorldMaterial::Stone);

    // Pilasters / engaged columns across the façade.
    MeshData pilaster = makeTaperedTrunk(0.38f, 0.32f, 9.8f, 10);
    auto addWindowBay = [&](float x, float zFront, float sillY, float winH, bool tall) {
        addMesh("pilaster", pilaster, trs({x - 1.55f, 3.3f, zFront}, 0, {1, 1, 1}), trim, true,
                WorldMaterial::Stone);
        addMesh("pilaster", pilaster, trs({x + 1.55f, 3.3f, zFront}, 0, {1, 1, 1}), trim, true,
                WorldMaterial::Stone);
        addBox("cap", {x - 1.75f, 12.9f, zFront - 0.15f}, {x + 1.75f, 13.3f, zFront + 0.35f}, trim,
               true, WorldMaterial::Stone);
        // Window niche + glass + gold mullion hint
        addBox("niche", {x - 1.15f, sillY, zFront - 0.05f}, {x + 1.15f, sillY + winH, zFront + 0.25f},
               stone, false, WorldMaterial::Stone);
        addBox("glass", {x - 1.0f, sillY + 0.15f, zFront - 0.12f},
               {x + 1.0f, sillY + winH - 0.15f, zFront + 0.02f}, glass, false, WorldMaterial::Plain);
        addBox("mullion", {x - 0.05f, sillY + 0.15f, zFront - 0.14f},
               {x + 0.05f, sillY + winH - 0.15f, zFront - 0.08f}, gold, false, WorldMaterial::Plain);
        // Pediment above tall windows
        if (tall) {
            addMesh("win_ped", makeConeMesh(1.4f, 0.7f, 3),
                    trs({x, sillY + winH + 0.15f, zFront}, 0, {1.0f, 0.45f, 0.2f}), trim, true,
                    WorldMaterial::Stone);
        }
    };

    // Central avant-corps: 5 tall bays
    for (int i = -2; i <= 2; ++i) addWindowBay(i * 2.7f, facadeZ - 1.45f, 3.6f, 7.2f, true);
    // Main corps side bays
    for (int i = -5; i <= 5; ++i) {
        if (std::abs(i) <= 2) continue;
        addWindowBay(i * 2.9f, facadeZ - 0.15f, 3.8f, 6.2f, false);
    }
    // Wing windows (two floors)
    for (int side : {-1, 1}) {
        for (int i = 0; i < 5; ++i) {
            const float x = static_cast<float>(side) * (20.0f + i * 3.0f);
            addWindowBay(x, facadeZ + 0.65f, 3.6f, 3.2f, false);
            addBox("win2", {x - 0.9f, 8.0f, facadeZ + 0.55f}, {x + 0.9f, 10.4f, facadeZ + 0.85f},
                   glass, false, WorldMaterial::Plain);
        }
    }

    // Entablature + cornice stack
    addBox("entablature", {-40.5f, 13.2f, facadeZ - 1.7f}, {40.5f, 14.0f, 57.6f}, trim, true,
           WorldMaterial::Stone);
    addBox("cornice", {-41.0f, 14.0f, facadeZ - 1.9f}, {41.0f, 14.7f, 57.9f}, trim, true,
           WorldMaterial::Stone);

    // Mansard roofs + dormers + chimneys
    addBox("roof_c", {-19.0f, 14.5f, facadeZ - 0.5f}, {19.0f, 18.5f, 57.0f}, slate, true,
           WorldMaterial::Plain);
    addBox("roof_av", {-8.0f, 15.0f, facadeZ - 1.8f}, {8.0f, 19.5f, facadeZ + 2.0f}, slate, true,
           WorldMaterial::Plain);
    addBox("roof_wl", {-37.0f, 11.5f, facadeZ}, {-17.0f, 15.5f, 57.5f}, slate, true,
           WorldMaterial::Plain);
    addBox("roof_wr", {17.0f, 11.5f, facadeZ}, {37.0f, 15.5f, 57.5f}, slate, true,
           WorldMaterial::Plain);
    addBox("roof_pl", {-41.0f, 13.5f, facadeZ - 0.5f}, {-34.5f, 17.5f, 58.0f}, slate, true,
           WorldMaterial::Plain);
    addBox("roof_pr", {34.5f, 13.5f, facadeZ - 0.5f}, {41.0f, 17.5f, 58.0f}, slate, true,
           WorldMaterial::Plain);
    addMesh("pediment", makeConeMesh(8.0f, 4.0f, 4),
            trs({0.0f, 19.3f, facadeZ - 1.6f}, 0, {1.0f, 0.5f, 0.22f}), slate, true,
            WorldMaterial::Plain);
    for (int i = -3; i <= 3; ++i) {
        const float x = i * 4.5f;
        addBox("dormer", {x - 0.7f, 16.2f, facadeZ - 0.7f}, {x + 0.7f, 17.8f, facadeZ - 0.2f}, cream,
               true, WorldMaterial::Stucco);
        addBox("dormer_glass", {x - 0.45f, 16.4f, facadeZ - 0.75f},
               {x + 0.45f, 17.5f, facadeZ - 0.55f}, glass, false, WorldMaterial::Plain);
    }
    for (float x : {-14.0f, -6.0f, 6.0f, 14.0f, -32.0f, 32.0f}) {
        addBox("chimney", {x - 0.45f, 17.0f, 52.0f}, {x + 0.45f, 21.0f, 53.2f}, stone, true,
               WorldMaterial::Stone);
    }

    // Roof balustrade + statues
    for (int i = -16; i <= 16; ++i) {
        const float x = i * 2.4f;
        addMesh("roof_bal", makeCylinderMesh(0.1f, 0.85f, 6),
                trs({x, 15.1f, facadeZ - 1.85f}, 0, {1, 1, 1}), trim, true, WorldMaterial::Stone);
    }
    addBox("roof_rail", {-39.0f, 15.45f, facadeZ - 1.95f}, {39.0f, 15.65f, facadeZ - 1.75f}, trim,
           true, WorldMaterial::Stone);
    for (int i = -3; i <= 3; ++i) {
        const float x = i * 8.0f;
        addMesh("roof_statue", makeTaperedTrunk(0.22f, 0.14f, 1.4f, 6),
                trs({x, 15.7f, facadeZ - 1.9f}, 0, {1, 1, 1}), {0.82f, 0.80f, 0.74f}, true,
                WorldMaterial::Stone);
    }

    // Terrace balustrade
    for (int i = -14; i <= 14; ++i) {
        addMesh("terr_bal", makeCylinderMesh(0.12f, 0.95f, 6),
                trs({static_cast<float>(i) * 2.0f, 1.7f, 46.3f}, 0, {1, 1, 1}), trim, true,
                WorldMaterial::Stone);
    }
    addBox("terr_rail", {-28.5f, 2.1f, 46.15f}, {28.5f, 2.3f, 46.45f}, trim, true,
           WorldMaterial::Stone);

    // ---- Side orangery / pavilion buildings ----
    auto buildPavilion = [&](float cx, float cz, float yaw) {
        const glm::mat4 R = glm::rotate(glm::mat4(1.0f), glm::radians(yaw), {0, 1, 0});
        auto localBox = [&](const char* name, glm::vec3 a, glm::vec3 b, const glm::vec3& col,
                            WorldMaterial mat) {
            const glm::vec3 c = (a + b) * 0.5f;
            const glm::vec3 h = (b - a) * 0.5f;
            MeshData box = makeBoxMesh(-h, h);
            addMesh(name, box, glm::translate(glm::mat4(1.0f), glm::vec3(cx, 0, cz)) * R *
                                       glm::translate(glm::mat4(1.0f), c),
                    col, true, mat);
        };
        localBox("pav_body", {-6.0f, 0.0f, -4.0f}, {6.0f, 6.5f, 4.0f}, cream, WorldMaterial::Stucco);
        localBox("pav_base", {-6.3f, 0.0f, -4.3f}, {6.3f, 1.0f, 4.3f}, stone, WorldMaterial::Stone);
        localBox("pav_roof", {-6.8f, 6.3f, -4.6f}, {6.8f, 8.2f, 4.6f}, slate, WorldMaterial::Plain);
        localBox("pav_door", {-1.2f, 1.0f, -4.15f}, {1.2f, 4.2f, -3.85f}, glass, WorldMaterial::Plain);
        for (float x : {-3.8f, 3.8f}) {
            localBox("pav_win", {x - 0.9f, 2.2f, -4.15f}, {x + 0.9f, 4.8f, -3.9f}, glass,
                     WorldMaterial::Plain);
        }
    };
    buildPavilion(-48.0f, 5.0f, 90.0f);
    buildPavilion(48.0f, 5.0f, -90.0f);

    // Garden temples / follies at the far corners.
    auto buildTemple = [&](float cx, float cz) {
        const float y = terrain_.heightAt(cx, cz);
        addMesh("temple_plat", makeCylinderMesh(3.2f, 0.4f, 16), trs({cx, y + 0.2f, cz}, 0, {1, 1, 1}),
                stone, true, WorldMaterial::Stone);
        MeshData tcol = makeTaperedTrunk(0.22f, 0.18f, 3.8f, 8);
        for (int i = 0; i < 6; ++i) {
            const float a = i * 60.0f;
            const float rad = 2.2f;
            addMesh("temple_col", tcol,
                    trs({cx + std::cos(glm::radians(a)) * rad, y + 0.4f,
                         cz + std::sin(glm::radians(a)) * rad},
                        a, {1, 1, 1}),
                    trim, true, WorldMaterial::Stone);
        }
        addMesh("temple_dome", makeSphereMesh(2.4f, 14, 10),
                trs({cx, y + 5.2f, cz}, 0, {1.0f, 0.55f, 1.0f}), slate, true, WorldMaterial::Plain);
    };
    buildTemple(-42.0f, -45.0f);
    buildTemple(42.0f, -45.0f);
    buildTemple(-38.0f, 28.0f);
    buildTemple(38.0f, 28.0f);

    // Statue walk along both sides of the main axis.
    for (int side : {-1, 1}) {
        for (int i = 0; i < 8; ++i) {
            const float z = -36.0f + i * 9.0f;
            const float x = static_cast<float>(side) * 8.5f;
            const float y = terrain_.heightAt(x, z);
            addBox("plinth", {x - 0.7f, y, z - 0.7f}, {x + 0.7f, y + 1.1f, z + 0.7f}, stone, true,
                   WorldMaterial::Stone);
            addMesh("statue", makeTaperedTrunk(0.28f, 0.16f, 1.8f, 8),
                    trs({x, y + 1.1f, z}, 0, {1, 1, 1}), {0.80f, 0.77f, 0.72f}, true,
                    WorldMaterial::Stone);
            addMesh("statue_h", makeSphereMesh(0.25f, 8, 6), trs({x, y + 3.1f, z}, 0, {1, 1, 1}),
                    {0.82f, 0.79f, 0.74f}, true, WorldMaterial::Stone);
        }
    }

    // Stone benches along the east/west allées.
    for (int side : {-1, 1}) {
        for (int i = 0; i < 6; ++i) {
            const float z = -30.0f + i * 12.0f;
            const float x = static_cast<float>(side) * 27.5f;
            const float y = terrain_.heightAt(x, z);
            addBox("bench", {x - 1.2f, y + 0.35f, z - 0.35f}, {x + 1.2f, y + 0.55f, z + 0.35f}, stone,
                   true, WorldMaterial::Stone);
            addBox("bench_leg", {x - 1.0f, y, z - 0.3f}, {x - 0.7f, y + 0.35f, z + 0.3f}, stone, true,
                   WorldMaterial::Stone);
            addBox("bench_leg", {x + 0.7f, y, z - 0.3f}, {x + 1.0f, y + 0.35f, z + 0.3f}, stone, true,
                   WorldMaterial::Stone);
        }
    }

    // Bosquet groves (dense tree clusters) filling the outer plane.
    MeshData trunk = makeTaperedTrunk(0.45f, 0.18f, 7.0f, 10);
    MeshData canopyA = makeSphereMesh(2.6f, 14, 10);
    MeshData canopyB = makeSphereMesh(2.0f, 12, 9);
    auto plantTree = [&](float x, float z, float s) {
        const float y = terrain_.heightAt(x, z);
        const float lean = (hash01(z, x) - 0.5f) * 10.0f;
        addMesh("trunk", trunk, trs({x, y, z}, lean, {s, s, s}), {0.32f, 0.20f, 0.11f}, true,
                WorldMaterial::Bark);
        addMesh("canopy_a", canopyA, trs({x, y + 7.0f * s, z}, 0, {1.1f * s, 0.85f * s, s}),
                hedgeDark, true, WorldMaterial::Hedge);
        addMesh("canopy_b", canopyB,
                trs({x + 0.8f * s, y + 6.2f * s, z - 0.5f * s}, 20.0f, {0.85f * s, 0.7f * s, 0.9f * s}),
                hedge, true, WorldMaterial::Hedge);
    };
    // Formal rows along the edges
    for (int side : {-1, 1}) {
        for (int i = 0; i < 16; ++i) {
            const float z = -55.0f + i * 7.0f;
            const float x = static_cast<float>(side) * 34.0f + hash01(static_cast<float>(side), z);
            plantTree(x, z, 0.9f + hash01(i, side) * 0.25f);
        }
    }
    // Outer bosquet fill (scattered)
    for (int i = 0; i < 55; ++i) {
        const float x = (hash01(i, 1.0f) - 0.5f) * 130.0f;
        const float z = (hash01(i, 2.0f) - 0.5f) * 130.0f;
        // Keep center formal garden + palace + canal clear.
        if (std::abs(x) < 32.0f && z > -75.0f && z < 48.0f) continue;
        if (z > 42.0f && std::abs(x) < 45.0f) continue;
        plantTree(x, z, 0.85f + hash01(i, 3.0f) * 0.4f);
    }

    // Circular pool water sitting inside the fountain rim (not a square grid).
    desc_.hasWater = true;
    desc_.waterCircular = true;
    desc_.waterRadius = 5.15f;
    desc_.waterCenter = {0.0f, 0.78f, 0.0f};
    desc_.waterSize = desc_.waterRadius * 2.0f;
    desc_.waterLevel = 0.78f;
    desc_.waterWaveScale = 0.28f;
    desc_.waterShoreZ = 1e6f;
    waterDeep_ = {0.08f, 0.32f, 0.40f};
    waterShallow_ = {0.28f, 0.60f, 0.62f};
    waterGpu_.upload(makeDiscMesh(desc_.waterRadius, 28, 64));

    if (desc_.photoSpots.empty()) {
        desc_.photoSpots.push_back({"Fountain court", {0.0f, 0.0f, -12.0f}, 0.0f});
        desc_.photoSpots.push_back({"Parterre east", {12.0f, 0.0f, 0.0f}, -90.0f});
        desc_.photoSpots.push_back({"Palace terrace", {0.0f, 1.5f, 42.0f}, 180.0f});
        desc_.photoSpots.push_back({"Canal view", {0.0f, 0.0f, -42.0f}, 0.0f});
    }

    buildGrass(desc_.grassCount, -55.0f, 55.0f, -70.0f, 45.0f, [&](float x, float z) {
        if (std::abs(x) < 3.4f && z > -75.0f && z < 46.0f) return false;
        if (std::abs(z) < 3.2f && std::abs(x) < 28.0f) return false;
        if (std::abs(x) > 25.5f && std::abs(x) < 29.5f && z > -40.0f && z < 40.0f) return false;
        if (x * x + z * z < 6.5f * 6.5f) return false;
        if (z > 43.0f && std::abs(x) < 42.0f) return false;
        return true;
    });
}

void World::buildBeach() {
    terrainMaterial_ = WorldMaterial::Sand;
    backdropGpu_.upload(makeBackdropRing(desc_.terrain.size * 0.7f, desc_.terrain.size * 1.05f, 1.2f,
                                         72, true));
    backdropColor_ = {0.55f, 0.62f, 0.70f};

    const glm::vec3 wood(0.48f, 0.32f, 0.18f);
    const glm::vec3 woodDark(0.32f, 0.20f, 0.11f);
    const glm::vec3 rockCol(0.42f, 0.40f, 0.38f);
    const glm::vec3 palmTrunk(0.45f, 0.32f, 0.16f);
    const glm::vec3 frondCol(0.22f, 0.48f, 0.18f);
    const glm::vec3 thatch(0.55f, 0.40f, 0.18f);
    const glm::vec3 canvas(0.92f, 0.88f, 0.78f);

    desc_.hasWater = true;
    desc_.waterCircular = false;
    // Keep the ocean sheet in the −Z basin and clip anything inland of the shore.
    if (desc_.waterSize < 10.0f) desc_.waterSize = 100.0f;
    desc_.waterLevel = 0.12f;  // sit above the submerged shelf, under the dunes
    desc_.waterCenter = {0.0f, desc_.waterLevel, -58.0f};
    desc_.waterWaveScale = 0.85f;
    desc_.waterShoreZ = -6.0f;  // discard water that climbs onto the sand
    desc_.waterRadius = 0.0f;
    waterDeep_ = {0.02f, 0.10f, 0.22f};
    waterShallow_ = {0.15f, 0.45f, 0.50f};
    waterGpu_.upload(makeGridMesh(desc_.waterSize, 128));

    // Pier built from individual planks + posts + railings.
    const float pierY = 1.05f;
    for (int i = 0; i < 22; ++i) {
        const float z0 = -18.0f + i * 1.15f;
        addBox("plank", {-2.0f, pierY, z0}, {2.0f, pierY + 0.1f, z0 + 1.0f}, wood, true,
               WorldMaterial::Wood);
    }
    // Side stringers
    addBox("stringer_l", {-2.15f, pierY - 0.25f, -18.0f}, {-1.95f, pierY, 7.0f}, woodDark, true,
           WorldMaterial::Wood);
    addBox("stringer_r", {1.95f, pierY - 0.25f, -18.0f}, {2.15f, pierY, 7.0f}, woodDark, true,
           WorldMaterial::Wood);
    MeshData post = makeTaperedTrunk(0.16f, 0.12f, 2.4f, 8);
    for (int i = 0; i < 10; ++i) {
        const float z = -16.5f + i * 2.6f;
        const float yL = terrain_.heightAt(-1.85f, z);
        const float yR = terrain_.heightAt(1.85f, z);
        addMesh("post_l", post, trs({-1.85f, yL, z}, 0, {1, 1, 1}), woodDark, true,
                WorldMaterial::Wood);
        addMesh("post_r", post, trs({1.85f, yR, z}, 0, {1, 1, 1}), woodDark, true,
                WorldMaterial::Wood);
        // Cross brace every other bay
        if (i % 2 == 0 && i < 9) {
            addBox("brace", {-1.7f, pierY - 0.55f, z}, {1.7f, pierY - 0.4f, z + 0.12f}, woodDark, true,
                   WorldMaterial::Wood);
        }
    }
    // Railings
    for (int side : {-1, 1}) {
        const float x = static_cast<float>(side) * 1.95f;
        addBox("rail", {x - 0.06f, pierY + 0.85f, -17.5f}, {x + 0.06f, pierY + 0.98f, 6.5f}, wood,
               true, WorldMaterial::Wood);
        for (int i = 0; i < 16; ++i) {
            const float z = -17.0f + i * 1.5f;
            addBox("picket", {x - 0.05f, pierY + 0.1f, z - 0.05f},
                   {x + 0.05f, pierY + 0.9f, z + 0.05f}, wood, true, WorldMaterial::Wood);
        }
    }

    // Open cabana / beach hut with posts and peaked thatch.
    const float cx = 12.0f, cz = 8.0f;
    const float cy = terrain_.heightAt(cx, cz);
    MeshData hutPost = makeTaperedTrunk(0.14f, 0.1f, 3.2f, 8);
    for (int corner = 0; corner < 4; ++corner) {
        const float ox = (corner % 2 == 0 ? -2.4f : 2.4f);
        const float oz = (corner < 2 ? -1.8f : 1.8f);
        addMesh("hut_post", hutPost, trs({cx + ox, cy, cz + oz}, 0, {1, 1, 1}), woodDark, true,
                WorldMaterial::Wood);
    }
    // Floor platform
    addBox("hut_floor", {cx - 2.6f, cy + 0.15f, cz - 2.0f}, {cx + 2.6f, cy + 0.3f, cz + 2.0f}, wood,
           true, WorldMaterial::Wood);
    // Low half-walls
    addBox("hut_wall_b", {cx - 2.5f, cy + 0.3f, cz + 1.7f}, {cx + 2.5f, cy + 1.5f, cz + 1.9f}, canvas,
           true, WorldMaterial::Stucco);
    addBox("hut_wall_l", {cx - 2.6f, cy + 0.3f, cz - 1.8f}, {cx - 2.4f, cy + 1.5f, cz + 1.8f}, canvas,
           true, WorldMaterial::Stucco);
    addBox("hut_wall_r", {cx + 2.4f, cy + 0.3f, cz - 1.8f}, {cx + 2.6f, cy + 1.5f, cz + 1.8f}, canvas,
           true, WorldMaterial::Stucco);
    // Peaked thatch roof (two slanted slabs + ridge)
    addMesh("thatch_a", makeBoxMesh({-3.2f, -0.08f, -2.4f}, {3.2f, 0.08f, 2.4f}),
            glm::translate(glm::mat4(1.0f), {cx, cy + 3.4f, cz}) *
                glm::rotate(glm::mat4(1.0f), glm::radians(-28.0f), {1, 0, 0}) *
                glm::translate(glm::mat4(1.0f), {0, 0, -1.1f}),
            thatch, true, WorldMaterial::Wood);
    addMesh("thatch_b", makeBoxMesh({-3.2f, -0.08f, -2.4f}, {3.2f, 0.08f, 2.4f}),
            glm::translate(glm::mat4(1.0f), {cx, cy + 3.4f, cz}) *
                glm::rotate(glm::mat4(1.0f), glm::radians(28.0f), {1, 0, 0}) *
                glm::translate(glm::mat4(1.0f), {0, 0, 1.1f}),
            thatch, true, WorldMaterial::Wood);
    addBox("ridge", {cx - 3.0f, cy + 4.35f, cz - 0.15f}, {cx + 3.0f, cy + 4.55f, cz + 0.15f},
           woodDark, true, WorldMaterial::Wood);

    // Irregular rocks clustered near the waterline.
    for (int i = 0; i < 16; ++i) {
        const float x = -24.0f + hash01(i, 2.0f) * 18.0f + (i % 3) * 3.0f;
        const float z = -10.0f + hash01(i, 5.0f) * 14.0f;
        const float y = terrain_.heightAt(x, z);
        const float s = 0.7f + hash01(i, 9.0f) * 1.6f;
        MeshData boulder = makeRockMesh(1.0f, 12, 9, 1.0f + i * 0.37f);
        addMesh("rock", boulder,
                trs({x, y + s * 0.15f, z}, hash01(i, 1.0f) * 360.0f, {s, s * 0.65f, s * 0.9f}),
                rockCol * (0.85f + hash01(i, 3.0f) * 0.25f), true, WorldMaterial::Stone);
    }

    // Palms with tapered trunks and drooping fronds.
    MeshData trunk = makeTaperedTrunk(0.28f, 0.12f, 8.5f, 10);
    MeshData frond = makeFrondMesh(3.8f, 0.55f);
    for (int i = 0; i < 9; ++i) {
        const float x = (i % 2 == 0 ? -1.0f : 1.0f) * (7.0f + (i / 2) * 4.5f);
        const float z = 2.0f + (i % 5) * 4.0f;
        const float y = terrain_.heightAt(x, z);
        const float bend = 8.0f + hash01(i, 4.0f) * 14.0f;
        addMesh("palm_trunk", trunk, trs({x, y, z}, bend, {1, 1, 1}), palmTrunk, true,
                WorldMaterial::Bark);
        const float tipY = y + 8.0f;
        for (int f = 0; f < 8; ++f) {
            const float yaw = f * 45.0f + i * 12.0f;
            const float pitch = -35.0f - hash01(f, i) * 25.0f;
            addMesh("frond", frond,
                    trs({x, tipY, z}, yaw, {1, 1, 1}) *
                        glm::rotate(glm::mat4(1.0f), glm::radians(pitch), {0, 0, 1}),
                    frondCol, false, WorldMaterial::Hedge);
        }
    }

    if (desc_.photoSpots.empty()) {
        desc_.photoSpots.push_back({"Shore", {0.0f, 0.0f, -6.0f}, 180.0f});
        desc_.photoSpots.push_back({"Pier", {0.0f, 1.2f, -10.0f}, 180.0f});
    }

    buildGrass(std::min(desc_.grassCount, 28000), -40.0f, 40.0f, 2.0f, 45.0f,
               [&](float x, float z) { return terrain_.heightAt(x, z) > 1.0f; });
}

void World::buildForest() {
    terrainMaterial_ = WorldMaterial::Grass;
    backdropColor_ = {0.18f, 0.28f, 0.14f};
    backdropGpu_.upload(makeBackdropRing(desc_.terrain.size * 0.45f, desc_.terrain.size * 0.7f, 18.0f,
                                         64, false));

    MeshData trunk = makeTaperedTrunk(0.5f, 0.18f, 10.0f, 10);
    MeshData canopyA = makeSphereMesh(3.2f, 12, 9);
    MeshData canopyB = makeSphereMesh(2.4f, 11, 8);
    const glm::vec3 bark(0.28f, 0.18f, 0.10f);
    const glm::vec3 leaf(0.14f, 0.36f, 0.14f);
    const glm::vec3 leafDark(0.10f, 0.26f, 0.10f);

    for (int i = 0; i < 45; ++i) {
        const float x = (hash01(i, 1) - 0.5f) * 70.0f;
        const float z = (hash01(i, 2) - 0.5f) * 70.0f;
        if (x * x + z * z < 100.0f) continue;  // clearing
        const float y = terrain_.heightAt(x, z);
        const float s = 0.75f + hash01(i, 3) * 0.6f;
        addMesh("trunk", trunk, trs({x, y, z}, hash01(i, 4) * 20.0f, {s, s, s}), bark, true,
                WorldMaterial::Bark);
        addMesh("canopy_a", canopyA, trs({x, y + 9.0f * s, z}, 0, {s, s * 0.85f, s}), leaf, true,
                WorldMaterial::Hedge);
        addMesh("canopy_b", canopyB,
                trs({x + 1.2f * s, y + 7.5f * s, z - 0.8f * s}, 25.0f, {s * 0.8f, s * 0.7f, s * 0.8f}),
                leafDark, true, WorldMaterial::Hedge);
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
