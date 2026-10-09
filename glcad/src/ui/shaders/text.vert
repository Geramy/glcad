#version 450
#extension GL_GOOGLE_include_directive : require
layout(location = 0) in vec2 iPos;
layout(location = 1) in vec2 iUV;
layout(location = 2) in vec4 iColor;
layout(location = 3) in vec4 iClip;

layout(location = 0) out vec2 vUV;
layout(location = 1) out vec4 vColor;
layout(location = 2) out vec4 vClip;
layout(location = 3) out vec2 vScreen;

#include "frame.glsl"

void main() {
    vUV = iUV;
    vColor = iColor;
    vClip = iClip;
    vScreen = iPos;
    vec2 ndc = (iPos / u.viewport.xy) * 2.0 - 1.0;
    gl_Position = vec4(ndc, 0.0, 1.0);
}
