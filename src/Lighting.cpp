#include "Lighting.h"

#include <glm/gtc/matrix_transform.hpp>

#include <algorithm>
#include <string>

// ---------------------------------------------------------------------------
// DirectionalLight
// ---------------------------------------------------------------------------
glm::vec3 DirectionalLight::direction() const {
    const float az = glm::radians(azimuthDeg);
    const float el = glm::radians(elevationDeg);
    // Vector pointing TOWARD the light, then negate.
    const glm::vec3 toLight(std::cos(el) * std::sin(az), std::sin(el), std::cos(el) * std::cos(az));
    return -glm::normalize(toLight);
}

// ---------------------------------------------------------------------------
// ShadowMap
// ---------------------------------------------------------------------------
bool ShadowMap::create(int size) {
    destroy();
    size_ = size;

    glGenTextures(1, &depthTex_);
    glBindTexture(GL_TEXTURE_2D, depthTex_);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_DEPTH_COMPONENT24, size, size, 0, GL_DEPTH_COMPONENT,
                 GL_FLOAT, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    // Outside the shadow frustum counts as "lit".
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_BORDER);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_BORDER);
    const float border[4] = {1.0f, 1.0f, 1.0f, 1.0f};
    glTexParameterfv(GL_TEXTURE_2D, GL_TEXTURE_BORDER_COLOR, border);
    glBindTexture(GL_TEXTURE_2D, 0);

    glGenFramebuffers(1, &fbo_);
    glBindFramebuffer(GL_FRAMEBUFFER, fbo_);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D, depthTex_, 0);
    glDrawBuffer(GL_NONE);
    glReadBuffer(GL_NONE);
    const bool complete = glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE;
    glBindFramebuffer(GL_FRAMEBUFFER, 0);

    if (!complete) {
        destroy();
        return false;
    }
    return true;
}

void ShadowMap::destroy() {
    if (fbo_) glDeleteFramebuffers(1, &fbo_);
    if (depthTex_) glDeleteTextures(1, &depthTex_);
    fbo_ = depthTex_ = 0;
    size_ = 0;
}

void ShadowMap::bindForWriting() const {
    glBindFramebuffer(GL_FRAMEBUFFER, fbo_);
    glViewport(0, 0, size_, size_);
    glClear(GL_DEPTH_BUFFER_BIT);
}

// ---------------------------------------------------------------------------
// LightingSystem
// ---------------------------------------------------------------------------
void LightingSystem::resetDefaults() {
    settings = LightingSettings{};
    settings.points[0].enabled = true;
    settings.points[1].color = {0.55f, 0.7f, 1.0f};
    settings.points[2].color = {1.0f, 0.4f, 0.4f};
    settings.points[3].color = {0.5f, 1.0f, 0.6f};
}

void LightingSystem::placePointLights(const glm::vec3& center, float radius) {
    const float r = std::max(radius, 0.1f);
    const glm::vec3 offsets[kMaxPointLights] = {
        {0.7f, 0.6f, 0.9f}, {-0.8f, 0.4f, 0.8f}, {0.0f, 0.9f, 0.6f}, {0.0f, -0.3f, 1.0f}};
    for (int i = 0; i < kMaxPointLights; ++i) settings.points[i].position = center + offsets[i] * r;
}

glm::mat4 LightingSystem::computeLightSpaceMatrix(const glm::vec3& center, float radius) const {
    const glm::vec3 dir = settings.sun.direction();
    const glm::vec3 up = std::abs(dir.y) > 0.99f ? glm::vec3(0, 0, 1) : glm::vec3(0, 1, 0);
    const float r = std::max(radius, 0.01f);
    const glm::vec3 eye = center - dir * (r * 2.0f);
    const glm::mat4 view = glm::lookAt(eye, center, up);
    const glm::mat4 proj = glm::ortho(-r, r, -r, r, 0.01f, r * 4.0f);
    return proj * view;
}

void LightingSystem::upload(const Shader& s, const glm::mat4& lightSpace,
                            bool shadowsActive) const {
    const LightingSettings& L = settings;
    s.setBool("uLightingEnabled", L.lightingEnabled);
    s.setVec3("uAmbient", srgbToLinear(L.ambientColor) * L.ambientIntensity);

    s.setBool("uSun.enabled", L.sun.enabled);
    s.setVec3("uSun.direction", L.sun.direction());
    s.setVec3("uSun.color", srgbToLinear(L.sun.color) * L.sun.intensity);

    for (int i = 0; i < kMaxPointLights; ++i) {
        const PointLight& p = L.points[i];
        const std::string base = "uPoints[" + std::to_string(i) + "].";
        s.setBool(base + "enabled", p.enabled);
        s.setVec3(base + "position", p.position);
        s.setVec3(base + "color", srgbToLinear(p.color) * p.intensity);
        s.setVec3(base + "attenuation", glm::vec3(p.constant, p.linear, p.quadratic));
    }

    s.setBool("uBlinn", L.specularModel == SpecularModel::BlinnPhong);
    s.setFloat("uSpecularStrength", L.specularStrength);
    s.setFloat("uShininess", L.shininess);

    s.setBool("uShadowsEnabled", shadowsActive);
    s.setFloat("uShadowBias", L.shadowBias);
    s.setInt("uPcfRadius", L.pcfRadius);
    s.setMat4("uLightSpace", lightSpace);
}

ShadowMap& LightingSystem::ensureShadowMap() {
    if (!shadowMap_.valid() || shadowMap_.size() != settings.shadowMapSize) {
        shadowMap_.create(settings.shadowMapSize);
    }
    return shadowMap_;
}

size_t LightingSystem::gpuBytes() const {
    return shadowMap_.valid() ? static_cast<size_t>(shadowMap_.size()) * shadowMap_.size() * 4 : 0;
}
