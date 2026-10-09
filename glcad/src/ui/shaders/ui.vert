#version 450
#extension GL_GOOGLE_include_directive : require
layout(location = 0) in vec4 iRect;      // x, y, w, h  (logical px)
layout(location = 1) in vec4 iColor;
layout(location = 2) in vec4 iParams;    // radius, thickness, kind, spread
layout(location = 3) in vec4 iClip;      // x, y, w, h

layout(location = 0) out vec2 vLocal;
layout(location = 1) out vec4 vColor;
layout(location = 2) out vec4 vParams;
layout(location = 3) out vec2 vHalf;
layout(location = 4) out vec4 vClip;
layout(location = 5) out vec2 vScreen;

#include "frame.glsl"

void main() {
    vec2 corner = vec2((gl_VertexIndex & 1), (gl_VertexIndex >> 1) & 1);

    float pad = max(iParams.w, iParams.y) + 2.0;
    vec2 pos = iRect.xy - pad + corner * (iRect.zw + 2.0 * pad);

    vScreen = pos;
    vLocal  = pos - (iRect.xy + iRect.zw * 0.5);
    vHalf   = iRect.zw * 0.5;
    vColor  = iColor;
    vParams = iParams;
    vClip   = iClip;

    vec2 ndc = (pos / u.viewport.xy) * 2.0 - 1.0;
    gl_Position = vec4(ndc, 0.0, 1.0);
}
