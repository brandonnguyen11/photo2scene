#version 410 core
in vec3 vWorldPos;
in vec3 vNormal;
in vec2 vUV;
in vec4 vLightSpacePos;
in float vViewDepth;
in float vHeight01;
out vec4 FragColor;

uniform vec3 uCameraPos;
uniform vec3 uBaseColor;
uniform vec3 uAmbient;
uniform vec3 uSunDir;      // travel direction
uniform vec3 uSunColor;
uniform bool uSunEnabled;
uniform sampler2D uShadowMap;
uniform bool uShadowsEnabled;
uniform float uShadowBias;
uniform float uFogDensity;
uniform vec3 uFogColor;

vec3 srgbToLinear(vec3 c) {
    return mix(c / 12.92, pow((c + 0.055) / 1.055, vec3(2.4)), step(vec3(0.04045), c));
}
vec3 linearToSrgb(vec3 c) {
    c = clamp(c, 0.0, 1.0);
    return mix(c * 12.92, 1.055 * pow(c, vec3(1.0 / 2.4)) - 0.055, step(vec3(0.0031308), c));
}

float shadowFactor(vec3 N, vec3 L) {
    if (!uShadowsEnabled) return 1.0;
    vec3 p = vLightSpacePos.xyz / vLightSpacePos.w;
    p = p * 0.5 + 0.5;
    if (p.z > 1.0) return 1.0;
    float bias = uShadowBias * 2.0;
    float closest = texture(uShadowMap, p.xy).r;
    return (p.z - bias > closest) ? 0.35 : 1.0;
}

void main() {
    vec3 albedo = srgbToLinear(mix(uBaseColor * 0.55, uBaseColor, vHeight01));
    vec3 N = normalize(vNormal);
    vec3 V = normalize(uCameraPos - vWorldPos);
    vec3 color = uAmbient * albedo;
    if (uSunEnabled) {
        vec3 L = normalize(-uSunDir);
        float ndl = max(dot(N, L), 0.15);
        color += shadowFactor(N, L) * uSunColor * albedo * ndl;
    }
    float dist = length(uCameraPos - vWorldPos);
    float fog = 1.0 - exp(-uFogDensity * dist);
    color = mix(color, srgbToLinear(uFogColor), clamp(fog, 0.0, 1.0));
    FragColor = vec4(linearToSrgb(color), 1.0);
}
