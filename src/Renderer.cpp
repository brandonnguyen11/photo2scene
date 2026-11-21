#include "Renderer.h"

#include <glm/gtc/matrix_transform.hpp>

#include <algorithm>
#include <cmath>

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

bool Renderer::init(const std::string& shaderDir, std::string* error) {
    shaderDir_ = shaderDir;
    if (!loadShaders(error)) return false;
    glGenVertexArrays(1, &emptyVao_);
    // 1x1 white texture so unbound samplers stay valid on strict drivers (macOS).
    glGenTextures(1, &whiteTex_);
    glBindTexture(GL_TEXTURE_2D, whiteTex_);
    const unsigned char white[4] = {255, 255, 255, 255};
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 1, 1, 0, GL_RGBA, GL_UNSIGNED_BYTE, white);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glBindTexture(GL_TEXTURE_2D, 0);
    gpuTimer_.init();
    return true;
}

bool Renderer::loadShaders(std::string* error) {
    const std::string d = shaderDir_ + "/";
    return sceneShader_.loadFromFiles(d + "vertex.glsl", d + "fragment.glsl", error) &&
           shadowShader_.loadFromFiles(d + "shadow_vertex.glsl", d + "shadow_fragment.glsl",
                                       error) &&
           skyShader_.loadFromFiles(d + "sky_vertex.glsl", d + "sky_fragment.glsl", error) &&
           unlitShader_.loadFromFiles(d + "vertex.glsl", d + "unlit_fragment.glsl", error) &&
           grassShader_.loadFromFiles(d + "grass_vertex.glsl", d + "grass_fragment.glsl", error) &&
           waterShader_.loadFromFiles(d + "water_vertex.glsl", d + "water_fragment.glsl", error);
}

bool Renderer::reloadShaders(std::string* error) {
    return sceneShader_.reload(error) && shadowShader_.reload(error) && skyShader_.reload(error) &&
           unlitShader_.reload(error) && grassShader_.reload(error) && waterShader_.reload(error);
}

void Renderer::shutdown() {
    if (emptyVao_) glDeleteVertexArrays(1, &emptyVao_);
    if (whiteTex_) glDeleteTextures(1, &whiteTex_);
    emptyVao_ = 0;
    whiteTex_ = 0;
    gpuTimer_.shutdown();
    sceneShader_ = Shader();
    shadowShader_ = Shader();
    skyShader_ = Shader();
    unlitShader_ = Shader();
    grassShader_ = Shader();
    waterShader_ = Shader();
}

void Renderer::drawSky(const SkyState& sky, const Camera& camera, const glm::mat4& invViewProj,
                       float timeSeconds) {
    glDisable(GL_DEPTH_TEST);
    glDepthMask(GL_FALSE);
    skyShader_.use();
    skyShader_.setMat4("uInvViewProj", invViewProj);
    skyShader_.setVec3("uCameraPos", camera.position());
    skyShader_.setVec3("uSunDir", sky.sunDirection);
    skyShader_.setVec3("uSunColor", sky.sunColorLinear);
    skyShader_.setFloat("uSunIntensity", sky.sunIntensity);
    skyShader_.setVec3("uZenithColor", sky.zenithColor);
    skyShader_.setVec3("uHorizonColor", sky.horizonColor);
    skyShader_.setFloat("uTime", timeSeconds);
    skyShader_.setBool("uIsNight", sky.isNight);
    glBindVertexArray(emptyVao_);
    glDrawArrays(GL_TRIANGLES, 0, 3);
    glBindVertexArray(0);
    glDepthMask(GL_TRUE);
    glEnable(GL_DEPTH_TEST);
}

