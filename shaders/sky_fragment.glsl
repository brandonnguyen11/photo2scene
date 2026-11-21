#version 410 core
// Analytic sky with sun disk, horizon haze, and scrolling clouds (S-1, S-2, S-6)
in vec2 vNdc;
out vec4 FragColor;

uniform mat4 uInvViewProj;
uniform vec3 uCameraPos;
uniform vec3 uSunDir;          // toward the sun
uniform vec3 uSunColor;
uniform float uSunIntensity;
uniform vec3 uZenithColor;
uniform vec3 uHorizonColor;
uniform float uTurbidity;
uniform float uCloudCover;
uniform float uTime;
uniform bool uIsNight;

float hash(vec2 p) {
    return fract(sin(dot(p, vec2(127.1, 311.7))) * 43758.5453);
}
float noise(vec2 p) {
    vec2 i = floor(p);
    vec2 f = fract(p);
    float a = hash(i);
    float b = hash(i + vec2(1.0, 0.0));
    float c = hash(i + vec2(0.0, 1.0));
    float d = hash(i + vec2(1.0, 1.0));
    vec2 u = f * f * (3.0 - 2.0 * f);
    return mix(mix(a, b, u.x), mix(c, d, u.x), u.y);
}
float fbm(vec2 p) {
    float v = 0.0;
    float a = 0.5;
    for (int i = 0; i < 5; ++i) {
        v += a * noise(p);
        p = p * 2.15 + vec2(1.7, 9.2);
        a *= 0.5;
    }
    return v;
}

void main() {
    vec4 farPoint = uInvViewProj * vec4(vNdc, 1.0, 1.0);
    vec3 dir = normalize(farPoint.xyz / farPoint.w - uCameraPos);
    vec3 sun = normalize(uSunDir);

    float elev = dir.y;
    float haze = clamp((uTurbidity - 1.5) / 6.0, 0.0, 1.0);

    // View-dependent sky: deeper blue at zenith, bright horizon, sun-side warm.
    float zenithT = pow(clamp(1.0 - max(elev, 0.0), 0.0, 1.0), mix(2.8, 1.4, haze));
    vec3 sky = mix(uZenithColor, uHorizonColor, zenithT);

    // Mie-ish glow around the sun direction across the sky.
    float mu = max(dot(dir, sun), 0.0);
    float mie = pow(mu, mix(16.0, 6.0, haze));
    sky += uSunColor * uSunIntensity * mie * mix(0.35, 0.7, haze);

    // Extra warm scattering near the horizon toward the sun.
    float horizonBand = exp(-abs(elev) * 8.0);
    sky += uSunColor * horizonBand * (0.15 + 0.35 * pow(mu, 4.0)) * uSunIntensity * 0.25;

    if (elev < 0.0) {
        sky = mix(uHorizonColor * 0.65, uHorizonColor * 0.18, clamp(-elev * 1.8, 0.0, 1.0));
    }

    // Soft scrolling clouds.
    if (elev > 0.03 && uCloudCover > 0.01) {
        vec2 cuv = dir.xz / max(elev, 0.08) * 0.28 + vec2(uTime * 0.01, uTime * 0.004);
        float clouds = fbm(cuv);
        float mask = smoothstep(1.0 - uCloudCover * 0.9, 1.0 - uCloudCover * 0.2, clouds);
        mask *= smoothstep(0.02, 0.18, elev);
        vec3 cloudCol = mix(vec3(0.92, 0.94, 0.98), uSunColor, 0.2 + 0.35 * pow(mu, 8.0));
        if (uIsNight) cloudCol = vec3(0.07, 0.08, 0.12);
        sky = mix(sky, cloudCol, mask * 0.85);
    }

    // Sun disk + corona.
    if (!uIsNight && sun.y > -0.02) {
        float disk = smoothstep(0.9994, 0.99985, mu);
        float corona = pow(mu, 120.0) * 1.8 + pow(mu, 24.0) * 0.45;
        sky += uSunColor * uSunIntensity * (disk * 12.0 + corona);
    }

    if (uIsNight && elev > 0.08) {
        float star = step(0.9965, hash(floor(dir.xy * 260.0)));
        sky += vec3(star * 0.9);
    }

    FragColor = vec4(clamp(sky, 0.0, 2.0), 1.0);
}
