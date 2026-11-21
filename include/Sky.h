#pragma once
// Sky.h — analytic Preetham sky + sun from time of day and latitude (S-1..S-3, S-5, S-6)

#include <glm/glm.hpp>

struct SkySettings {
    float timeOfDayHours = 10.0f;  // 0–24
    float latitudeDeg = 48.8f;     // Versailles-ish default
    float turbidity = 2.7f;        // 1.8 clear … ~8 hazy
    float cloudCover = 0.25f;      // 0–1
    float fogDensity = 0.018f;
    float windStrength = 0.45f;
    bool autoAdvance = false;
    float autoAdvanceSpeed = 0.15f;  // hours per real second
};

struct SkyState {
    glm::vec3 sunDirection{0.0f, 1.0f, 0.0f};  // toward the sun
    glm::vec3 sunColorLinear{1.0f};
    float sunIntensity = 1.0f;
    glm::vec3 ambientLinear{0.15f};
    glm::vec3 horizonColor{0.7f, 0.8f, 0.95f};
    glm::vec3 zenithColor{0.2f, 0.45f, 0.9f};
    bool isNight = false;
};

class Sky {
public:
    SkySettings settings;

    // Updates sun position / colors from time + latitude + turbidity.
    SkyState evaluate() const;

    // Advances auto time-of-day if enabled.
    void tick(float dtSeconds);

    static float sunElevationDeg(const SkySettings& s);
    static glm::vec3 sunDirectionToward(const SkySettings& s);
};