void Renderer::drawSceneObject(const Shader& shader, const glm::mat4& model,
                               const glm::vec3& baseColor, GLuint texture, bool checker,
                               const Mesh& mesh, int material) {
    if (!mesh.valid()) return;
    shader.setMat4("uModel", model);
    shader.setMat3("uNormalMatrix", glm::transpose(glm::inverse(glm::mat3(model))));
    shader.setBool("uUseTexture", texture != 0);
    shader.setVec3("uBaseColor", baseColor);
    shader.setBool("uChecker", checker);
    shader.setInt("uMaterial", material);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, texture != 0 ? texture : whiteTex_);
    mesh.draw();
}

void Renderer::renderWorld(const World& world, const Scene* photoScene, const Camera& camera,
                           LightingSystem& lighting, float timeSeconds,
                           const RenderTarget& target) {
    if (target.measureGpuTime) gpuTimer_.begin();

    SkyState sky = world.sky().evaluate();
    // Drive the directional light from the analytic sun.
    LightingSettings& ls = lighting.settings;
    {
        const glm::vec3 toward = sky.sunDirection;
        ls.sun.enabled = !sky.isNight || toward.y > -0.15f;
        ls.sun.color = sky.sunColorLinear;
        ls.sun.intensity = sky.sunIntensity;
        // Convert toward-sun into travel direction + azimuth/elevation for the existing helper.
        const glm::vec3 travel = -toward;
        ls.sun.elevationDeg = glm::degrees(std::asin(std::clamp(toward.y, -1.0f, 1.0f)));
        ls.sun.azimuthDeg = glm::degrees(std::atan2(toward.x, toward.z));
        ls.ambientColor = sky.ambientLinear;
        ls.ambientIntensity = 1.0f;
        (void)travel;
    }

    const bool litMode = settings.viewMode == ViewMode::Lit;
    const bool shadowsActive = litMode && ls.lightingEnabled && ls.shadowsEnabled && ls.sun.enabled;

    glEnable(GL_DEPTH_TEST);
    glEnable(GL_CULL_FACE);
    glCullFace(GL_BACK);

    glm::mat4 lightSpace(1.0f);
    if (shadowsActive) {
        ShadowMap& sm = lighting.ensureShadowMap();
        if (sm.valid()) {
            lightSpace = lighting.computeLightSpaceMatrix(glm::vec3(0.0f, 2.0f, 0.0f), 55.0f);
            sm.bindForWriting();
            glEnable(GL_POLYGON_OFFSET_FILL);
            glPolygonOffset(2.0f, 4.0f);
            shadowShader_.use();
            shadowShader_.setMat4("uLightSpace", lightSpace);
            shadowShader_.setMat4("uModel", glm::mat4(1.0f));
            if (world.terrainMesh().valid()) world.terrainMesh().draw();
            for (const WorldObject& obj : world.objects()) {
                if (!obj.visible || !obj.castsShadow || !obj.mesh.valid()) continue;
                shadowShader_.setMat4("uModel", obj.model);
                obj.mesh.draw();
            }
            if (settings.showPhotoMesh && photoScene && photoScene->hasReconstruction()) {
                shadowShader_.setMat4("uModel", photoScene->photo().model);
                photoScene->photo().mesh.draw();
            }
            glDisable(GL_POLYGON_OFFSET_FILL);
        }
    }

    glBindFramebuffer(GL_FRAMEBUFFER, target.framebuffer);
    glViewport(0, 0, target.width, target.height);
    glClearColor(sky.horizonColor.r, sky.horizonColor.g, sky.horizonColor.b, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    const float aspect = static_cast<float>(target.width) / std::max(target.height, 1);
    const glm::mat4 view = camera.view();
    const glm::mat4 proj = camera.projection(aspect);
    const glm::vec3 eye = camera.position();
    const glm::mat4 invViewProj = glm::inverse(proj * view);

    // Sky
    skyShader_.use();
    skyShader_.setFloat("uTurbidity", world.sky().settings.turbidity);
    skyShader_.setFloat("uCloudCover", world.sky().settings.cloudCover);
    drawSky(sky, camera, invViewProj, timeSeconds);

    if (settings.wireframe) glPolygonMode(GL_FRONT_AND_BACK, GL_LINE);

    const float fogDensity = world.sky().settings.fogDensity;
    const glm::vec3 fogColor = sky.horizonColor;

    auto setupLit = [&](Shader& sh) {
        sh.use();
        sh.setMat4("uView", view);
        sh.setMat4("uProj", proj);
        sh.setVec3("uCameraPos", eye);
        sh.setInt("uViewMode", static_cast<int>(settings.viewMode));
        sh.setVec2("uDepthRange", glm::vec2(0.5f, world.radius()));
        lighting.upload(sh, lightSpace, shadowsActive);
        sh.setFloat("uFogDensity", fogDensity);
        sh.setVec3("uFogColor", fogColor);
        sh.setInt("uAlbedoTex", 0);
        sh.setInt("uShadowMap", 1);
        glActiveTexture(GL_TEXTURE1);
        glBindTexture(GL_TEXTURE_2D, shadowsActive ? lighting.ensureShadowMap().depthTexture() : 0);
    };

    setupLit(sceneShader_);

    // Terrain
    drawSceneObject(sceneShader_, glm::mat4(1.0f), world.terrainColor(), 0, false,
                    world.terrainMesh(), static_cast<int>(world.terrainMaterial()));

    // Backdrop (plain so fog can melt it into the sky color)
    drawSceneObject(sceneShader_, glm::mat4(1.0f), world.backdropColor(), 0, false,
                    world.backdropMesh(), static_cast<int>(WorldMaterial::Plain));

    // Props
    glDisable(GL_CULL_FACE);
    for (const WorldObject& obj : world.objects()) {
        if (!obj.visible) continue;
        drawSceneObject(sceneShader_, obj.model, obj.baseColor, 0, false, obj.mesh,
                        static_cast<int>(obj.material));
    }

    // Photo mesh (optional)
    if (settings.showPhotoMesh && photoScene && photoScene->hasReconstruction()) {
        const SceneObject& photo = photoScene->photo();
        drawSceneObject(sceneShader_, photo.model, photo.baseColor, photo.texture, photo.checker,
                        photo.mesh);
    }

    // Grass
    if (world.hasGrass()) {
        grassShader_.use();
        grassShader_.setMat4("uView", view);
        grassShader_.setMat4("uProj", proj);
        grassShader_.setMat4("uLightSpace", lightSpace);
        grassShader_.setVec3("uCameraPos", eye);
        grassShader_.setFloat("uTime", timeSeconds);
        grassShader_.setFloat("uWind", world.windStrength());
        grassShader_.setVec3("uBaseColor", glm::vec3(0.28f, 0.58f, 0.20f));
        grassShader_.setVec3("uAmbient", ls.ambientColor * ls.ambientIntensity);
        grassShader_.setVec3("uSunDir", ls.sun.direction());
        grassShader_.setVec3("uSunColor", srgbToLinear(ls.sun.color) * ls.sun.intensity);
        grassShader_.setBool("uSunEnabled", ls.sun.enabled);
        grassShader_.setInt("uShadowMap", 1);
        grassShader_.setBool("uShadowsEnabled", shadowsActive);
        grassShader_.setFloat("uShadowBias", ls.shadowBias);
        grassShader_.setFloat("uFogDensity", fogDensity);
        grassShader_.setVec3("uFogColor", fogColor);
        glActiveTexture(GL_TEXTURE1);
        glBindTexture(GL_TEXTURE_2D, shadowsActive ? lighting.ensureShadowMap().depthTexture() : 0);
        glDisable(GL_CULL_FACE);
        world.grassMesh().drawInstanced();
    }

    // Water
    if (world.hasWater()) {
        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        waterShader_.use();
        const glm::mat4 waterModel =
            glm::translate(glm::mat4(1.0f),
                           glm::vec3(world.desc().waterCenter.x, world.desc().waterLevel,
                                     world.desc().waterCenter.z));
        waterShader_.setMat4("uModel", waterModel);
        waterShader_.setMat4("uView", view);
        waterShader_.setMat4("uProj", proj);
        waterShader_.setFloat("uTime", timeSeconds);
        waterShader_.setVec3("uCameraPos", eye);
        waterShader_.setVec3("uSunDir", ls.sun.direction());
        waterShader_.setVec3("uSunColor", srgbToLinear(ls.sun.color) * ls.sun.intensity);
        waterShader_.setVec3("uDeepColor", world.waterDeepColor());
        waterShader_.setVec3("uShallowColor", world.waterShallowColor());
        waterShader_.setVec3("uHorizonColor", sky.horizonColor);
        waterShader_.setVec3("uZenithColor", sky.zenithColor);
        waterShader_.setFloat("uFogDensity", fogDensity);
        waterShader_.setVec3("uFogColor", fogColor);
        glDisable(GL_CULL_FACE);
        world.waterMesh().draw();
        glDisable(GL_BLEND);
    }

    if (settings.wireframe) glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, 0);
    if (target.measureGpuTime) gpuTimer_.end();
}

