#version 410 core
// Gerstner wave ocean (WA-1)
layout(location = 0) in vec3 aPos;
layout(location = 1) in vec3 aNormal;
layout(location = 2) in vec2 aUV;

uniform mat4 uModel;
uniform mat4 uView;
uniform mat4 uProj;
uniform float uTime;

out vec3 vWorldPos;
out vec3 vNormal;
out vec2 vUV;
out float vViewDepth;

struct Wave {
    vec2 dir;
    float amplitude;
    float wavelength;
    float speed;
};

void gerstner(vec3 pos, float t, out vec3 displaced, out vec3 normal) {
    Wave waves[5];
    waves[0] = Wave(normalize(vec2(0.4, 0.9)), 0.35, 18.0, 1.1);
    waves[1] = Wave(normalize(vec2(-0.7, 0.5)), 0.18, 9.0, 1.4);
    waves[2] = Wave(normalize(vec2(0.9, -0.2)), 0.12, 5.5, 1.8);
    waves[3] = Wave(normalize(vec2(-0.3, -0.95)), 0.08, 3.2, 2.1);
    waves[4] = Wave(normalize(vec2(0.6, 0.4)), 0.05, 2.0, 2.4);

    vec3 p = pos;
    vec3 n = vec3(0.0, 1.0, 0.0);
    for (int i = 0; i < 5; ++i) {
        float w = 6.2831853 / waves[i].wavelength;
        float phase = w * dot(waves[i].dir, pos.xz) - waves[i].speed * t;
        float qa = 0.55; // steepness factor
        p.x += qa * waves[i].amplitude * waves[i].dir.x * cos(phase);
        p.z += qa * waves[i].amplitude * waves[i].dir.y * cos(phase);
        p.y += waves[i].amplitude * sin(phase);

        n.x -= waves[i].dir.x * w * waves[i].amplitude * cos(phase);
        n.z -= waves[i].dir.y * w * waves[i].amplitude * cos(phase);
        n.y -= qa * w * waves[i].amplitude * sin(phase);
    }
    displaced = p;
    normal = normalize(n);
}

void main() {
    vec3 local = aPos;
    vec3 disp, n;
    gerstner(local, uTime, disp, n);
    vec4 world = uModel * vec4(disp, 1.0);
    vec4 viewPos = uView * world;
    vWorldPos = world.xyz;
    vNormal = normalize(mat3(uModel) * n);
    vUV = aUV;
    vViewDepth = -viewPos.z;
    gl_Position = uProj * viewPos;
}
