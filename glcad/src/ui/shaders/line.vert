#version 450
#extension GL_GOOGLE_include_directive : require
#include "frame.glsl"
#include "stroke.glsl"
layout(location=0) in vec3 inStart;
layout(location=1) in vec4 inColor;
layout(location=2) in float inWidth;
layout(location=3) in vec3 inEnd;
layout(location=0) out vec4 vColor;
layout(location=1) out vec3 vWorld;
void main() {
    stroke(inStart,inEnd,inWidth,gl_VertexIndex,gl_Position,vWorld);
    vColor=inColor;
}
