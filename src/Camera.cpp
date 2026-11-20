#include "Camera.h"

#include <glm/gtc/matrix_transform.hpp>

#include <algorithm>
#include <cmath>

namespace {
constexpr float kMaxPitch = 1.55f;  // just under 90 degrees
constexpr float kOrbitSpeed = 0.006f;  // radians per pixel
}  // namespace

glm::vec3 Camera::position() const {
    const float cp = std::cos(pitch);
    return target + distance * glm::vec3(cp * std::sin(yaw), std::sin(pitch), cp * std::cos(yaw));
}

glm::vec3 Camera::forward() const { return glm::normalize(target - position()); }

glm::mat4 Camera::view() const { return glm::lookAt(position(), target, glm::vec3(0, 1, 0)); }

glm::mat4 Camera::projection(float aspect) const {
    return glm::perspective(glm::radians(fovYDeg), std::max(aspect, 1e-4f), nearPlane, farPlane);
}

void Camera::orbit(float dxPixels, float dyPixels) {
    orbitRadians(-dxPixels * kOrbitSpeed, dyPixels * kOrbitSpeed);
}

void Camera::orbitRadians(float dYaw, float dPitch) {
    yaw += dYaw;
    pitch = std::clamp(pitch + dPitch, -kMaxPitch, kMaxPitch);
}

void Camera::pan(float dxPixels, float dyPixels, float viewportHeightPixels) {
    const glm::vec3 f = forward();
    const glm::vec3 right = glm::normalize(glm::cross(f, glm::vec3(0, 1, 0)));
    const glm::vec3 up = glm::cross(right, f);
    // World units per pixel at the target's distance.
    const float worldPerPixel = 2.0f * distance * std::tan(glm::radians(fovYDeg) * 0.5f) /
                                std::max(viewportHeightPixels, 1.0f);
    target += (-dxPixels * right + dyPixels * up) * worldPerPixel;
}

void Camera::zoom(float wheelSteps) {
    distance = std::clamp(distance * std::pow(0.9f, wheelSteps), 0.05f, 100.0f);
}

void Camera::setHome(const glm::vec3& homeTarget, float homeDistance, float homeYaw,
                     float homePitch, float homeFovDeg) {
    homeTarget_ = homeTarget;
    homeDistance_ = homeDistance;
    homeYaw_ = homeYaw;
    homePitch_ = homePitch;
    homeFov_ = homeFovDeg;
}

void Camera::reset() {
    target = homeTarget_;
    distance = homeDistance_;
    yaw = homeYaw_;
    pitch = homePitch_;
    fovYDeg = homeFov_;
}
