#version 410 core
// vertex.glsl — shared vertex shader for the lit scene and unlit gizmos.

layout(location = 0) in vec3 aPos;
layout(location = 1) in vec3 aNormal;
layout(location = 2) in vec2 aUV;

uniform mat4 uModel;
uniform mat4 uView;
uniform mat4 uProj;
uniform mat3 uNormalMatrix;   // transpose(inverse(mat3(uModel)))
uniform mat4 uLightSpace;     // directional-light view-projection for shadows

out vec3 vWorldPos;
out vec3 vNormal;
out vec2 vUV;
out vec4 vLightSpacePos;
out float vViewDepth;

void main() {
    vec4 world = uModel * vec4(aPos, 1.0);
    vec4 viewPos = uView * world;
    vWorldPos = world.xyz;
    vNormal = uNormalMatrix * aNormal;
    vUV = aUV;
    vLightSpacePos = uLightSpace * world;
    vViewDepth = -viewPos.z;
    gl_Position = uProj * viewPos;
}
