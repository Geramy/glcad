#version 450
#extension GL_GOOGLE_include_directive : require
#include "frame.glsl"
layout(location = 0) in vec3 iPos;
layout(location = 1) in vec3 iNormal;
layout(location = 2) in vec4 iColor;

layout(location = 0) out vec3 vNormal;
layout(location = 1) out vec4 vColor;
layout(location = 2) out vec3 vView;
layout(location = 3) out vec3 vWorld;

void main() {
    vNormal = iNormal;
    vColor  = iColor;
    vView   = u.eye.xyz - iPos;
    vWorld  = iPos;
    gl_Position = u.viewProj * vec4(iPos, 1.0);
}
