#version 410 core
// fragment.glsl — Photo2Scene custom lighting model (Member 2: Lighting Engine)
//
//   color = ambient * albedo
//         + shadow * sun   * (albedo * N.L + specular)
//         + sum_i atten_i  * point_i * (albedo * N.L + specular)
//
// Diffuse: Lambert. Specular: Phong (R.V)^n or Blinn-Phong (N.H)^n.
// All lighting is computed in linear space; the photo texture is decoded from
// sRGB and the result is re-encoded to sRGB at the end.

#define MAX_POINT_LIGHTS 4

in vec3 vWorldPos;
in vec3 vNormal;
in vec2 vUV;
in vec4 vLightSpacePos;
in float vViewDepth;

out vec4 FragColor;

// Material / surface
uniform sampler2D uAlbedoTex;
uniform bool uUseTexture;
uniform vec3 uBaseColor;      // sRGB
uniform bool uChecker;
uniform int uMaterial;        // 0 plain, 1 grass, 2 gravel, 3 hedge, 4 sand, 5 stone, 6 water

// Camera / debug views
uniform vec3 uCameraPos;
uniform int uViewMode;        // 0 lit, 1 unlit, 2 normals, 3 depth
uniform vec2 uDepthRange;

// Lighting
uniform bool uLightingEnabled;
uniform vec3 uAmbient;        // linear color * intensity
uniform bool uBlinn;
uniform float uSpecularStrength;
uniform float uShininess;

struct DirLight {
    bool enabled;
    vec3 direction;           // direction light travels
    vec3 color;               // linear color * intensity
};
uniform DirLight uSun;

struct PointLight {
    bool enabled;
    vec3 position;
    vec3 color;               // linear color * intensity
    vec3 attenuation;         // constant, linear, quadratic
};
uniform PointLight uPoints[MAX_POINT_LIGHTS];

// Shadows
uniform sampler2D uShadowMap;
uniform bool uShadowsEnabled;
uniform float uShadowBias;
uniform int uPcfRadius;

// Aerial perspective (S-5)
uniform float uFogDensity;
uniform vec3 uFogColor;

vec3 srgbToLinear(vec3 c) {
    return mix(c / 12.92, pow((c + 0.055) / 1.055, vec3(2.4)), step(vec3(0.04045), c));
}

vec3 linearToSrgb(vec3 c) {
    c = clamp(c, 0.0, 1.0);
    return mix(c * 12.92, 1.055 * pow(c, vec3(1.0 / 2.4)) - 0.055, step(vec3(0.0031308), c));
}

// 0 = fully shadowed, 1 = fully lit. Percentage-closer filtering over a
// (2r+1)^2 neighborhood softens the edges.
float shadowFactor(vec3 N, vec3 L) {
    if (!uShadowsEnabled) return 1.0;
    vec3 p = vLightSpacePos.xyz / vLightSpacePos.w;
    p = p * 0.5 + 0.5;
    if (p.z > 1.0) return 1.0;

    float ndl = max(dot(N, L), 0.0);
    float bias = uShadowBias * (1.0 + 4.0 * (1.0 - ndl));   // more bias at grazing angles
    vec2 texel = 1.0 / vec2(textureSize(uShadowMap, 0));

    float lit = 0.0;
    int samples = 0;
    for (int x = -uPcfRadius; x <= uPcfRadius; ++x) {
        for (int y = -uPcfRadius; y <= uPcfRadius; ++y) {
            float closest = texture(uShadowMap, p.xy + vec2(x, y) * texel).r;
            lit += (p.z - bias > closest) ? 0.0 : 1.0;
            ++samples;
        }
    }
    return lit / float(max(samples, 1));
}

vec3 specular(vec3 N, vec3 L, vec3 V) {
    float s;
    if (uBlinn) {
        vec3 H = normalize(L + V);
        s = pow(max(dot(N, H), 0.0), uShininess);
    } else {
        vec3 R = reflect(-L, N);
        s = pow(max(dot(R, V), 0.0), uShininess);
    }
    return vec3(uSpecularStrength * s);
}

// Lambert diffuse + specular for one light (radiance already includes color).
vec3 shade(vec3 albedo, vec3 N, vec3 L, vec3 V, vec3 radiance) {
    float ndl = max(dot(N, L), 0.0);
    vec3 spec = ndl > 0.0 ? specular(N, L, V) : vec3(0.0);
    return radiance * (albedo * ndl + spec);
}

float hash21(vec2 p) {
    return fract(sin(dot(p, vec2(127.1, 311.7))) * 43758.5453);
}
float noise21(vec2 p) {
    vec2 i = floor(p);
    vec2 f = fract(p);
    float a = hash21(i), b = hash21(i + vec2(1, 0));
    float c = hash21(i + vec2(0, 1)), d = hash21(i + vec2(1, 1));
    vec2 u = f * f * (3.0 - 2.0 * f);
    return mix(mix(a, b, u.x), mix(c, d, u.x), u.y);
}
float fbm21(vec2 p) {
    float v = 0.0, a = 0.5;
    for (int i = 0; i < 5; ++i) { v += a * noise21(p); p *= 2.03; a *= 0.5; }
    return v;
}

