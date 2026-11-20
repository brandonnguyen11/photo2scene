#version 410 core
// shadow_vertex.glsl — renders depth from the directional light's point of view.
layout(location = 0) in vec3 aPos;

uniform mat4 uLightSpace;
uniform mat4 uModel;

void main() {
    gl_Position = uLightSpace * uModel * vec4(aPos, 1.0);
}
