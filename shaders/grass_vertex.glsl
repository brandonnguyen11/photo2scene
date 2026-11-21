#version 410 core
layout(location = 0) in vec3 aPos;
layout(location = 1) in vec3 aNormal;
layout(location = 2) in vec2 aUV;
layout(location = 3) in vec4 aPosScale; // xyz + scale
layout(location = 4) in float aYaw;
layout(location = 5) in float aPhase;

uniform mat4 uView;
uniform mat4 uProj;
uniform mat4 uLightSpace;
uniform float uTime;
uniform float uWind;

out vec3 vWorldPos;
out vec3 vNormal;
out vec2 vUV;
out vec4 vLightSpacePos;
out float vViewDepth;
out float vHeight01;

void main() {
    float c = cos(aYaw), s = sin(aYaw);
    mat2 rot = mat2(c, -s, s, c);
    vec3 local = aPos;
    local.xz = rot * local.xz;

    // Bend more toward the tip (wind waves travel across the lawn).
    float h = clamp(aPos.y / 0.55, 0.0, 1.0);
    float gust = sin(uTime * (1.4 + uWind) + aPosScale.x * 0.35 + aPosScale.z * 0.28 + aPhase);
    float sway = gust * uWind * 0.35 * h * h;
    local.x += sway;
    local.z += sway * 0.55;

    vec3 world = vec3(aPosScale.x, aPosScale.y, aPosScale.z) + local * aPosScale.w;
    vec4 viewPos = uView * vec4(world, 1.0);
    vWorldPos = world;
    vNormal = normalize(vec3(s * 0.15, 1.0, c * 0.15));
    vUV = aUV;
    vLightSpacePos = uLightSpace * vec4(world, 1.0);
    vViewDepth = -viewPos.z;
    vHeight01 = h;
    gl_Position = uProj * viewPos;
}
