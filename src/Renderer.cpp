#include "Renderer.h"

#include <glm/gtc/matrix_transform.hpp>

#include <algorithm>

// ---------------------------------------------------------------------------
// GpuTimer
// ---------------------------------------------------------------------------
void GpuTimer::init() {
    glGenQueries(kQueries, queries_);
    initialized_ = true;
}

void GpuTimer::shutdown() {
    if (initialized_) glDeleteQueries(kQueries, queries_);
    initialized_ = false;
}

void GpuTimer::begin() {
    if (initialized_) glBeginQuery(GL_TIME_ELAPSED, queries_[frame_ % kQueries]);
}

void GpuTimer::end() {
    if (!initialized_) return;
    glEndQuery(GL_TIME_ELAPSED);
    ++frame_;
    if (frame_ >= kQueries) {
        // The query we will overwrite next was issued kQueries-1 frames ago.
        const GLuint q = queries_[frame_ % kQueries];
        GLint available = 0;
        glGetQueryObjectiv(q, GL_QUERY_RESULT_AVAILABLE, &available);
        if (available) {
            GLuint64 ns = 0;
            glGetQueryObjectui64v(q, GL_QUERY_RESULT, &ns);
            lastMs_ = static_cast<double>(ns) / 1.0e6;
        }
    }
}

// ---------------------------------------------------------------------------
// Renderer
// ---------------------------------------------------------------------------
bool Renderer::init(const std::string& shaderDir, std::string* error) {
    shaderDir_ = shaderDir;
    if (!loadShaders(error)) return false;
    glGenVertexArrays(1, &emptyVao_);  // core profile needs a VAO even with no attributes
    gpuTimer_.init();
    return true;
}

bool Renderer::loadShaders(std::string* error) {
    const std::string d = shaderDir_ + "/";
    return sceneShader_.loadFromFiles(d + "vertex.glsl", d + "fragment.glsl", error) &&
           shadowShader_.loadFromFiles(d + "shadow_vertex.glsl", d + "shadow_fragment.glsl",
                                       error) &&
           skyShader_.loadFromFiles(d + "sky_vertex.glsl", d + "sky_fragment.glsl", error) &&
           unlitShader_.loadFromFiles(d + "vertex.glsl", d + "unlit_fragment.glsl", error);
}

bool Renderer::reloadShaders(std::string* error) {
    // Each reload keeps the old program on failure, so a typo never blanks the screen.
    return sceneShader_.reload(error) && shadowShader_.reload(error) && skyShader_.reload(error) &&
           unlitShader_.reload(error);
}

void Renderer::shutdown() {
    if (emptyVao_) glDeleteVertexArrays(1, &emptyVao_);
    emptyVao_ = 0;
    gpuTimer_.shutdown();
    sceneShader_ = Shader();
    shadowShader_ = Shader();
    skyShader_ = Shader();
    unlitShader_ = Shader();
}

