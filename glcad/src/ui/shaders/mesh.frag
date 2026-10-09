#version 450
#extension GL_GOOGLE_include_directive : require
#include "frame.glsl"
#include "view_ray.glsl"
layout(location = 0) in vec3 vNormal;
layout(location = 1) in vec4 vColor;
layout(location = 2) in vec3 vView;
layout(location = 3) in vec3 vWorld;

#include "mesh_shading.glsl"
layout(location = 0) out vec4 outColor;
void main() {
    vec4 clip = u.viewProj * vec4(vWorld,1);
    vec2 sheet = sketchCoordinates(clip.xy/clip.w);
    vec2 sheetWidth = fwidth(sheet);
    if (dot(u.section.xyz, u.section.xyz) > 1e-12 && dot(u.section.xyz, vWorld) > u.section.w) discard;
    outColor = shadeMesh(vNormal, vColor, vView, vWorld,sheet,sheetWidth);
}
