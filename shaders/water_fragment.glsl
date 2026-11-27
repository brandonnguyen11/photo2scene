#version 410 core 
// Fresnel water + circular pool / shore clipping + beach shoreline
in vec3 vWorldPos;
in vec3 vNormal;
in vec2 vUV;
in float vViewDepth;
in vec2 vLocalXZ;
in float vTime;
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
float hash21(vec2 p) { return fract(sin(dot(p, vec2(127.1, 311.7))) * 43758.5453); }
float noise21(vec2 p) {
    vec2 i = floor(p), f = fract(p);
    vec2 u = f * f * (3.0 - 2.0 * f);
    return mix(mix(hash21(i), hash21(i + vec2(1,0)), u.x),
    mix(hash21(i + vec2(0,1)), hash21(i + vec2(1, 1)), u.x), u.y);
}

void main() {
    // Circular fountain pool — hard clip to the disc (not a square grid).
    if (uClipRadius > 0.0 && length(vLocalXZ) > uClipRadius) discard; 
    bool beach = uShoreZ < 1.0e5;
    float shoreDist = 1.0e4;
    float shoreFade = 1.0;
    if (beach) {
        if (vWorldPos.z > uShoreZ) discard;
        shoreDist = uShoreZ - vWorldPos.z;
        shoreFade = smoothstep(0.0, 2.5, shoreDist);
    }

    vec3 N = normalize(vNormal);
    vec3 V = normalize(uCameraPos - vWorldPos);
    float fresnel = mix(0.1, 1.0, pow(1.0 - max(dot(N, V), 0.0), 3.5));

    // Turquoise shallows -> deep blue offshore
    float depthFactor = beach
        ? smoothstep(2.0, 40.0, shoreDist)
        : clamp((uWaterCenter.y + 0.6 - vWorldPos.y) * 0.5 + 0.4, 0.0, 1.0);
    vec3 waterCol = mix(srgbToLinear(uShallowColor), srgbToLinear(uDeepColor), depthFactor);

    vec3 R = reflect(-V, N);
    float skyT = pow(1.0 - max(R.y, 0.0), 1.4);
    vec3 skyCol = mix(srgbToLinear(uZenithColor), srgbToLinear(uHorizonColor), skyT);

    vec3 L = normalize(-uSunDir);
    vec3 H = normalize(L + V);
    float spec = pow(max(dot(N, H), 0.0), 220.0) * 1.8;

    vec3 color = mix(waterCol, skyCol, fresnel);
    color += uSunColor * spec;

    // Shoreline foam: a band that surges in and out, broken up by noise
    float foam = 0.0;
    if (beach) {
        float w = 1.2 + 1.6 * (0.5 + 0.5 * sin(vTime * 0.7 + vWorldPos.x * 0.11));
        float n = noise21(vWorldPos.xz * 2.5 + vec2(vTime * 0.2, 0.0));
        foam = (1.0 - smoothstep(0.0, w, shoreDist)) * (0.45 + 0.55 * n);
        color = mix(color, vec3(0.85, 0.88, 0.9), foam * 0.9);
    }

    // Fog, then fade into the sky's horizon color so the ocean has no visible edge
    float dist = length(uCameraPos - vWorldPos);
    float fog = 1.0 - exp(-uFogDensity * dist * 0.7);
    color = mix(color, srgbToLinear(uFogColor), clamp(fog, 0.0, 0.85));
    float horizonBlend = smoothstep(120.0, 1500.0, dist);
    color = mix(color, srgbToLinear(uHorizonColor), horizonBlend);

    float alpha = beach ? mix(0.45, 0.92, smoothstep(0.0, 14.0, shoreDist)) : 0.88;
    alpha *= shoreFade;
    alpha = max(alpha, foam * 0.9);
    alpha = mix(alpha, 1.0, horizonBlend);
    if (uClipRadius > 0.0) {
        alpha *= smoothstep(uClipRadius, uClipRadius * 0.82, length(vLocalXZ));
    }

    FragColor = vec4(linearToSrgb(color), alpha);
}
   