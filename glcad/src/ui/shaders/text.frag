#version 450
#extension GL_GOOGLE_include_directive : require
layout(location = 0) in vec2 vUV;
layout(location = 1) in vec4 vColor;
layout(location = 2) in vec4 vClip;
layout(location = 3) in vec2 vScreen;

layout(location = 0) out vec4 outColor;
layout(set = 0, binding = 0) uniform sampler2D uAtlas;

#include "frame.glsl"

void main() {
    if (vScreen.x < vClip.x || vScreen.x > vClip.x + vClip.z ||
        vScreen.y < vClip.y || vScreen.y > vClip.y + vClip.w)
        discard;

    vec2 atlasSize = vec2(textureSize(uAtlas, 0));
    float texelsPerPixel = max(length(dFdx(vUV) * atlasSize),
                               length(dFdy(vUV) * atlasSize));
    float sd = texture(uAtlas, vUV).r;
    // Font encodes the outline at byte 128.
    float distance = (sd - 128.0 / 255.0) * u.viewport.z;
    float a = clamp(distance / max(texelsPerPixel, 1e-4) + 0.5, 0.0, 1.0);

    if (a <= 0.002) discard;
    outColor = vec4(vColor.rgb, vColor.a * a);
}
