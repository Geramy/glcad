#version 450
#extension GL_GOOGLE_include_directive : require
#include "frame.glsl"
layout(location = 0) in vec2 vUV;
layout(location = 1) in vec4 vColor;
layout(location = 2) in vec4 vClip;
layout(location = 3) in vec2 vScreen;
layout(location = 0) out vec4 outColor;

void main() {
    if (vScreen.x < vClip.x || vScreen.x > vClip.x + vClip.z ||
        vScreen.y < vClip.y || vScreen.y > vClip.y + vClip.w)
        discard;
    outColor = vColor;
}