void Renderer::render(const Scene& scene, const Camera& camera, LightingSystem& lighting,
                      const RenderTarget& target) {
    if (target.measureGpuTime) gpuTimer_.begin();

    const LightingSettings& ls = lighting.settings;
    const Environment& env = scene.environment();
    const SceneObject& photo = scene.photo();
    const SceneObject& ground = scene.ground();

    const bool litMode = settings.viewMode == ViewMode::Lit;
    const bool shadowsActive =
        litMode && ls.lightingEnabled && ls.shadowsEnabled && ls.sun.enabled && photo.mesh.valid();

    glEnable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);  // the depth mesh is an open surface; we shade both sides

    // ---- Pass 1: shadow map ------------------------------------------------
    glm::mat4 lightSpace(1.0f);
    if (shadowsActive) {
        ShadowMap& sm = lighting.ensureShadowMap();
        if (sm.valid()) {
            // Cover the object generously so its shadow on the ground is included.
            lightSpace = lighting.computeLightSpaceMatrix(scene.center(), scene.radius() * 2.0f);
            sm.bindForWriting();
            glEnable(GL_POLYGON_OFFSET_FILL);
            glPolygonOffset(2.0f, 4.0f);  // slope-scaled bias against shadow acne
            shadowShader_.use();
            shadowShader_.setMat4("uLightSpace", lightSpace);
            for (const SceneObject* obj : {&photo, &ground}) {
                if (!obj->visible || !obj->castsShadow || !obj->mesh.valid()) continue;
                shadowShader_.setMat4("uModel", obj->model);
                obj->mesh.draw();
            }
            glDisable(GL_POLYGON_OFFSET_FILL);
        }
    }

    // ---- Main framebuffer ---------------------------------------------------
    glBindFramebuffer(GL_FRAMEBUFFER, target.framebuffer);
    glViewport(0, 0, target.width, target.height);
    glClearColor(env.skyHorizon.r, env.skyHorizon.g, env.skyHorizon.b, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    const float aspect = static_cast<float>(target.width) / std::max(target.height, 1);
    const glm::mat4 view = camera.view();
    const glm::mat4 proj = camera.projection(aspect);
    const glm::vec3 eye = camera.position();

    // ---- Pass 2: sky ---------------------------------------------------------
    if (scene.showSky) {
        glDisable(GL_DEPTH_TEST);
        glDepthMask(GL_FALSE);
        skyShader_.use();
        skyShader_.setMat4("uInvViewProj", glm::inverse(proj * view));
        skyShader_.setVec3("uCameraPos", eye);
        skyShader_.setVec3("uSkyTop", env.skyTop);
        skyShader_.setVec3("uSkyHorizon", env.skyHorizon);
        skyShader_.setVec3("uSkyBottom", env.skyBottom);
        glBindVertexArray(emptyVao_);
        glDrawArrays(GL_TRIANGLES, 0, 3);
        glBindVertexArray(0);
        glDepthMask(GL_TRUE);
        glEnable(GL_DEPTH_TEST);
    }

    // ---- Pass 3: scene -------------------------------------------------------
    if (settings.wireframe) glPolygonMode(GL_FRONT_AND_BACK, GL_LINE);

    sceneShader_.use();
    sceneShader_.setMat4("uView", view);
    sceneShader_.setMat4("uProj", proj);
    sceneShader_.setVec3("uCameraPos", eye);
    sceneShader_.setInt("uViewMode", static_cast<int>(settings.viewMode));
    const float camToCenter = glm::length(scene.center() - eye);
    sceneShader_.setVec2("uDepthRange",
                         glm::vec2(std::max(0.01f, camToCenter - scene.radius()),
                                   camToCenter + scene.radius()));
    lighting.upload(sceneShader_, lightSpace, shadowsActive);

    sceneShader_.setInt("uAlbedoTex", 0);
    sceneShader_.setInt("uShadowMap", 1);
    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, shadowsActive ? lighting.ensureShadowMap().depthTexture() : 0);

    auto drawObject = [&](const SceneObject& obj) {
        if (!obj.visible || !obj.mesh.valid()) return;
        sceneShader_.setMat4("uModel", obj.model);
        sceneShader_.setMat3("uNormalMatrix", glm::transpose(glm::inverse(glm::mat3(obj.model))));
        sceneShader_.setBool("uUseTexture", obj.texture != 0);
        sceneShader_.setVec3("uBaseColor", obj.baseColor);
        sceneShader_.setBool("uChecker", obj.checker);
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, obj.texture);
        obj.mesh.draw();
    };
    drawObject(photo);
    if (scene.showGround) drawObject(ground);

    if (settings.wireframe) glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);

    // ---- Pass 4: light gizmos -------------------------------------------------
    if (target.overlays && settings.showLightGizmos && litMode && ls.lightingEnabled) {
        unlitShader_.use();
        unlitShader_.setMat4("uView", view);
        unlitShader_.setMat4("uProj", proj);
        const float size = std::max(scene.radius() * 0.025f, 0.005f);
        for (const PointLight& p : ls.points) {
            if (!p.enabled) continue;
            const glm::mat4 model = glm::translate(glm::mat4(1.0f), p.position) *
                                    glm::scale(glm::mat4(1.0f), glm::vec3(size));
            unlitShader_.setMat4("uModel", model);
            unlitShader_.setVec3("uColor", p.color);
            scene.gizmoMesh().draw();
        }
    }

    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, 0);
    if (target.measureGpuTime) gpuTimer_.end();
}
