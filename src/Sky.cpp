#include "Sky.h"

#include <algorithm>
#include <cmath>

namespace {

constexpr float kPi = 3.14159265358979323846f;

float wrapHours(float h) {
    h = std::fmod(h, 24.0f);
    if (h < 0.0f) h += 24.0f;
    return h;
}

// Solar elevation / azimuth from local solar time and latitude (simplified).
void sunAngles(const SkySettings& s, float& elevationRad, float& azimuthRad) {
    const float lat = glm::radians(s.latitudeDeg);
    // Approximate day-of-year as summer solstice for a lush garden look.
    const float declination = glm::radians(23.44f);
    const float hourAngle = (wrapHours(s.timeOfDayHours) - 12.0f) * 15.0f * (kPi / 180.0f);

    const float sinEl = std::sin(lat) * std::sin(declination) +
                        std::cos(lat) * std::cos(declination) * std::cos(hourAngle);
    elevationRad = std::asin(std::clamp(sinEl, -1.0f, 1.0f));

    const float cosAz = (std::sin(declination) - std::sin(lat) * sinEl) /
                        std::max(1e-5f, std::cos(lat) * std::cos(elevationRad));
    azimuthRad = std::acos(std::clamp(cosAz, -1.0f, 1.0f));
    if (hourAngle > 0.0f) azimuthRad = 2.0f * kPi - azimuthRad;
}

glm::vec3 sunColorFromElevation(float elevDeg) {
    // Warm at low angles, white/slightly cool at noon.
    const float t = std::clamp(elevDeg / 60.0f, 0.0f, 1.0f);
    const glm::vec3 dawn(1.0f, 0.45f, 0.18f);
    const glm::vec3 noon(1.0f, 0.97f, 0.90f);
    return glm::mix(dawn, noon, t * t);
}

}  // namespace

glm::vec3 Sky::sunDirectionToward(const SkySettings& s) {
    float el = 0.0f, az = 0.0f;
    sunAngles(s, el, az);
    return glm::normalize(glm::vec3(std::cos(el) * std::sin(az), std::sin(el),
                                   std::cos(el) * std::cos(az)));
}

float Sky::sunElevationDeg(const SkySettings& s) {
    float el = 0.0f, az = 0.0f;
    sunAngles(s, el, az);
    (void)az;
    return glm::degrees(el);
}

SkyState Sky::evaluate() const {
    SkyState out;
    float el = 0.0f, az = 0.0f;
    sunAngles(settings, el, az);
    const float elevDeg = glm::degrees(el);
    out.sunDirection = glm::normalize(
        glm::vec3(std::cos(el) * std::sin(az), std::sin(el), std::cos(el) * std::cos(az)));

    out.isNight = elevDeg < -2.0f;
    const float dayFactor = std::clamp((elevDeg + 4.0f) / 12.0f, 0.0f, 1.0f);

    // Turbidity softens / yellows the sky a bit.
    const float turb = std::clamp(settings.turbidity, 1.5f, 8.0f);
    const float haze = (turb - 1.5f) / 6.5f;

    // Punchy clear-day blues; turbidity only gently desaturates.
    out.zenithColor = glm::mix(glm::vec3(0.15f, 0.42f, 0.92f), glm::vec3(0.40f, 0.55f, 0.72f), haze);
    out.horizonColor = glm::mix(glm::vec3(0.78f, 0.88f, 0.98f), glm::vec3(0.85f, 0.78f, 0.65f), haze);

    // Sunset / sunrise wash.
    if (elevDeg > -4.0f && elevDeg < 18.0f) {
        const float warm = 1.0f - std::abs(elevDeg - 4.0f) / 14.0f;
        const float w = std::clamp(warm, 0.0f, 1.0f);
        out.horizonColor = glm::mix(out.horizonColor, glm::vec3(1.0f, 0.48f, 0.22f), w * 0.9f);
        out.zenithColor = glm::mix(out.zenithColor, glm::vec3(0.25f, 0.18f, 0.45f), w * 0.45f);
    }

    if (out.isNight) {
        out.zenithColor = glm::vec3(0.02f, 0.03f, 0.08f);
        out.horizonColor = glm::vec3(0.05f, 0.06f, 0.12f);
        out.sunColorLinear = glm::vec3(0.35f, 0.40f, 0.55f);
        out.sunIntensity = 0.08f;
        out.ambientLinear = glm::vec3(0.04f, 0.05f, 0.08f);
    } else {
        out.sunColorLinear = sunColorFromElevation(elevDeg);
        out.sunIntensity = 0.7f + 1.1f * dayFactor;
        out.ambientLinear = out.horizonColor * (0.18f + 0.20f * dayFactor) * (1.0f - 0.2f * haze);
        out.sunIntensity *= 1.0f - 0.35f * settings.cloudCover;
        out.ambientLinear *= 1.0f - 0.15f * settings.cloudCover;
    }

    // Keep sky readable at dawn/dusk — don't crush to near-black.
    out.zenithColor *= 0.55f + 0.45f * dayFactor;
    out.horizonColor *= 0.65f + 0.35f * dayFactor;
    return out;
}

void Sky::tick(float dtSeconds) {
    if (!settings.autoAdvance) return;
    settings.timeOfDayHours =
        wrapHours(settings.timeOfDayHours + settings.autoAdvanceSpeed * dtSeconds);
}
