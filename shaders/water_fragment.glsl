#version 410 core
// Fresnel water: sky reflection + depth color + sun glint (WA-2)
in vec3 vWorldPos;
in vec3 vNormal;
in vec2 vUV;
in float vViewDepth;
out vec4 FragColor;

uniform vec3 uCameraPos;
uniform vec3 uSunDir;       // travel
uniform vec3 uSunColor;
uniform vec3 uDeepColor;
uniform vec3 uShallowColor;
uniform vec3 uHorizonColor;
uniform vec3 uZenithColor;
uniform float uFogDensity;
uniform vec3 uFogColor;

vec3 srgbToLinear(vec3 c) {
    return mix(c / 12.92, pow((c + 0.055) / 1.055, vec3(2.4)), step(vec3(0.04045), c));
}
vec3 linearToSrgb(vec3 c) {
    c = clamp(c, 0.0, 1.0);
    return mix(c * 12.92, 1.055 * pow(c, vec3(1.0 / 2.4)) - 0.055, step(vec3(0.0031308), c));
}

void main() {
    vec3 N = normalize(vNormal);
    vec3 V = normalize(uCameraPos - vWorldPos);
    float fresnel = pow(1.0 - max(dot(N, V), 0.0), 4.0);
    fresnel = mix(0.08, 1.0, fresnel);

    float depthFactor = clamp((0.4 - vWorldPos.y) * 0.35 + 0.35, 0.0, 1.0);
    vec3 waterCol = mix(srgbToLinear(uShallowColor), srgbToLinear(uDeepColor), depthFactor);

    vec3 R = reflect(-V, N);
    float skyT = pow(1.0 - max(R.y, 0.0), 1.5);
    vec3 skyCol = mix(srgbToLinear(uZenithColor), srgbToLinear(uHorizonColor), skyT);

    vec3 L = normalize(-uSunDir);
    vec3 H = normalize(L + V);
    float spec = pow(max(dot(N, H), 0.0), 256.0) * 2.2;

    vec3 color = mix(waterCol, skyCol, fresnel);
    color += uSunColor * spec;

    float dist = length(uCameraPos - vWorldPos);
    float fog = 1.0 - exp(-uFogDensity * dist * 0.7);
    color = mix(color, srgbToLinear(uFogColor), clamp(fog, 0.0, 0.85));
    FragColor = vec4(linearToSrgb(color), 0.92);
}
