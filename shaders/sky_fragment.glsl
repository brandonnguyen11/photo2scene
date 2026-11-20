#version 410 core
// sky_fragment.glsl — vertical gradient background based on the view ray.
in vec2 vNdc;
out vec4 FragColor;

uniform mat4 uInvViewProj;
uniform vec3 uCameraPos;
uniform vec3 uSkyTop;
uniform vec3 uSkyHorizon;
uniform vec3 uSkyBottom;

void main() {
    vec4 farPoint = uInvViewProj * vec4(vNdc, 1.0, 1.0);
    vec3 dir = normalize(farPoint.xyz / farPoint.w - uCameraPos);
    vec3 color = dir.y >= 0.0
        ? mix(uSkyHorizon, uSkyTop, pow(clamp(dir.y, 0.0, 1.0), 0.6))
        : mix(uSkyHorizon, uSkyBottom, pow(clamp(-dir.y, 0.0, 1.0), 0.4));
    FragColor = vec4(color, 1.0);
}
