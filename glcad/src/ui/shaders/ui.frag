#version 450
layout(location = 0) in vec2 vLocal;
layout(location = 1) in vec4 vColor;
layout(location = 2) in vec4 vParams;   // radius, thickness, kind, spread
layout(location = 3) in vec2 vHalf;
layout(location = 4) in vec4 vClip;
layout(location = 5) in vec2 vScreen;

layout(location = 0) out vec4 outColor;

float sd_round_box(vec2 p, vec2 b, float r) {
    vec2 q = abs(p) - b + r;
    return min(max(q.x, q.y), 0.0) + length(max(q, 0.0)) - r;
}

void main() {
    if (vScreen.x < vClip.x || vScreen.x > vClip.x + vClip.z ||
        vScreen.y < vClip.y || vScreen.y > vClip.y + vClip.w)
        discard;

    float radius    = vParams.x;
    float thickness = vParams.y;
    int   kind      = int(vParams.z + 0.5);
    float spread    = vParams.w;

    float d = sd_round_box(vLocal, vHalf, min(radius, min(vHalf.x, vHalf.y)));
    float a = 0.0;

    if (kind == 0) {
        a = 1.0 - smoothstep(-0.7, 0.7, d);
    } else if (kind == 1) {
        a = (1.0 - smoothstep(-0.7, 0.7, d)) *
            smoothstep(-thickness - 0.7, -thickness + 0.7, d);
    } else {
        a = 1.0 - smoothstep(-spread * 0.5, spread, d);
        a *= a;
    }

    if (a <= 0.001) discard;
    outColor = vec4(vColor.rgb, vColor.a * a);
}
