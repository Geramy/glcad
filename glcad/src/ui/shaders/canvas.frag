#version 450
#extension GL_GOOGLE_include_directive : require
#include "frame.glsl"
#include "view_ray.glsl"
layout(location = 0) in vec2 vNdc;
layout(location = 0) out vec4 outColor;

float gridLine(vec2 w, vec2 fw, float spacing, float widthPx) {
    vec2 g = abs(fract(w / spacing - 0.5) - 0.5) * spacing / max(fw, vec2(1e-9));
    vec2 spacingPx = spacing / max(fw, vec2(1e-9));
    vec2 coverage = (1.0 - smoothstep(vec2(0.0), vec2(widthPx), g)) *
                    smoothstep(vec2(4.0), vec2(14.0), spacingPx);
    return max(coverage.x, coverage.y);
}

void gridAt(vec2 w, vec2 fw, out float mi, out float ma) {
    float c=cos(u.planeN.w),s=sin(u.planeN.w);
    w=mat2(c,-s,s,c)*w;
    fw=fwidth(w);
    float decade = max(u.planeStyle.w, 1e-9);
    mi = gridLine(w, fw, decade, 1.0) * u.planeStyle.z;
    ma = gridLine(w, fw, decade * 10.0, 1.4) * u.planeStyle.z;
}

void main() {
    vec3 p0,d;
    viewRay(vNdc,p0,d);
    vec3 dn = d;

    float tg = (abs(d.z) < 1e-12) ? -1.0 : -p0.z / d.z;
    bool ground = abs(d.z)>=1e-12 && (tg>=0.0 || !perspectiveView());
    vec3 c = u.background.rgb;
    float depth = 1.0;
    if (ground) {
        vec3 w = p0.xyz + d * tg;
        vec4 clip = u.viewProj * vec4(w, 1.0);
        depth = clamp(clip.z / clip.w, 0.0, 1.0) + 4e-7;
        vec2 fw = fwidth(w.xy);
        float mi, ma;
        gridAt(w.xy, fw, mi, ma);
        float fade = smoothstep(0.0, 0.25, abs(dn.z));
        c = mix(c, u.gridMinor.rgb, mi * 0.9 * fade);
        c = mix(c, u.gridMajor.rgb, ma * 0.9 * fade);
        float ax = 1.0 - smoothstep(0.0, 1.5, abs(w.y) / max(fw.y, 1e-9));
        float ay = 1.0 - smoothstep(0.0, 1.5, abs(w.x) / max(fw.x, 1e-9));
        c = mix(c, u.axisX.rgb, ax * 0.85 * fade * u.planeU.w);
        c = mix(c, u.axisY.rgb, ay * 0.85 * fade * u.planeU.w);
    }

    vec3 n = u.planeN.xyz;
    float dd = dot(d, n);
    float ts = (abs(dd) < 1e-12) ? -1.0 : dot(u.planeOrigin.xyz - p0.xyz, n) / dd;
    if (abs(dd)>=1e-12 && (ts>=0.0 || !perspectiveView()) && u.planeStyle.x > 0.0) {
        vec3 w = p0.xyz + d * ts;
        vec3 rel = w - u.planeOrigin.xyz;
        vec2 pu = vec2(dot(rel, u.planeU.xyz), dot(rel, u.planeV.xyz));
        vec2 lo = u.plane.xy, hi = u.plane.zw;
        bool inside = pu.x >= lo.x && pu.x <= hi.x && pu.y >= lo.y && pu.y <= hi.y;
        if (inside && (!ground || ts <= tg + 1e-6)) {
            vec4 clip = u.viewProj * vec4(w, 1.0);
            depth = clamp(clip.z / clip.w - 1e-4, 0.0, 1.0);
            vec2 fw = fwidth(pu);
            float tilt = 1.0 - u.viewport.w;
            float fade = smoothstep(0.0, 0.25, abs(dot(dn, n)));
            vec3 base = (ground && ts <= tg + 1e-6) ? c : u.background.rgb;
            vec3 tint = mix(base, u.gridMajor.rgb, u.planeStyle.x * max(tilt, 0.35));
            float mi, ma;
            gridAt(pu, fw, mi, ma);
            tint = mix(tint, u.gridMinor.rgb, mi * 0.9 * fade);
            tint = mix(tint, u.gridMajor.rgb, ma * 0.9 * fade);
            float ax = 1.0 - smoothstep(0.0, 1.5, abs(pu.y) / max(fw.y, 1e-9));
            float ay = 1.0 - smoothstep(0.0, 1.5, abs(pu.x) / max(fw.x, 1e-9));
            tint = mix(tint, u.axisX.rgb, ax * 0.85 * fade * u.planeU.w);
            tint = mix(tint, u.axisY.rgb, ay * 0.85 * fade * u.planeU.w);
            float ex = min(pu.x - lo.x, hi.x - pu.x) / max(fw.x, 1e-9);
            float ey = min(pu.y - lo.y, hi.y - pu.y) / max(fw.y, 1e-9);
            float glow = 1.0 - smoothstep(0.0, u.planeStyle.y, min(ex, ey));
            glow *= glow;
            c = mix(tint, u.gridMajor.rgb, 0.7 * glow * max(tilt, 0.35));
        }
    }

    gl_FragDepth = depth;
    outColor = vec4(c, 1.0);
}