void Renderer::render(const Scene& scene, const Camera& camera, LightingSystem& lighting,
                      const RenderTarget& target) {
    // Thin wrapper: synthesize a minimal sky from the environment preset.
    if (target.measureGpuTime) gpuTimer_.begin();

    const LightingSettings& ls = lighting.settings;
    const Environment& env = scene.environment();
    const SceneObject& photo = scene.photo();
    const SceneObject& ground = scene.ground();

    const bool litMode = settings.viewMode == ViewMode::Lit;
    const bool shadowsActive =
        litMode && ls.lightingEnabled && ls.shadowsEnabled && ls.sun.enabled && photo.mesh.valid();

    glEnable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);

    glm::mat4 lightSpace(1.0f);
    if (shadowsActive) {
        ShadowMap& sm = lighting.ensureShadowMap();
        if (sm.valid()) {
            lightSpace = lighting.computeLightSpaceMatrix(scene.center(), scene.radius() * 2.0f);
            sm.bindForWriting();
            glEnable(GL_POLYGON_OFFSET_FILL);
            glPolygonOffset(2.0f, 4.0f);
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

    glBindFramebuffer(GL_FRAMEBUFFER, target.framebuffer);
    glViewport(0, 0, target.width, target.height);
    glClearColor(env.skyHorizon.r, env.skyHorizon.g, env.skyHorizon.b, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    const float aspect = static_cast<float>(target.width) / std::max(target.height, 1);
    const glm::mat4 view = camera.view();
    const glm::mat4 proj = camera.projection(aspect);
    const glm::vec3 eye = camera.position();

    if (scene.showSky) {
        SkyState sky;
        sky.zenithColor = env.skyTop;
        sky.horizonColor = env.skyHorizon;
        sky.sunDirection = -ls.sun.direction();
        sky.sunColorLinear = srgbToLinear(ls.sun.color);
        sky.sunIntensity = ls.sun.intensity;
        sky.isNight = false;
        drawSky(sky, camera, glm::inverse(proj * view), 0.0f);
        skyShader_.use();
        skyShader_.setFloat("uTurbidity", 2.5f);
        skyShader_.setFloat("uCloudCover", 0.1f);
    }

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
    sceneShader_.setFloat("uFogDensity", 0.0f);
    sceneShader_.setVec3("uFogColor", env.skyHorizon);
    sceneShader_.setInt("uAlbedoTex", 0);
    sceneShader_.setInt("uShadowMap", 1);
    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, shadowsActive ? lighting.ensureShadowMap().depthTexture() : 0);

    drawSceneObject(sceneShader_, photo.model, photo.baseColor, photo.texture, photo.checker,
                    photo.mesh);
    if (scene.showGround)
        drawSceneObject(sceneShader_, ground.model, ground.baseColor, 0, ground.checker,
                        ground.mesh);

    if (settings.wireframe) glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, 0);
    if (target.measureGpuTime) gpuTimer_.end();
}
