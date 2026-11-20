#pragma once
// Scene.h — scene objects and virtual environment (Member 1 + 3.8 Environment)

#include "Mesh.h"
#include "MeshData.h"

#include <glad/gl.h>
#include <glm/glm.hpp>

#include <string>
#include <vector>

struct Environment {
    const char* name;
    glm::vec3 skyTop;      // sRGB
    glm::vec3 skyHorizon;  // sRGB
    glm::vec3 skyBottom;   // sRGB (below the horizon)
    glm::vec3 groundColor; // sRGB
    glm::vec3 ambient;     // suggested ambient light color (sRGB)
    glm::vec3 sunColor;    // suggested sun color (sRGB)
    float sunElevationDeg; // suggested sun elevation
};

const std::vector<Environment>& environmentPresets();

struct SceneObject {
    std::string name;
    Mesh mesh;
    glm::mat4 model{1.0f};
    bool visible = true;
    bool castsShadow = true;
    GLuint texture = 0;          // 0 = use baseColor
    glm::vec3 baseColor{0.8f};   // sRGB
    bool checker = false;        // procedural checker pattern (ground)
};

class Scene {
public:
    void init();
    void shutdown();

    // Replaces the reconstructed photo mesh. Takes the texture by GL id; the
    // caller (App) owns the texture's lifetime.
    void setReconstruction(const MeshData& mesh, GLuint texture);
    void clearReconstruction();
    bool hasReconstruction() const { return photo_.mesh.valid(); }

    void setEnvironment(int index);
    int environmentIndex() const { return environmentIndex_; }
    const Environment& environment() const;

    SceneObject& photo() { return photo_; }
    SceneObject& ground() { return ground_; }
    const SceneObject& photo() const { return photo_; }
    const SceneObject& ground() const { return ground_; }
    const Mesh& gizmoMesh() const { return gizmo_; }

    // Bounds of the reconstructed object (falls back to a unit region).
    glm::vec3 center() const { return center_; }
    float radius() const { return radius_; }

    size_t gpuBytes() const;

    bool showGround = true;
    bool showSky = true;

private:
    void updateGroundTransform();

    SceneObject photo_;
    SceneObject ground_;
    Mesh gizmo_;
    glm::vec3 boundsMin_{-1.0f, -1.0f, -3.0f};
    glm::vec3 boundsMax_{1.0f, 1.0f, -1.0f};
    glm::vec3 center_{0.0f, 0.0f, -2.0f};
    float radius_ = 1.5f;
    int environmentIndex_ = 0;
};