vec3 proceduralAlbedo(vec3 baseLin) {
    if (uMaterial == 1) { // grass lawn
        float n = fbm21(vWorldPos.xz * 1.8);
        float blades = fbm21(vWorldPos.xz * 12.0);
        vec3 dark = baseLin * 0.55;
        vec3 light = baseLin * 1.25;
        return mix(dark, light, n) * mix(0.85, 1.15, blades);
    }
    if (uMaterial == 2) { // gravel
        float n = fbm21(vWorldPos.xz * 8.0);
        float peb = step(0.62, noise21(vWorldPos.xz * 28.0));
        return baseLin * mix(0.7, 1.2, n) * mix(1.0, 0.75, peb);
    }
    if (uMaterial == 3) { // leafy hedge
        float n = fbm21(vWorldPos.xz * 4.0 + vWorldPos.y * 3.0);
        float leaf = fbm21(vWorldPos.xy * 18.0 + vWorldPos.z * 18.0);
        return baseLin * mix(0.65, 1.35, n) * mix(0.9, 1.2, leaf);
    }
    if (uMaterial == 4) { // sand
        float n = fbm21(vWorldPos.xz * 2.5);
        float rip = sin(vWorldPos.x * 3.5 + n * 4.0) * 0.5 + 0.5;
        vec3 wet = baseLin * vec3(0.55, 0.5, 0.42);
        return mix(baseLin * mix(0.85, 1.15, n), wet, smoothstep(0.3, -0.2, vWorldPos.y) * 0.65)
             * mix(0.95, 1.08, rip);
    }
    if (uMaterial == 5) { // stone
        float n = fbm21(vWorldPos.xz * 3.0 + vWorldPos.y * 3.0);
        return baseLin * mix(0.75, 1.2, n);
    }
    return baseLin;
}

void main() {
    vec3 albedo = uUseTexture ? srgbToLinear(texture(uAlbedoTex, vUV).rgb)
                              : srgbToLinear(uBaseColor);
    albedo = proceduralAlbedo(albedo);
    if (uChecker) {
        vec2 cell = floor(vWorldPos.xz * 2.0);
        albedo *= mix(0.8, 1.0, mod(cell.x + cell.y, 2.0));
    }

    vec3 N = normalize(vNormal);
    if (!gl_FrontFacing) N = -N;   // two-sided shading for the open depth surface
    // Cheap procedural normal detail for terrain-like materials.
    if (uMaterial == 1 || uMaterial == 4 || uMaterial == 2) {
        float nx = fbm21(vWorldPos.xz * 3.0) - 0.5;
        float nz = fbm21(vWorldPos.xz * 3.0 + 9.1) - 0.5;
        N = normalize(N + vec3(nx, 0.0, nz) * 0.35);
    }

    if (uViewMode == 2) {          // normals
        FragColor = vec4(N * 0.5 + 0.5, 1.0);
        return;
    }
    if (uViewMode == 3) {          // depth (near = white)
        float t = clamp((vViewDepth - uDepthRange.x) / max(uDepthRange.y - uDepthRange.x, 1e-4),
                        0.0, 1.0);
        FragColor = vec4(vec3(1.0 - t), 1.0);
        return;
    }
    if (uViewMode == 1 || !uLightingEnabled) {   // unlit: original photo colors
        FragColor = vec4(linearToSrgb(albedo), 1.0);
        return;
    }

    vec3 V = normalize(uCameraPos - vWorldPos);
    vec3 color = uAmbient * albedo;

    if (uSun.enabled) {
        vec3 L = normalize(-uSun.direction);
        color += shadowFactor(N, L) * shade(albedo, N, L, V, uSun.color);
    }

    for (int i = 0; i < MAX_POINT_LIGHTS; ++i) {
        if (!uPoints[i].enabled) continue;
        vec3 toLight = uPoints[i].position - vWorldPos;
        float d = length(toLight);
        vec3 L = toLight / max(d, 1e-5);
        vec3 a = uPoints[i].attenuation;
        float atten = 1.0 / max(a.x + a.y * d + a.z * d * d, 1e-4);
        color += atten * shade(albedo, N, L, V, uPoints[i].color);
    }

    float dist = length(uCameraPos - vWorldPos);
    float fog = 1.0 - exp(-uFogDensity * dist);
    color = mix(color, srgbToLinear(uFogColor), clamp(fog, 0.0, 1.0));

    FragColor = vec4(linearToSrgb(color), 1.0);
}
