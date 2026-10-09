#version 450
#extension GL_GOOGLE_include_directive : require
#include "frame.glsl"
layout(location=0) in vec4 inCentreA0;
layout(location=1) in vec4 inMajorA1;
layout(location=2) in vec4 inMinor;
layout(location=3) in vec4 inColor;
layout(location=0) out vec4 vColor;
layout(location=1) out vec3 vWorld;
void main() {
    float angle=mix(inCentreA0.w,inMajorA1.w,float(gl_VertexIndex)/255.0);
    vWorld=inCentreA0.xyz+inMajorA1.xyz*cos(angle)+inMinor.xyz*sin(angle);
    gl_Position=u.viewProj*vec4(vWorld,1);
    gl_Position.z-=2e-4*gl_Position.w;
    vColor=inColor;
}
