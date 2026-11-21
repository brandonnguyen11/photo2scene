#pragma once
// Renderer.h — multi-pass OpenGL renderer (world + optional v1 reconstruction)

#include "Camera.h"
#include "Lighting.h"
#include "Scene.h"
#include "Shader.h"
#include "Sky.h"
#include "World.h"

#include <glad/gl.h>

#include <string>

enum class ViewMode : int { Lit = 0, Unlit = 1, Normals = 2, Depth = 3 };

struct RenderSettings {
    ViewMode viewMode = ViewMode::Lit;
    bool wireframe = false;
    bool showLightGizmos = false;
    bool showPhotoMesh = true;
};

struct RenderTarget {
    GLuint framebuffer = 0;
    int width = 1;
    int height = 1;
    bool overlays = true;
    bool measureGpuTime = true;
};

class GpuTimer {
public:
    void init();
    void shutdown();
    void begin();
    void end();
    double lastMs() const { return lastMs_; }

private:
    static constexpr int kQueries = 4;
    GLuint queries_[kQueries] = {};
    int frame_ = 0;
    double lastMs_ = 0.0;
    bool initialized_ = false;
};

class Renderer {
public:
    bool init(const std::string& shaderDir, std::string* error);
    bool reloadShaders(std::string* error);
    void shutdown();

    // Immersive world path (v2). photoScene is optional (A-7 fallback mesh).
    void renderWorld(const World& world, const Scene* photoScene, const Camera& camera,
                     LightingSystem& lighting, float timeSeconds, const RenderTarget& target);

    // Legacy v1-only path.
    void render(const Scene& scene, const Camera& camera, LightingSystem& lighting,
                const RenderTarget& target);

    RenderSettings settings;
    double gpuFrameMs() const { return gpuTimer_.lastMs(); }

private:
    bool loadShaders(std::string* error);
    void drawSky(const SkyState& sky, const Camera& camera, const glm::mat4& invViewProj,
                 float timeSeconds);
    void drawSceneObject(const Shader& shader, const glm::mat4& model, const glm::vec3& baseColor,
                         GLuint texture, bool checker, const Mesh& mesh, int material = 0);

    std::string shaderDir_;
    Shader sceneShader_;
    Shader shadowShader_;
    Shader skyShader_;
    Shader unlitShader_;
    Shader grassShader_;
    Shader waterShader_;
    GLuint emptyVao_ = 0;
    GLuint whiteTex_ = 0;
    GpuTimer gpuTimer_;
};
