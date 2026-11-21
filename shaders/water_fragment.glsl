#version 410 core
// Fresnel water + circular pool / shore clipping
in vec3 vWorldPos;
in vec3 vNormal;
in vec2 vUV;
in float vViewDepth;
in vec2 vLocalXZ;
out vec4 FragColor;

uniform vec3 uCameraPos;
uniform vec3 uSunDir;
uniform vec3 uSunColor;
uniform vec3 uDeepColor;
uniform vec3 uShallowColor;
uniform vec3 uHorizonColor;
uniform vec3 uZenithColor;
uniform float uFogDensity;
uniform vec3 uFogColor;
uniform float uClipRadius;   // >0 → discard outside circle (local XZ)
uniform float uShoreZ;       // discard if world Z above this (inland)
uniform vec3 uWaterCenter;

vec3 srgbToLinear(vec3 c) {
    return mix(c / 12.92, pow((c + 0.055) / 1.055, vec3(2.4)), step(vec3(0.04045), c));
}
vec3 linearToSrgb(vec3 c) {
    c = clamp(c, 0.0, 1.0);
    return mix(c * 12.92, 1.055 * pow(c, vec3(1.0 / 2.4)) - 0.055, step(vec3(0.0031308), c));
}

void main() {
    // Circular fountain pool — hard clip to the disc (not a square grid).
    if (uClipRadius > 0.0) {
        float r = length(vLocalXZ);
        if (r > uClipRadius) discard;
    }

    // Beach: keep water in the ocean basin, fade out before the dunes.
    float shoreFade = 1.0;
    if (uShoreZ < 1.0e5) {
        if (vWorldPos.z > uShoreZ) discard;
        shoreFade = smoothstep(uShoreZ, uShoreZ - 4.0, vWorldPos.z);
    }

    vec3 N = normalize(vNormal);
    vec3 V = normalize(uCameraPos - vWorldPos);
    float fresnel = pow(1.0 - max(dot(N, V), 0.0), 3.5);
    fresnel = mix(0.1, 1.0, fresnel);

    float depthFactor = clamp((uWaterCenter.y + 0.6 - vWorldPos.y) * 0.5 + 0.4, 0.0, 1.0);
    vec3 waterCol = mix(srgbToLinear(uShallowColor), srgbToLinear(uDeepColor), depthFactor);

    vec3 R = reflect(-V, N);
    float skyT = pow(1.0 - max(R.y, 0.0), 1.4);
    vec3 skyCol = mix(srgbToLinear(uZenithColor), srgbToLinear(uHorizonColor), skyT);

    vec3 L = normalize(-uSunDir);
    vec3 H = normalize(L + V);
    float spec = pow(max(dot(N, H), 0.0), 220.0) * 1.8;

    vec3 color = mix(waterCol, skyCol, fresnel);
    color += uSunColor * spec;

    float dist = length(uCameraPos - vWorldPos);
    float fog = 1.0 - exp(-uFogDensity * dist * 0.7);
    color = mix(color, srgbToLinear(uFogColor), clamp(fog, 0.0, 0.85));

    float alpha = 0.88 * shoreFade;
    if (uClipRadius > 0.0) {
        float r = length(vLocalXZ);
        alpha *= smoothstep(uClipRadius, uClipRadius * 0.82, r);
    }

    FragColor = vec4(linearToSrgb(color), alpha);
}
