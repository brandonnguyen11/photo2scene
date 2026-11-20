#version 410 core
// unlit_fragment.glsl — flat color (light gizmos).
out vec4 FragColor;
uniform vec3 uColor;

void main() {
    FragColor = vec4(uColor, 1.0);
}
