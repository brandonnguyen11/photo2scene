#version 410 core
// Gerstner waves + time-evolving fBm (Brownian-like) surface motion
layout(location = 0) in vec3 aPos;
layout(location = 1) in vec3 aNormal;
layout(location = 2) in vec2 aUV;

uniform mat4 uModel;
uniform mat4 uView;
uniform mat4 uProj;
uniform float uTime;
uniform float uWaveScale;  // 1 = ocean, ~0.2 = calm fountain

out vec3 vWorldPos;
out vec3 vNormal;
out vec2 vUV;
out float vViewDepth;
out vec2 vLocalXZ;

float hash21(vec2 p) {
    return fract(sin(dot(p, vec2(127.1, 311.7))) * 43758.5453);
}

float noise21(vec2 p) {
    vec2 i = floor(p);
    vec2 f = fract(p);
    float a = hash21(i);
    float b = hash21(i + vec2(1.0, 0.0));
    float c = hash21(i + vec2(0.0, 1.0));
    float d = hash21(i + vec2(1.0, 1.0));
    vec2 u = f * f * (3.0 - 2.0 * f);
    return mix(mix(a, b, u.x), mix(c, d, u.x), u.y);
}

// Fractional Brownian motion — layered noise for irregular / Brownian-like motion.
float fbm(vec2 p) {
    float v = 0.0;
    float a = 0.5;
    mat2 rot = mat2(0.80, -0.60, 0.60, 0.80);
    for (int i = 0; i < 5; ++i) {
        v += a * noise21(p);
        p = rot * p * 2.02 + vec2(1.7, 9.2);
        a *= 0.5;
    }
    return v;
}

struct Wave {
    vec2 dir;
    float amplitude;
    float wavelength;
    float speed;
};

void gerstner(vec3 pos, float t, float ampScale, out vec3 displaced, out vec3 normal) {
    Wave waves[5];
    waves[0] = Wave(normalize(vec2(0.35, 0.94)), 0.28, 22.0, 0.85);
    waves[1] = Wave(normalize(vec2(-0.75, 0.45)), 0.16, 11.0, 1.15);
    waves[2] = Wave(normalize(vec2(0.92, -0.25)), 0.10, 6.5, 1.45);
    waves[3] = Wave(normalize(vec2(-0.25, -0.97)), 0.07, 3.8, 1.85);
    waves[4] = Wave(normalize(vec2(0.55, 0.55)), 0.04, 2.2, 2.25);

    vec3 p = pos;
    vec3 n = vec3(0.0, 1.0, 0.0);
    for (int i = 0; i < 5; ++i) {
        float amp = waves[i].amplitude * ampScale;
        float w = 6.2831853 / waves[i].wavelength;
        float phase = w * dot(waves[i].dir, pos.xz) - waves[i].speed * t;
        float qa = mix(0.25, 0.55, ampScale);  // less steep for calm pools
        p.x += qa * amp * waves[i].dir.x * cos(phase);
        p.z += qa * amp * waves[i].dir.y * cos(phase);
        p.y += amp * sin(phase);

        n.x -= waves[i].dir.x * w * amp * cos(phase);
        n.z -= waves[i].dir.y * w * amp * cos(phase);
        n.y -= qa * w * amp * sin(phase);
    }
    displaced = p;
    normal = normalize(n);
}

// Slow drifting noise field — looks like Brownian agitation of the surface.
void brownianRipple(vec2 xz, float t, float ampScale, inout vec3 p, inout vec3 n) {
    // Incommensurate drift speeds so the pattern never loops cleanly.
    vec2 driftA = xz * 0.11 + vec2(t * 0.07, t * 0.045);
    vec2 driftB = xz * 0.27 + vec2(-t * 0.11, t * 0.083);
    vec2 driftC = xz * 0.55 + vec2(t * 0.19, -t * 0.13);

    // Domain warp (Brownian-like path through noise space).
    vec2 warp = vec2(fbm(driftA), fbm(driftA + 17.3));
    float h1 = fbm(driftB + warp * 1.5);
    float h2 = fbm(driftC + warp.yx * 2.0);
    float h = (h1 * 0.65 + h2 * 0.35) * 2.0 - 1.0;

    float amp = 0.22 * ampScale;
    p.y += h * amp;

    // Approximate normal from finite differences in noise.
    float e = 0.35;
    float hx = fbm((driftB + vec2(e, 0.0)) + warp * 1.5);
    float hz = fbm((driftB + vec2(0.0, e)) + warp * 1.5);
    n.x -= (hx - h1) / e * amp * 2.5;
    n.z -= (hz - h1) / e * amp * 2.5;
    n = normalize(n);
}

void main() {
    float ampScale = max(uWaveScale, 0.05);
    vec3 disp, n;
    gerstner(aPos, uTime, ampScale, disp, n);
    brownianRipple(aPos.xz, uTime, ampScale, disp, n);

    vec4 world = uModel * vec4(disp, 1.0);
    vec4 viewPos = uView * world;
    vWorldPos = world.xyz;
    vNormal = normalize(mat3(uModel) * n);
    vUV = aUV;
    vLocalXZ = aPos.xz;
    vViewDepth = -viewPos.z;
    gl_Position = uProj * viewPos;
}
