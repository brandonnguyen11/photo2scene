#include "Scene.h"

#include <glm/gtc/matrix_transform.hpp>

#include <algorithm>

const std::vector<Environment>& environmentPresets() {
    static const std::vector<Environment> presets = {
        {"Studio", {0.17f, 0.18f, 0.20f}, {0.32f, 0.33f, 0.36f}, {0.12f, 0.12f, 0.13f},
         {0.55f, 0.55f, 0.57f}, {1.0f, 1.0f, 1.0f}, {1.0f, 0.97f, 0.92f}, 40.0f},
        {"Daylight", {0.24f, 0.48f, 0.85f}, {0.76f, 0.86f, 0.96f}, {0.33f, 0.36f, 0.30f},
         {0.46f, 0.50f, 0.40f}, {0.75f, 0.85f, 1.0f}, {1.0f, 0.96f, 0.86f}, 55.0f},
        {"Sunset", {0.20f, 0.15f, 0.35f}, {0.98f, 0.55f, 0.30f}, {0.20f, 0.12f, 0.10f},
         {0.45f, 0.35f, 0.30f}, {1.0f, 0.70f, 0.55f}, {1.0f, 0.62f, 0.35f}, 12.0f},
        {"Night", {0.01f, 0.02f, 0.06f}, {0.06f, 0.08f, 0.16f}, {0.02f, 0.02f, 0.03f},
         {0.15f, 0.16f, 0.20f}, {0.40f, 0.50f, 0.90f}, {0.60f, 0.70f, 1.0f}, 30.0f},
    };
    return presets;
}

void Scene::init() {
    ground_.name = "Ground";
    ground_.mesh.upload(makePlaneMesh());
    ground_.castsShadow = false;
    ground_.checker = true;

    gizmo_.upload(makeCubeMesh(1.0f));

    photo_.name = "Reconstruction";
    setEnvironment(0);
    updateGroundTransform();
}

void Scene::shutdown() {
    photo_.mesh.destroy();
    ground_.mesh.destroy();
    gizmo_.destroy();
}

void Scene::setReconstruction(const MeshData& mesh, GLuint texture) {
    photo_.mesh.upload(mesh);
    photo_.texture = texture;
    photo_.visible = true;
    boundsMin_ = mesh.boundsMin;
    boundsMax_ = mesh.boundsMax;
    center_ = (boundsMin_ + boundsMax_) * 0.5f;
    radius_ = std::max(glm::length(boundsMax_ - boundsMin_) * 0.5f, 0.05f);
    updateGroundTransform();
}

void Scene::clearReconstruction() {
    photo_.mesh.destroy();
    photo_.texture = 0;
}

void Scene::setEnvironment(int index) {
    const int n = static_cast<int>(environmentPresets().size());
    environmentIndex_ = std::clamp(index, 0, n - 1);
    ground_.baseColor = environment().groundColor;
}

const Environment& Scene::environment() const { return environmentPresets()[environmentIndex_]; }

void Scene::updateGroundTransform() {
    // Ground sits just under the lowest point of the reconstruction and
    // extends well beyond it so shadows have somewhere to land.
    const glm::vec3 extent = boundsMax_ - boundsMin_;
    const float halfSize = std::max({extent.x, extent.y, extent.z, 0.5f}) * 2.5f;
    ground_.model = glm::translate(glm::mat4(1.0f),
                                   glm::vec3(center_.x, boundsMin_.y - 0.002f, center_.z)) *
                    glm::scale(glm::mat4(1.0f), glm::vec3(halfSize, 1.0f, halfSize));
}

size_t Scene::gpuBytes() const {
    return photo_.mesh.gpuBytes() + ground_.mesh.gpuBytes() + gizmo_.gpuBytes();
}
