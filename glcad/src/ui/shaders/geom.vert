#version 450
#extension GL_GOOGLE_include_directive : require
#include "frame.glsl"
layout(location = 0) in vec3 inPos;
layout(location = 1) in vec4 inColor;
layout(location = 0) out vec4 vColor;
layout(location = 1) out vec3 vWorld;

void main() {
    vWorld = inPos;
    gl_Position = u.viewProj * vec4(inPos, 1.0);
    // Nudged toward the eye so a line wins the depth tie with its surface.
    gl_Position.z -= 2e-4 * gl_Position.w;
    gl_PointSize = 9.0;
    vColor = inColor;
}
