#version 450
#extension GL_GOOGLE_include_directive : require
#include "frame.glsl"
layout(location = 0) in vec4 vColor;
layout(location = 1) in vec3 vWorld;
layout(location = 0) out vec4 outColor;
void main() {
    if (dot(u.section.xyz, u.section.xyz) > 1e-12 &&
        dot(u.section.xyz, vWorld) > u.section.w)
        discard;
    outColor = vColor;
}
