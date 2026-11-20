#pragma once
// Renderer.h — multi-pass OpenGL renderer (Member 1: Graphics Engine)
//
// Pass 1: shadow map (depth from the directional light)
// Pass 2: sky gradient (full-screen triangle)
// Pass 3: lit scene (reconstruction + ground)
// Pass 4: unlit overlays (point-light gizmos)

#include "Camera.h"
#include "Lighting.h"
#include "Scene.h"
#include "Shader.h"

#include <glad/gl.h>

#include <string>

enum class ViewMode : int { Lit = 0, Unlit = 1, Normals = 2, Depth = 3 };

struct RenderSettings {
    ViewMode viewMode = ViewMode::Lit;
    bool wireframe = false;
    bool showLightGizmos = true;
};

struct RenderTarget {
    GLuint framebuffer = 0;  // 0 = default framebuffer (the window)
    int width = 1;
    int height = 1;
    bool overlays = true;    // draw gizmos
    bool measureGpuTime = true;
};

// Double-buffered GL_TIME_ELAPSED queries so reading results never stalls.
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

    void render(const Scene& scene, const Camera& camera, LightingSystem& lighting,
                const RenderTarget& target);

    RenderSettings settings;
    double gpuFrameMs() const { return gpuTimer_.lastMs(); }

private:
    bool loadShaders(std::string* error);

    std::string shaderDir_;
    Shader sceneShader_;
    Shader shadowShader_;
    Shader skyShader_;
    Shader unlitShader_;
    GLuint emptyVao_ = 0;
    GpuTimer gpuTimer_;
};
