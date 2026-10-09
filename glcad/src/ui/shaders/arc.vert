#version 450
#extension GL_GOOGLE_include_directive : require
#include "frame.glsl"
#include "stroke.glsl"
layout(location = 0) in vec4 inCentreA0;   // centre.xyz, start angle
layout(location = 1) in vec4 inMajorA1;    // U * major radius, end angle
layout(location = 2) in vec4 inMinor;      // V * minor radius, unused
layout(location = 3) in vec4 inColor;
layout(location = 0) out vec4 vColor;

layout(location=1) out vec3 vWorld;
void main() {
    int segment=gl_VertexIndex/6;
    float a0=mix(inCentreA0.w,inMajorA1.w,float(segment)/255.0);
    float a1=mix(inCentreA0.w,inMajorA1.w,float(segment+1)/255.0);
    vec3 start=inCentreA0.xyz+inMajorA1.xyz*cos(a0)+inMinor.xyz*sin(a0);
    vec3 end=inCentreA0.xyz+inMajorA1.xyz*cos(a1)+inMinor.xyz*sin(a1);
    stroke(start,end,inMinor.w,gl_VertexIndex%6,gl_Position,vWorld);
    vColor=inColor;
}
