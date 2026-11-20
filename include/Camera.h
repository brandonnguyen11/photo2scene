#pragma once
// Camera.h — orbit camera (Member 1: Graphics Engine)
//
// The camera orbits a target point. Its eye position is
//   target + distance * (cos(pitch)sin(yaw), sin(pitch), cos(pitch)cos(yaw))
// so yaw = pitch = 0 puts the eye on the +Z side looking toward -Z, which is
// exactly the pose of the camera that took the photograph (see Reconstruction).

#include <glm/glm.hpp>

class Camera {
public:
    glm::vec3 target{0.0f, 0.0f, -2.0f};
    float distance = 2.0f;
    float yaw = 0.0f;    // radians
    float pitch = 0.0f;  // radians, clamped to avoid gimbal flip
    float fovYDeg = 50.0f;
    float nearPlane = 0.01f;
    float farPlane = 200.0f;

    glm::vec3 position() const;
    glm::vec3 forward() const;
    glm::mat4 view() const;
    glm::mat4 projection(float aspect) const;

    // Mouse helpers (deltas in window pixels).
    void orbit(float dxPixels, float dyPixels);
    void orbitRadians(float dYaw, float dPitch);
    void pan(float dxPixels, float dyPixels, float viewportHeightPixels);
    void zoom(float wheelSteps);

    // "Home" pose used by Reset.
    void setHome(const glm::vec3& homeTarget, float homeDistance, float homeYaw, float homePitch,
                 float homeFovDeg);
    void reset();

private:
    glm::vec3 homeTarget_{0.0f, 0.0f, -2.0f};
    float homeDistance_ = 2.0f;
    float homeYaw_ = 0.0f;
    float homePitch_ = 0.0f;
    float homeFov_ = 50.0f;
};
