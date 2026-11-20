#pragma once
// Lighting.h — custom lighting system (Member 2: Lighting Engine)
//
// Holds all light/material parameters, uploads them to the scene shader and
// owns the directional-light shadow map. The actual shading math lives in
// shaders/fragment.glsl.

#include "Shader.h"

#include <glad/gl.h>
#include <glm/glm.hpp>

#include <array>
#include <cmath>

constexpr int kMaxPointLights = 4;  // must match MAX_POINT_LIGHTS in fragment.glsl

// Colors in the UI are sRGB; lighting math happens in linear space.
inline glm::vec3 srgbToLinear(const glm::vec3& c) {
    auto f = [](float x) {
        return x <= 0.04045f ? x / 12.92f : std::pow((x + 0.055f) / 1.055f, 2.4f);
    };
    return {f(c.r), f(c.g), f(c.b)};
}

struct DirectionalLight {
    bool enabled = true;
    float azimuthDeg = 35.0f;    // 0 = light comes from the camera side (+Z)
    float elevationDeg = 40.0f;  // 90 = straight down
    glm::vec3 color{1.0f, 0.97f, 0.92f};
    float intensity = 0.8f;

    // Direction the light TRAVELS (from the light toward the scene).
    glm::vec3 direction() const;
};

struct PointLight {
    bool enabled = false;
    glm::vec3 position{0.6f, 0.6f, -1.2f};
    glm::vec3 color{1.0f, 0.85f, 0.65f};
    float intensity = 1.0f;
    // Attenuation = 1 / (constant + linear*d + quadratic*d^2)
    float constant = 1.0f;
    float linear = 0.35f;
    float quadratic = 0.45f;
};

enum class SpecularModel : int { Phong = 0, BlinnPhong = 1 };

struct LightingSettings {
    bool lightingEnabled = true;

    glm::vec3 ambientColor{1.0f, 1.0f, 1.0f};
    float ambientIntensity = 0.18f;

    DirectionalLight sun;
    std::array<PointLight, kMaxPointLights> points;

    SpecularModel specularModel = SpecularModel::BlinnPhong;
    float specularStrength = 0.15f;
    float shininess = 32.0f;

    bool shadowsEnabled = true;
    float shadowBias = 0.0015f;
    int pcfRadius = 1;          // 0 = hard shadows, 1 = 3x3, 2 = 5x5
    int shadowMapSize = 2048;
};

class ShadowMap {
public:
    ~ShadowMap() { destroy(); }
    bool create(int size);
    void destroy();
    void bindForWriting() const;
    GLuint depthTexture() const { return depthTex_; }
    int size() const { return size_; }
    bool valid() const { return fbo_ != 0; }

private:
    GLuint fbo_ = 0;
    GLuint depthTex_ = 0;
    int size_ = 0;
};

class LightingSystem {
public:
    LightingSettings settings;

    void resetDefaults();
    // Places the point lights around a scene with the given bounds.
    void placePointLights(const glm::vec3& center, float radius);

    // Orthographic light-space matrix that covers a bounding sphere.
    glm::mat4 computeLightSpaceMatrix(const glm::vec3& center, float radius) const;

    // Uploads every lighting uniform to the scene shader.
    void upload(const Shader& shader, const glm::mat4& lightSpace, bool shadowsActive) const;

    // (Re)creates the shadow map if needed (size changed or not yet created).
    ShadowMap& ensureShadowMap();
    size_t gpuBytes() const;

private:
    ShadowMap shadowMap_;
};
